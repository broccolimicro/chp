#include "graph.h"

#include <queue>
#include <ranges>

#include <arithmetic/expression.h>
#include <chp/simulator.h>
#include <common/message.h>
#include <common/text.h>
#include <common/mapping.h>

//TODO(steven.kneiser): delete after development
#include <algorithm>
#include <filesystem>
#include <interpret_arithmetic/export.h>
#include <interpret_chp/export_dot.h>
#include "../tests/dot.h"

#define MAX_DSA_ITERATION 32  //TODO(steven.kneiser): a low ceiling to catch divergers until we support loops in DSA form
#define MAX_PROCESS_COUNT 16
#define INITIAL_DSA_VALUE 1
//DESIGN(steven.kneiser):
//  We 1-index our DSA indices to preserve 0 for a var's initial value
//   as is it sometimes defined in the Reset (see petri::graph Reset encoding's values).
//   This saves us when referring to unenumerated reset variables in the Control-Flow Graph.
//
//  This currently is safe to & eventually will be deprecated,
//   but I'm reserving this space for a few foreseeable moments while I refine Process Decomp.

using arithmetic::Expression;

namespace chp
{

place::place()
{
	arbiter = false;
}

place::~place() {}

// Merge two places and combine the predicate and effective predicate.
// composition can be one of:
// 1. petri::parallel
// 2. petri::choice
// 3. petri::sequence
// See haystack/lib/petri/petri/graph.h for their definitions.
place place::merge(int composition, const place &p0, const place &p1)
{
	place result;
	result.arbiter = (p0.arbiter or p1.arbiter);
	return result;
}

ostream &operator<<(ostream &os, const place &p) {
	return os;
}

transition::transition() {
	guard = arithmetic::Expression::vdd();
	action = true;
}

transition::transition(arithmetic::Expression guard, arithmetic::Choice assign) {
	this->guard = guard;
	this->action = assign;
}

transition::~transition() {}

transition transition::merge(int composition, const transition &t0, const transition &t1) {
	if (composition == petri::parallel or composition == petri::sequence) {
		transition result(t0.guard & t1.guard, t0.action & t1.action);
		result.guard.minimize();
		return result;
	} else if (composition == petri::choice) {
		transition result(t0.guard | t1.guard, t0.action | t1.action);
		result.guard.minimize();
		return result;
	}
	return transition();
}

bool transition::mergeable(int composition, const transition &t0, const transition &t1)
{
	return true;
}

bool transition::is_infeasible()
{
	return guard.isNull() or action.isInfeasible();
}

bool transition::is_vacuous()
{
	return guard.isConstant() and action.isVacuous();
}

ostream &operator<<(ostream &os, const transition &t) {
	os << t.guard << "->" << t.action;
	return os;
}

variable::variable() {
	region = 0;
}

variable::variable(string name, int region) {
	this->name = name;
	this->region = region;
}

variable::~variable() {}

graph::graph() {}

graph::~graph() {}

/**
 * @brief Find or create a net with the given name and region
 * 
 * First tries to find an exact match for the net. If not found and define is true
 * or vars with the same name exist in other regions, creates a new net and connects
 * it to other vars with the same name.
 * 
 * @param name The name of the net to find or create
 * @param region The region for the net
 * @param define Whether to create the net if not found
 * @return The index of the found or created net, or -1 if not found and not created
 */
int graph::netIndex(string name, bool define) {
	int region = 0;
	size_t tic = name.rfind('\'');
	if (tic != string::npos) {
		region = std::stoi(name.substr(tic+1));
		name = name.substr(0, tic);
	}

	vector<int> remote;
	// First try to find the exact net
	for (int i = 0; i < (int)vars.size(); i++) {
		if (vars[i].name == name) {
			remote.push_back(i);
			if (vars[i].region == region) {
				return i;
			}
		}
	}

	// If not found but define is true or we found vars with the same
	// name, create a new net and connect it to the other vars with the
	// same name
	if (define or not remote.empty()) {
		int uid = create(variable(name, region));
		for (int i = 0; i < (int)remote.size(); i++) {
			connect_remote(uid, remote[i]);
		}
		return uid;
	}
	return -1;
}

/**
 * @brief Find the index of a net with the given name and region
 * 
 * Searches for a net by exact name and region match.
 * 
 * @param name The name of the net to find
 * @param region The region to search in
 * @return The index of the net if found, -1 otherwise
 */
int graph::netIndex(string name) const {
	int region = 0;
	size_t tic = name.rfind('\'');
	if (tic != string::npos) {
		region = std::stoi(name.substr(tic+1));
		name = name.substr(0, tic);
	}

	for (int i = 0; i < (int)vars.size(); i++) {
		if (vars[i].name == name and vars[i].region == region) {
			return i;
		}
	}
	return -1;
}

/**
 * @brief Get the name and region of a net by index
 * 
 * @param uid The index of the net
 * @return A pair containing the name and region of the net
 */
string graph::netAt(int uid) const {
	if (uid >= (int)vars.size()) {
		return "";
	}
	return vars[uid].name + (vars[uid].region != 0 ?
		"'" + ::to_string(vars[uid].region) : "");
}

int graph::netCount() const {
	return (int)vars.size();
}

arithmetic::State graph::U() const {
	arithmetic::State result;
	for (size_t i = 0; i < vars.size(); i++) {
		result.values.push_back(arithmetic::Value::U(arithmetic::Value::INT));
	}
	return result;
}

/**
 * @brief Create a new net in the graph
 * 
 * Adds a new net to the graph and initializes its remote connections.
 * If the net is a ghost net, adds it to the ghost_vars list.
 * 
 * @param n The net to create
 * @return The index of the newly created net
 */
//TODO(steven.kneiser): int -> size_t that needs the super/petri::graph to also be updated
int graph::create(variable n) {
	int uid = (int)vars.size();
	vars.push_back(n);
	vars.back().remote.push_back(uid);
	return uid;
}

/**
 * @brief Connect two vars as remote counterparts
 * 
 * Establishes a remote connection between two vars, making them share
 * the same remote list. This is used to connect vars with the same name
 * across different regions.
 * 
 * @param from Index of the first net
 * @param to Index of the second net
 */
void graph::connect_remote(int from, int to) {
	vars[from].remote.insert(vars[from].remote.end(), vars[to].remote.begin(), vars[to].remote.end());
	sort(vars[from].remote.begin(), vars[from].remote.end());
	vars[from].remote.erase(unique(vars[from].remote.begin(), vars[from].remote.end()), vars[from].remote.end());
	vars[to].remote = vars[from].remote;
}


/**
 * @brief Get all remote net groups
 * 
 * A remote group collects all of the isochronic regions of a net. It is a set of vars that are connected
 * to each other via remote connections. This function identifies all distinct remote groups in the graph.
 * 
 * @return A vector of vectors, where each inner vector contains the indices of vars in one remote group
 */
vector<vector<int> > graph::remote_groups() {
	vector<vector<int> > groups;

	for (int i = 0; i < (int)vars.size(); i++) {
		bool found = false;
		for (int j = 0; j < (int)groups.size() and not found; j++) {
			found = (find(groups[j].begin(), groups[j].end(), i) != groups[j].end());
		}
		if (not found) {
			groups.push_back(vars[i].remote);
		}
	}

	return groups;
}

chp::transition &graph::at(term_index idx) {
	return transitions[idx.index];
}

arithmetic::Parallel &graph::term(term_index idx) {
	return transitions[idx.index].action.terms[idx.term];
}

Mapping<petri::iterator> graph::merge(graph g) {
	Mapping<int> netMap(-1, false);

	// Add all of the vars and look for duplicates
	int count = (int)vars.size();
	for (int i = 0; i < (int)g.vars.size(); i++) {
		int uid = (int)vars.size();
		vector<int> remote;
		for (int j = 0; j < count; j++) {
			if (vars[j].name == g.vars[i].name) {
				if (vars[j].region == g.vars[i].region) {
					uid = j;
				}
				remote.push_back(j);
			}
		}

		netMap.set(i, uid);
		if (uid >= (int)vars.size()) {
			vars.push_back(g.vars[i]);
			vars.back().remote = remote;
		}
	}

	// Fill in the remote vars
	for (auto i = netMap.fwd.begin(); i != netMap.fwd.end(); i++) {
		for (int j = 0; j < (int)g.vars[i->first].remote.size(); j++) {
			vars[i->second].remote.push_back(netMap.map(g.vars[i->first].remote[j]));
		}
		sort(vars[i->second].remote.begin(), vars[i->second].remote.end());
		vars[i->second].remote.erase(unique(vars[i->second].remote.begin(), vars[i->second].remote.end()), vars[i->second].remote.end());
	}

	for (int i = 0; i < (int)g.transitions.size(); i++) {
		if (not g.transitions.is_valid(i)) continue;

		g.transitions[i].action.applyVars(netMap);
		g.transitions[i].guard.applyVars(netMap);
	}

	// Remap all expressions to new vars
	return super::merge(g);
}


void graph::post_process(bool proper_nesting, bool aggressive) {
	// Handle Reset Behavior

	bool change = true;
	while (change)
	{
		super::reduce(proper_nesting, aggressive);
		change = false;

		for (int i = 0; i < (int)reset.size(); i++)
		{
			simulator::super sim(this, reset[i]);
			sim.enabled();

			change = false;
			for (int j = 0; j < (int)sim.ready.size() and !change; j++) {
				arithmetic::Expression guard = transitions[sim.ready[j].index].action.guard();
				guard.minimize();

				bool firable = guard.isValid();
				for (int k = 0; k < (int)sim.ready[j].tokens.size() and firable; k++) {
					for (int l = 0; l < (int)arcs[petri::transition::type].size() and firable; l++) {
						if (arcs[petri::transition::type][l].to.index == sim.tokens[sim.ready[j].tokens[k]].index) {
							firable = false;
						}
					}
					for (int l = 0; l < (int)arcs[petri::place::type].size() and firable; l++) {
						if (arcs[petri::place::type][l].from.index == sim.tokens[sim.ready[j].tokens[k]].index and arcs[petri::place::type][l].to.index != sim.ready[j].index) {
							firable = false;
						}
					}
				}

				if (firable) {
					petri::enabled_transition t = sim.fire(j);
					reset[i].tokens = sim.tokens;
					//cout << "firing reset action " << transitions[t.index].guard << "->" << transitions[t.index].action << endl;
					for (int k = (int)transitions[t.index].action.terms.size()-1; k >= 0; k--) {
						int idx = i;
						if (k > 0) {
							idx = reset.size();
							reset.push_back(reset[i]);
						}

						arithmetic::State guard_action = U();
						passesGuard(reset[idx].encodings, reset[idx].encodings, transitions[t.index].guard, &guard_action);
						//cout << "passesGuard " << reset[idx].encodings << " " << transitions[t.index].guard << " " << guard_action << endl;
						// TODO(edward.bingham) set up a global encoding and actually simulate the guards
						reset[idx].encodings &= guard_action;
						//cout << "evaluating " << transitions[t.index].action.terms[k] << " " << reset[idx].encodings << endl;

						arithmetic::State local = transitions[t.index].action.terms[k].evaluate(reset[idx].encodings);
						//cout << "result " << local << endl;
						arithmetic::State remote = local.remote(remote_groups());
						//cout << "remote " << remote << endl;

						reset[idx].encodings = localAssign(reset[idx].encodings, remote, true);
						//cout << "localAssign " << reset[idx].encodings << endl;
					}

					//cout << endl << endl;
					change = true;
				}
			}
		}
	}

	/*change = true;
	while (change) {
		super::reduce(proper_nesting, aggressive);*/

		// If there is a guard at the end of a conditional branch, then we unzip
		// the conditional merge by one transition (make copies of the next
		// transition on each branch and move the merge down the sequence). This
		// allows us to merge that guard at the end of the conditional branch into
		// the transition.
		/*for (petri::iterator i(place::type, 0); i < (int)places.size() and not change; i++) {
			if (not is_valid(i)) continue;

			vector<petri::iterator> p = prev(i);
			vector<petri::iterator> active, passive;
			for (int k = 0; k < (int)p.size(); k++) {
				if (transitions[p[k].index].action.isPassive()) {
					passive.push_back(p[k]);
				} else {
					active.push_back(p[k]);
				}
			}

			if (passive.size() > 1 or (passive.size() == 1 and active.size() > 0)) {
				vector<petri::iterator> copies;
				if ((int)active.size() == 0) {
					copies.push_back(i);
				}

				vector<petri::iterator> n = next(i);
				vector<vector<petri::iterator> > nn;
				for (int l = 0; l < (int)n.size(); l++) {
					nn.push_back(next(n[l]));
				}
				vector<vector<petri::iterator> > np;
				for (int l = 0; l < (int)n.size(); l++) {
					np.push_back(prev(n[l]));
					np.back().erase(std::remove(np.back().begin(), np.back().end(), i), np.back().end());
				}

				for (int k = 0; k < (int)passive.size(); k++) {
					// Disconnect this transition
					for (int l = (int)arcs[petri::transition::type].size()-1; l >= 0; l--) {
						if (arcs[petri::transition::type][l].from == passive[k]) {
							erase_arc(petri::iterator(petri::transition::type, l));
						}
					}

					if (k >= (int)copies.size()) {
						copies.push_back(create(places[i.index]));
						for (int l = 0; l < (int)n.size(); l++) {
							petri::iterator x = copy(n[l]);
							connect(copies.back(), x);
							connect(x, nn[l]);
							connect(np[l], x);
						}
					}

					connect(passive[k], copies[k]);
				}

				change = true;
			}
		}
		if (change)
			continue;*/

		/*for (petri::iterator i(transition::type, 0); i < (int)transitions.size() and not change; i++) {
			if (not is_valid(i)) continue;

			vector<petri::iterator> p = prev(i);
			vector<petri::iterator> n = next(i);

			if (transitions[i.index].action.isVacuous() and (p.size() <= 1u or n.size() <= 1u)) {
				vector<petri::iterator> nn = next(n); // transitions
				for (int l = 0; l < (int)nn.size(); l++) {
					transitions[nn[l].index] = transition::merge(petri::sequence, transitions[i.index], transitions[nn[l].index]);
				}

				cout << "pinching vacuous action " << i << endl;

				pinch(i);
				change = true;
			}
		}
		if (change) {
			continue;
		}
	}*/
}

//
// Render init sequence of assignments if they exist
//TODO: migrate this upstream where? ...all the way to petri::graph constructor? nah, but interpret_chp/import_cog.cpp::import_cog(...)?
//
void graph::renderReset() {
	//TODO: make method idempotent
	//bool isResetRendered = false;

	// First, find the entry point(s) to follow the reset
	//TODONE: ah, do I want to grab a handle through thte reset tokens instead? If there are multiple, shouldn't I need to attach this sequence to all of them via parallel splits
	//TODO: properly support multiple reset-states
	vector<petri::iterator> entryIts;
	for (const chp::state &resetState : this->reset) {
		for (const petri::token &token : resetState.tokens) {
			entryIts.push_back(petri::iterator(petri::place::type, token.index));
		}
	}


	//petri::iterator entryIt;
	//bool entryFound = false;
	//for (TransitionIdx transitionIdx = 0; transitionIdx < this->transitions.size(); transitionIdx++) {
	//	petri::iterator t_it(transition::type, transitionIdx);

	//	if (this->is_valid(t_it)) {
	//		entryFound = true;
	//		entryIt = t_it;
	//		break;
	//	}
	//}
	//if (not entryFound) { return; } //TODO: perhaps in this case, the reset is all there is ...but then there's no channel I/O, so is there a point?? desired side-effects? at least convenient virtual metadata to preserve at least for compile-time?

	// Unpack initialization assignments hiding in petri::graph's reset metadata
	chp::state newResetState;
	petri::iterator newHeadIt;
	for (const chp::state &resetState : this->reset) {  //TODO: properly support multiple reset states
		VarIdx varIdx = 0;
		for (const arithmetic::Value &varReset : resetState.encodings.values) {
			if (varReset.state != arithmetic::Value::StateType::VALID) { continue; }

			string varName = this->vars[varIdx].name;
			VarValue initValue = varReset.ival;
			cout << "reset> " << varName << " := " << std::to_string(initValue) << endl;

			arithmetic::Action varInit(
					arithmetic::Expression::varOf(varIdx),
					arithmetic::Expression::intOf(initValue));
			chp::transition varInitTransition(
					arithmetic::Expression::vdd(), arithmetic::Choice({{varInit}}));
			//this->super::insert_before(entryIt, varInitTransition);

			if (newHeadIt.index == -1) {
				petri::iterator newResetIt = this->super::create(chp::place());
				newResetState.tokens.push_back(newResetIt.index);
				petri::iterator varInitIt = this->super::create(varInitTransition);
				this->connect(newResetIt, varInitIt);  // order matters (e.g. redefinitions like "a=2;b=a*3;a=b+5")

				for (petri::iterator entryIt : entryIts) {
					this->connect(varInitIt, entryIt);
				}
				newHeadIt = varInitIt;

				varIdx++;
				continue;
			}

			//TODO: does order matter? yes, for redefs (a=2;b=a*3;a=b+5)
			//TODO: parallelize these as much as possible
			newHeadIt = this->super::insert_after(newHeadIt, varInitTransition);
			varIdx++;
		}
	}

	this->reset = {newResetState};
}


vector<graph> graph::decompose() {  //chp::graph &g) {}
	// TODO: Return new additional subgraphs (optional: pass w/ self for forest of processes)
	cout << endl << "\\_,.=~-^*'\"`\\_,.=~-^*'\"`\\_,.=~-^*'\"`\\_,.=~-^*'\"`\\_,.=~-^*'\"`\\_,.=~-^*'\"`\\_,.=~-^*'\"`\\_,.=~-^*'\"`" << endl << endl;
	cout << "decomposing: " << this->name << endl;

	if (transitions.count() == 0) {
		cout << "decomposed." << endl;
		return {*this};
	}

	this->convertToDSA();

	//// if (debug) {} ??
#ifdef GRAPHVIZ_SUPPORTED
	std::filesystem::path debugDirPath = std::filesystem::current_path() / "build" / "dbg";
	string prefix = "";
	string dsa_filename = (debugDirPath / (prefix + this->name + "_dsa.png")).string();
	string dsa_dot = chp::export_graph(*this, true, false).to_string();
	gvdot::render(dsa_filename, dsa_dot);
#endif

	this->computeUseDefChains();

#ifdef GRAPHVIZ_SUPPORTED
	//std::filesystem::path debugDirPath = std::filesystem::current_path() / "build" / "dbg";
	//string prefix = "";
	string pre_analysis_filename = (debugDirPath / (prefix + this->name + "_analysis_pre.png")).string();
	string pre_analysis_dot = chp::export_analysis(*this, true, true).to_string();
	gvdot::render(pre_analysis_filename, pre_analysis_dot);
#endif

	vector<graph> processes = this->project();

#ifdef GRAPHVIZ_SUPPORTED
	//std::filesystem::path debugDirPath = std::filesystem::current_path() / "build" / "dbg";
	//string prefix = "";
	string post_analysis_filename = (debugDirPath / (prefix + this->name + "_analysis_post.png")).string();
	string post_analysis_dot = chp::export_analysis(*this, true, true).to_string();
	gvdot::render(post_analysis_filename, post_analysis_dot);
#endif

	cout << "decomposed." << endl << endl;
	return processes;

	// TODO Process Decomposition and Projection
	//
	// The goal of this project is to break up a large sequential process into
	// many smaller parallel processes. This step in the compilation has an
	// enormous effect on the performance and power of the finaly design. It
	// increases parallelism and reduces the size of the state space for each
	// process, making it easier for the later stages of the compilation process
	// to complete.
	//
	// The following steps are guidelines and not hard rules. If you think you
	// found a better way to approach the problem, then feel free to chase that
	// down. If you need supporting infrastructure anywhere else in the project,
	// feel free to add that in. If you need to modify this function definition,
	// go for it.
	//
	// 1. Fill out chp::graph as needed
	// 	 a. Work with team members to identify shared and unique requirements for the CHP graph class
	//   b. Create the data structures necessary to implement those requirements
	//
	// 2. Process Decomposition
	//   a. Given a collection of contiguous actions in the CHP graph determine the
	//      set of input and output data dependencies for those actions
	//   b. Cut those actions out of the process and put them in their own process
	//   c. Create two new channels
	//   d. Insert channel actions to tie together the new processes
	//
	// 3. Projection
	//   a. Identify dataless channels
	//   b. Look for probes on those channels in multi-guard selection statements
	//   c. Mark all channels without those probes as slack elastic
	//
	// === Successful completion of project ===
	//
	// Your time is yours, what do you want to tackle next?
	// Some ideas:
	// 1. Remove all channel actions for dataless slack elastic channels
	// 2. Delete channels from variable space
	//
	// Final cleaup:
	// 1. Clean up any bugs
	// 2. Prepare demo
	// 3. Document as needed

	//string dot = chp::export_graph(*this, true, true).to_string();
	//string dot_filename = (std::filesystem::current_path() / ("test_" + this->name)).string();
	//ofstream dot_file(dot_filename);
	//if (!dot_file) {
	//	std::cerr << "ERROR: Failed to open file for dot export: "
	//		<< dot_filename << std::endl;
	//	//<< "ERROR: Try again from dir: <project_dir>/lib/flow" << std::endl;

	//}  else {
	//	dot_file << dot;
	//}
}

void graph::expand() {
	// TODO Handshake Expansion and Reshuffling
	//
	// The goal of this project is given a CHP circuit specification, break that
	// specification down into boolean assignments and guards on wires, and write
	// that out into an HSE for the rest of the flow. The Handshake Reshuffling
	// step has a large effect on the difficulty of the state variable insertion
	// algorithm and therefore the size of the circuit down the line.
	//
	// The following steps are guidelines and not hard rules. If you think you
	// found a better way to approach the problem, then feel free to chase that
	// down. If you need supporting infrastructure anywhere else in the project,
	// feel free to add that in. If you need to modify this function definition,
	// go for it.
 	//
	// 1. Fill out chp::graph as needed
	//   a. Work with team members to identify shared and unique requirements for the CHP graph class
	//   b. Create the data structures necessary to implement those requirements
	//
	// 2. Build channel protocol definitions into CHP language
	//   a. define language in haystack/lib/parse_chp
	//   b. create structures in haystack/lib/chp to support multiple processes
	//   c. work with haystack/lib/interpret_chp to fill in those processes from the parsed abstract syntax tree
	//
	// 3. Expand channel actions
	//   a. Cut and paste channel protocol definitions
	//   b. Create the more complex data dependencies necessary for the most parallel implementation of each composition operation
	//
	// 4. Handshake Reshuffling
	//   a. Create a function that can prune a set of orderings given as input, while maintaining circuit correctness (stability, non-interference, etc)
	//
	// === Successful completion of project ===
	//
	// Your time is yours, what do you want to tackle next?
	// Some ideas:
	// 1. Identify the orderings which result in the most ambiguous state encodings, or are associated with the most conflicts.
	// 2. Apply your previous function to prune those orderings from the handshaking expansions.
	// 3. create an HSE exporter for your expanded CHP graph.
	//
	// Final cleaup:
	// 1. Clean up any bugs
	// 2. Prepare demo
	// 3. Document as needed
}

void graph::flatten(bool debug) {
	if (debug) { cout << "¿Yµ wWµøT? " << this->name << endl; }

	if (!this->split_groups_ready) {
		this->compute_split_groups();
	}

	// Index of places w/ multiple outputs -> Indices of their output transitions
	std::map<size_t, std::set<size_t>> split_places;

	for (const petri::iterator &place : this->get_places()) {
		std::vector<petri::iterator> out_transitions = this->super::next(place);

		if (out_transitions.size() > 1) {
			//TODO: prune
			//std::set<size_t> out_transition_idxs = out_transitions | std::views::transform([](const petri::iterator &it) { return it.index; });
			//split_places[place.index] = std::set<size_t>(out_transition_idxs.begin(), out_transition_idxs.end());

			for (petri::iterator transition : out_transitions) {
				split_places[place.index].insert(transition.index);
			}
		}
	}

	if (split_places.empty()) {
		//TODO: what if no splits? already flattened? Brainstorm example
		// aha! e.g. see ds_adder_flat where shared transitions need to be duplicated (s & co assignment)
		if (debug) {
			cout << "Why no split places?" << endl
				<< "¡Yµ wWµøT!" << endl << endl;
		}
		return;
	}

	// Split-place adjacency list
	std::map<petri::iterator, std::set<petri::iterator> > monopartite_split_projection;

	// Transition index of split child -> connected splits
	//std::map<petri::iterator, std::set<petri::iterator> > split_ancestors;

	// Transition index of split child -> all transition indices until next split or halt
	std::map<petri::iterator, std::vector<petri::iterator>> transition_sequences;

	// Project out a subgraph of only the directed ins&outs between place-based splits
	// Also identify trailing children (sequences of transitions) to be merged
	for (const auto &[place_idx, transition_idxs] : split_places) {
		//TODO: iterate over places for the modern, idiomatic way (w/o pre-pass for split_groups) (oh, do we need the projection pre-explored so we can do quick set-membership look-ups?
		//const vector<petri::iterator> &out_transitions = this->super::out(place_idx);
		//if (out_transitions.size() < 2) { continue; }
		//vector <size_t> transition_idxs = ;

		for (const auto &split_transition_idx : transition_idxs) {
			std::set<petri::iterator> transition_sequence_members;
			std::vector<petri::iterator> transition_sequence;

			//TODO: create a bound (petri/iterator.h)? (collection of regions composed in choice)
			//TODO: for split_transition_idx, crawl breadth-first & acquire every transition_idx until split-places or last transition
			petri::iterator curr(transition::type, split_transition_idx);
			//cout << endl << "====> " << curr.to_string();

			// Breadth-first search from transition to all ends, stopping at other split-places
			std::set<petri::iterator> visited;
			std::queue<petri::iterator> queue;
			queue.push(curr);
			while (not queue.empty()) {
				curr = queue.front();
				queue.pop();
				visited.insert(curr);
				//cout << endl << "[" << curr.to_string() << "] -> ";

				for (const petri::iterator &next_place_it : this->next(curr)) {

					// Halt at other split-places
					if (split_places.contains(next_place_it.index)) {
						petri::iterator parent_split(place::type, place_idx);
						//// Ignore circular self-references? No, useful info for dominance calculation later ...just skip when modifying graph
						//if (next_place_it == parent_split) { continue; }
						monopartite_split_projection[next_place_it].insert(parent_split);

						//cout << endl << "-=-=-" << next_place_it.to_string() << "-=-=-";
						//std::set<petri::iterator> split_neighbors = monopartite_split_projection[next_place_it];
						//std::copy(split_neighbors.begin(), split_neighbors.end(), std::ostream_iterator<petri::iterator>(cout, " "));
						//cout << "-=-=-" << endl;
						continue;
					}

					for (const petri::iterator &next_transition_it : this->next(next_place_it)) {
						//TODO: store all split_transitions in a set for quicker lookup to halt crawl
						if (visited.contains(next_transition_it)) { continue; }
						queue.push(next_transition_it);

						//cout << next_transition_it.to_string() << " ";
						if (!transition_sequence_members.contains(next_transition_it)) {
							transition_sequence_members.insert(next_transition_it);
							transition_sequence.push_back(next_transition_it);
						}
					}
				}
			}

			petri::iterator split_transition_it(transition::type, split_transition_idx);
			transition_sequences[split_transition_it] = transition_sequence;
		}
	}

	auto print_map = [](const auto &m) {
		for (const auto &[key, values] : m) { cout << key << ": {";
			for (auto it = values.begin(); it != values.end(); ++it) {
				cout << *it; if (std::next(it) != values.end()) { cout << ", "; }
			} cout << "}" << std::endl; } cout << endl; };

	if (debug) {
		print_map(split_places);
		print_map(monopartite_split_projection);
		print_map(transition_sequences);
	}

	//TODO: replace indices w/ iterators
	// Identify most dominant split
	auto dominance(this->split_dominance());
	//TODO: compute dominance can be handled in a helper (e.g. this->compute_dominance() & this->dominance_ready w/ dominance relation AND dominance frontiers)
	//TODO: max(in_degree) feels more proper, but is this sufficient?
	int most_dominant_split_place = -1;  //TODO: replace with proper petri::iterator
	size_t greatest_out_degree = 0;

	for (const auto &[split_place, out_neighbors] : monopartite_split_projection) {
		size_t out_degree = out_neighbors.size();

		if (out_degree > greatest_out_degree
				|| (out_degree == greatest_out_degree && split_place.index < most_dominant_split_place)) {
			greatest_out_degree = out_degree;
			most_dominant_split_place = split_place.index;
		}
	}
	if (debug) { cout << endl << "][][][][  DOM> " << most_dominant_split_place << endl << endl; }
	petri::iterator dominator(place::type, most_dominant_split_place);

	//TODO: each of these big comments could be a helper
	// From "dominator" split, iteratively merge child splits by depth
	//TODO: durr, you gotta start bottom-up
	while (monopartite_split_projection.size() > 1) {

		// Flatten one level of nesting split-places
		for (const petri::iterator &place_to_merge : monopartite_split_projection[dominator]) {

			// Skip circular self-references
			if (place_to_merge == dominator) { continue; }
			if (debug) { cout << endl << "====> PLACE_TO_MERGE> " << place_to_merge.to_string() << endl; }

			// Identify parent(s) transition_sequence to concatenate with
			//TODO: might need non-const to mutate
			for (const petri::iterator &in_transition : this->prev(place_to_merge)) {
				if (debug) { cout << "in_transition> " << in_transition.to_string() << endl << endl; }
				for (const auto &[parent_sequence_head, parent_sequence] : transition_sequences) {
					// Concatenate the predicates, then concatenate the transition sequences (even dropping the sequence&split from their respective maps)

					if (parent_sequence.empty()) {
						if (parent_sequence_head != in_transition) { continue; }
						// Found a lone parent head/predicate with no body to merge with!

						//TODO: Don'tRepeatYourself: copied from the more involved sequence concatenation after
						arithmetic::Expression parent_predicate = this->transitions[parent_sequence_head.index].guard;

						for (const petri::iterator &child_transition : this->next(place_to_merge)) {
							if (debug) {
								cout << "PARENT_HEAD> " << parent_sequence_head.to_string() << endl;
								cout << " CHILD_HEAD> " << child_transition.to_string() << endl << endl;
							}

							arithmetic::Expression child_predicate = this->transitions[child_transition.index].guard;
							if (debug) {
								cout << "P_predicate> " << parent_predicate.to_string();
								cout << "C_predicate> " << child_predicate.to_string();
							}

							// Concatenate the predicates
							//TODO: logical <-> bitwise decision needed?
							arithmetic::Expression merged_predicate = parent_predicate && child_predicate;
							if (debug) { cout << "M_predicate> " << merged_predicate.to_string() << endl; }
							//merged_predicate.minimize(); //TODO: !!! What petri::graph vars/etc need to be updated on modification (e.g. info deleted & added)

							if (debug) { cout << "(empty parent_body)> "; }
							vector<petri::iterator> child_sequence = transition_sequences[child_transition];
							if (debug) { std::copy(child_sequence.begin(), child_sequence.end(), std::ostream_iterator<petri::iterator>(cout, " ")); }
							if (debug) { cout << endl; }

							//TODO: splice out in+out arcs to parent_sequence_head
							for (const auto &parent_in_arc :  this->in(parent_sequence_head)) {
								this->erase_arc(parent_in_arc);
							}

							// Create graph copies of desired sequences
							//TODO: but that'll over-sequentialize tiny sub-parallelism within branch ( but we'll reconstruct it when we analyze it anyways)
							//TODO: can we copy a region or bound?
							petri::iterator new_head = this->create(transition::type);
							transition *new_transition = &this->transitions[new_head.index];
							new_transition->guard = merged_predicate;
							this->connect(dominator, new_head);

							vector<petri::iterator> new_sequence = this->copy(child_sequence);
							this->connect(new_sequence);
							if (debug) {
								cout << "NEW> ";
								std::copy(new_sequence.begin(), new_sequence.end(), std::ostream_iterator<petri::iterator>(cout, " "));
								cout << endl << endl;
							}

							// Attach new sequence/branch where parent used to be
							this->connect(new_head, new_sequence.front());
							this->connect(new_sequence.back(), dominator);
						}
						continue; // don't proceed to full-parent search if parent_sequence.empty()! Find a better way to merge these 2 cases
					}

					// Found a full parent (head/predicate AND body > 0) to flatten/concatente!
					//TODO: merge_transition_sequences or something more composable would be a great DRY helper function
					if (parent_sequence.back() == in_transition) {
						//TODO: is transition.guard unique to chp::transition and not petri?? ah, piped in through template
						arithmetic::Expression parent_predicate = this->transitions[parent_sequence_head.index].guard;

						for (const petri::iterator &child_transition : this->next(place_to_merge)) {
							if (debug) {
								cout << "PARENT_HEAD> " << parent_sequence_head.to_string() << endl;
								cout << " CHILD_HEAD> " << child_transition.to_string() << endl << endl;
							}

							arithmetic::Expression child_predicate = this->transitions[child_transition.index].guard;
							if (debug) {
								cout << "P_predicate> " << parent_predicate.to_string();
								cout << "C_predicate> " << child_predicate.to_string();
							}

							// Concatenate the predicates
							//TODO: logical <-> bitwise decision needed?
							arithmetic::Expression merged_predicate = parent_predicate && child_predicate;
							if (debug) { cout << "M_predicate> " << merged_predicate.to_string() << endl; }
							//merged_predicate.minimize(); //TODO: !!! What petri::graph vars/etc need to be updated on modification (e.g. info deleted & added)
																					 //TODO: petri/tests/graph.cpp::flatten
																					 //TODO: merge branches with logically-equivalent predicates

																					 // Concatenate the transition sequences
																					 //TODO: be mindful of what labels/etc are duplicated/copied or referenced
							vector<petri::iterator> merged_sequence(parent_sequence);
							vector<petri::iterator> child_sequence = transition_sequences[child_transition];
							merged_sequence.insert(merged_sequence.end(), child_sequence.begin(), child_sequence.end());

							if (debug) {
								cout << "MERGED> ";
								std::copy(merged_sequence.begin(), merged_sequence.end(), std::ostream_iterator<petri::iterator>(cout, " "));
								cout << endl;
							}

							//TODO: splice out in+out arcs to parent_sequence_head
							for (const auto &parent_in_arc :  this->in(parent_sequence_head)) {
								this->erase_arc(parent_in_arc); //TODO: NONONO MODIFIYING THE LIST YOU'RE ITERATING OVER
							}

							// Create graph copies of desired sequences
							//TODO: but that'll over-sequentialize tiny sub-parallelism within branch ( but we'll reconstruct it when we analyze it anyways)
							//TODO: can we copy a region or bound?
							petri::iterator new_head = this->create(transition::type);
							transition *new_transition = &this->transitions[new_head.index];
							new_transition->guard = merged_predicate;
							this->connect(dominator, new_head);

							vector<petri::iterator> new_sequence = this->copy(merged_sequence);
							this->connect(new_sequence);
							if (debug) {
								cout << "NEW> ";
								std::copy(new_sequence.begin(), new_sequence.end(), std::ostream_iterator<petri::iterator>(cout, " "));
								cout << endl << endl;
							}

							// Attach new sequence/branch where parent used to be
							this->connect(new_head, new_sequence.front());
							//TODO: only connect if monopartite projection terminates or include dominator
							this->connect(new_sequence.back(), dominator);
						}

						break;
					}
				}
			}

			//bound trail = bound::from_nodes({dominator, place_to_merge});
			//std::copy(trail.begin(), trail.end(), std::ostream_iterator<region>(cout, " "));
			//cout << " -=-=- " << trail.to_string() << endl;
		}

		//TODO:

		break; //TODO: just for debugging
	}

	// Is the dominator in the reset? if not, march to marking & unzip to dominator
	//TODO: handle pre-disconnected start-up sequence (shuffle on to each branch)
	if (this->reset.empty()) {
		cout << "internal: empty reset" << endl;
		return;
	}
	state &marking = this->reset[0];  //TODO: handle multiple resets in this->reset?
	if (marking.tokens.empty()) {
		cout << "internal: empty reset marking" << endl;
		return;
	}
	int start_idx = marking.tokens[0].index; //TODO: handle multi-token markings
	petri::iterator start(place::type, start_idx);
	if (debug) { cout << endl << endl << "=== RESET>" << start.to_string() << endl; }

	// Unzip all merge-places (multiple inputs, one output) to dominator
	//TODO: fix multiple recursively, not just closest ancestor to dominator
	// e.g., crawl EVERY input of dominator until it returns to itself or terminates,
	// seeking a sequence with a conditional-merge-place ...to unzip forwards to dominator again
	// ...so do an initial crawl to identify a vector of these points, then for each do the unzips
	// (order might matter, so take note of distance or inter-relationships between these conditional merges

	petri::iterator final_merge(dominator);
	while (this->super::prev(final_merge).size() == 1) {
			final_merge = this->super::prev(final_merge)[0];
	}

	if (debug) { cout << "final_merge: " << final_merge.to_string() << endl; }

	set<petri::iterator> visited;
	visited.insert(final_merge);
	while (final_merge != dominator) {

		petri::iterator next = this->unzip_forwards(final_merge);
		if (debug) {
			cout << next << " "; //next.to_string() << " ";
		}

		if (visited.contains(next)) { break; }  // Detect cycles
		if (visited.size() > 64) {
			cerr << "ERROR: Unable to unzip to dominator" << endl;
			break;
		}  //TODO: HACK: detect divergent runaway bug
		//if (next == start) {
		//	if (debug) {
		//		cerr << "ERROR: Unable to unzip to dominator" << endl;
		//	}
		//	break;
		//}

		visited.insert(next);
		final_merge = next;
	}

	if (marking.tokens.size() > 1) {
		marking.tokens.erase(marking.tokens.begin(), marking.tokens.end() - 1);
		this->reset.erase(this->reset.begin() + 1, this->reset.end());
	}
	if (debug) { cout << endl; }

	// Recompute split groups after flattening
	//this->mark_modified();
	this->post_process(true, false);
	this->split_groups_ready = false;
	this->compute_split_groups();

	if (debug) { cout << "¡Yµ wWµøT!" << endl << endl; }
}

bool graph::isFlat() const {
	//TODO: cache result for rapid look-up in chp::graph

	// Collect subset of places that have either
	// multiple inputs or multiple outputs
	set<petri::iterator> multi_places;  // Multiple inputs
	for (petri::iterator place_it : this->get_places()) {
		if (this->super::in(place_it).size() > 1
				|| this->super::out(place_it).size() > 1) {
			multi_places.insert(place_it);
		}
	}

	// If every pair of multi_place is in parallel, graph is flat
	//TODO: there are more flat graphs that don't fit this constraint
	for (petri::iterator a : multi_places) {
		for (petri::iterator b : multi_places) {
			if (a != b
					&& !this->super::is(petri::composition::parallel, a, b, true, true)) {
				return false;
			}
		}
	}

	return true;
}

arithmetic::Expression graph::exclusion(int index) const {
	arithmetic::Expression result;
	vector<int> p = prev(transition::type, index);

	for (int i = 0; i < (int)p.size(); i++) {
		vector<int> n = next(place::type, p[i]);
		if (n.size() > 1) {
			for (int j = 0; j < (int)n.size(); j++) {
				if (n[j] != index) {
					result = result | transitions[n[j]].guard;
				}
			}
		}
	}
	return result;
}

void graph::computeControlFlowGraph() {
	this->controlFlowGraphReady = false;

	////// Find starting node & populate entry block
	////petri::iterator init_transition;
	////for (petri::iterator i = this->begin(transition::type); i < this->end(transition::type); i++) {
	////	if (this->is_valid(i)) {
	////		init_transition = i;
	////		break;
	////	}
	////}

	////if (not init_transition.valid()) {
	////	clog << "CFG is empty" << endl;
	////	this->controlFlowGraphReady = true;
	////	return;
	////}

	// Populate initial block w/ embedded reset states
	//TODO: Should we expect unmarked, reset-less graphs? Possibly as malformed artifacts from failing Process Decomposition
	if (this->reset.empty()) {
		clog << "No reset detected!" << endl;
		this->controlFlowGraphReady = true;
		return;
	}

	//// Find one of any "initial transitions" pointed to by place(s) marked for reset
	//const petri::token &resetVar = resetState.tokens[0];
	//petri::iterator resetPlace(petri::place::type, resetVar.index);
	//// Use any initial transition to spawn crawl
	//TransitionIdx oneResetTransitionIdx = this->next(resetPlace)[0].index;
	//petri::iterator oneResetTransition(petri::transition::type, oneResetTransitionIdx);

	// Find all "initial transitions" pointed to by place(s) marked for reset
	//const chp::state &resetState = this->reset[0];
	set<petri::iterator> resetTransitions;
	for (chp::state &resetState : this->reset) {
		for (const petri::token &resetVar : resetState.tokens) {
			petri::iterator resetPlace(petri::place::type, resetVar.index);

			for (petri::iterator resetTransition : this->next(resetPlace)) {
				resetTransitions.insert(resetTransition);
			}
		}
	}

	// Create initial CFG block w/ reset definitions
	petri::iterator oneResetTransition = *resetTransitions.begin();
	chp::graph::controlFlowBlock resetBlock(0, true, {oneResetTransition});
	//TODO: Now insert proper init block for reset encodings/values with outs & analysis metadata pre-populated for worklist? (e.g. outs, gens, postDefs)
	//TODO: aha, sufficient just to insert as preDefs to this one? or will we need a true, empty "init" block like in the compiler literature (dragon book)?
	this->controlFlowGraph.push_back(resetBlock);

	// Crawl transitions breadth-first to cluster into Control-Flow Graph blocks
	//   with path-tracking to uncover most-relevant "Reaching Definitions"
	petri::iterator current_it;
	vector<petri::iterator> current_path;  // Full path to current node, including current node
	std::queue<vector<petri::iterator>> queue;
	std::set<petri::iterator> visited;
	queue.push({oneResetTransition});

	while (not queue.empty()) {
		vector<petri::iterator> current_path = queue.front();
		queue.pop();
		//if (not current_path.empty()) {
		//	current_path = vector<petri::iterator>(current_path.begin(), current_path.end() - 1);
		//}

		current_it = current_path.back();
		visited.insert(current_it);

		BlockIdx current_block_uid = this->transitionToBlock[current_it.index];
		chp::graph::controlFlowBlock current_block = this->controlFlowGraph[current_block_uid];

		// If statement is an assignment, document gen-kill sets
		auto [var_assigned, prevDefinitions] = this->getPreviousDefinitions(current_it, current_path);
		if (var_assigned != -1) {
			this->controlFlowGraph[current_block.uid].gens[current_it.index] = var_assigned;

			if (not prevDefinitions.empty()) {
				TransitionIdx transitionToKill = prevDefinitions.back();
				this->controlFlowGraph[current_block.uid].kills[current_it.index] = transitionToKill;
			}
		}

		//
		// Crawl to & connect transitions/statements up next
		//
		vector<petri::iterator> out_transitions;
		for (petri::iterator out_place : this->next(current_it)) {
			for (petri::iterator out_transition : this->next(out_place)) {
				out_transitions.push_back(out_transition);
			}
		}

		// Only leads to 1 transition: could it be in the same block?
		size_t out_transitions_count = out_transitions.size();
		if (out_transitions_count == 1) {
			petri::iterator next_transition_it = out_transitions[0];

			// Edge-case: returning to init entry OR joining a pre-discovered merge ahead
			bool already_in_block = this->transitionToBlock.contains(next_transition_it.index);
			if (already_in_block) {
				BlockIdx next_block_uid = this->transitionToBlock[next_transition_it.index];
				//if (next_block_uid == 0) { continue; }  //TODO: remove hack after testing. This cut unrolls the unconiditional loopback from program end-to-beginning for repetition-intolerant DSA enumeration. Once nested repetitions are supported, this can go.
				this->controlFlowGraph[next_block_uid].ins.insert(current_block.uid);
				this->controlFlowGraph[current_block.uid].outs.insert(next_block_uid);
				continue;
			}

			// Edge-case: discovering a new merge ahead
			// There might be a merge at the next transition OR even the place on the way!
			// Pure, self-contained cases where (ONLY 1 transition -> many local-only places -> 1 transition) work appropriately
			set<petri::iterator> neighbor_transitions_merging_ahead;
			for (petri::iterator place_to_next_transition : this->prev(next_transition_it)) {
				for (petri::iterator neighbor_transition : this->prev(place_to_next_transition)) {
					neighbor_transitions_merging_ahead.insert(neighbor_transition);
				}
			}

			size_t next_transition_in_count = neighbor_transitions_merging_ahead.size();
			if (next_transition_in_count > 1) {

				// Create new block
				BlockIdx new_block_uid = this->controlFlowGraph.size();
				bool is_reset_block = resetTransitions.contains(next_transition_it);
				chp::graph::controlFlowBlock new_block(
						new_block_uid,
						is_reset_block,
						{next_transition_it},
						{current_block.uid});
				this->controlFlowGraph.push_back(new_block);
				this->transitionToBlock[next_transition_it.index] = new_block_uid;
				this->controlFlowGraph[current_block.uid].outs.insert(new_block_uid);

				// If this new block is a reset block, we've discovered a repetition! Don't preserve path to reset.
				if (is_reset_block) {
					current_path = {next_transition_it};

				} else {
					current_path.push_back(next_transition_it);
				}
				queue.push(current_path);
				continue;
			}

			// Default-case (sequence): Append to current block
			//this->controlFlowGraph[current_block_uid].last = next_transition_it;
			this->controlFlowGraph[current_block_uid].transitions.push_back(next_transition_it);
			this->transitionToBlock[next_transition_it.index] = current_block.uid;

			current_path.push_back(next_transition_it);
			queue.push(current_path);
			continue;

		// We assume ALWAYS non-terminating process (i.e. no dead-ends)
		} else if (out_transitions_count == 0) {
			cerr << "ERROR: Terminating / dead-end Transition found. We assume ALWAYS non-terminating processes." << endl;
			return;
		}

		// Wrap up the current block & initiate new ones
		for (petri::iterator &next_transition_it : out_transitions) {

			// Has this already been documented? If so, ensure our block connects to theirs
			bool already_in_block = this->transitionToBlock.contains(next_transition_it.index);
			if (already_in_block) {
				BlockIdx next_block_uid = this->transitionToBlock[next_transition_it.index];
				this->controlFlowGraph[next_block_uid].ins.insert(current_block.uid);
				this->controlFlowGraph[current_block.uid].outs.insert(next_block_uid);
				continue;
			}

			// Create new block
			BlockIdx new_block_uid = this->controlFlowGraph.size();
			bool is_reset_block = resetTransitions.contains(next_transition_it);
			chp::graph::controlFlowBlock new_block(
					new_block_uid,
					is_reset_block,
					{next_transition_it},
					{current_block.uid});
			this->controlFlowGraph.push_back(new_block);
			this->transitionToBlock[next_transition_it.index] = new_block_uid;
			this->controlFlowGraph[current_block.uid].outs.insert(new_block_uid);

			if (!visited.contains(next_transition_it)) {
				// If this new block is a reset block, we've discovered a repetition! Don't preserve path to reset
				if (is_reset_block) {
					current_path = {next_transition_it};

				} else {
					current_path.push_back(next_transition_it);
				}
				queue.push(current_path);
			}
		}
	}

	this->controlFlowGraphReady = true;
}

void graph::setUseDef(VarIdx varIdx, TransitionIdx transitionIdx, bool isDefinition) {
	string varName = this->netAt(varIdx);

	// New variable? Record its name
	if (this->useDefChains.find(varIdx) == this->useDefChains.end()) {
		this->useDefChains[varIdx].name = varName;
		this->useDefChains[varIdx].varIdx = varIdx;
	}

	if (isDefinition) {
		this->useDefChains[varIdx].defs.push_back(transitionIdx);
		//this->defs[varName].push_back(transitionIdx);
		clog << "DEF " << varName << " @ " << transitionIdx << endl;

	} else {
		this->useDefChains[varIdx].uses.push_back(transitionIdx);
		//this->uses[varName].push_back(transitionIdx);
		clog << "use " << varName << " @ " << transitionIdx << endl;
	}
}

void graph::extractUseDefFromExpression(TransitionIdx transitionIdx, const arithmetic::Expression &e, bool isDefinition) {
	if (isDefinition && e.top.isVar() && e.size() == 0) {
		this->setUseDef(e.top.index, transitionIdx, true);

	} else if (not e.isUndef()) { //if (e.isExpr()) {}
		for (const arithmetic::Operand &subExpr : e.exprIndex()) {

			// Iterate across all sub-expression leaves
			//TODO: introduce some simpler "walkLeaves"-esque helper method into Expression?
			const arithmetic::Operation &operation = *e.getExpr(subExpr.index);
			for (const arithmetic::Operand &operand : operation.operands) {
				if (operand.type == arithmetic::Operand::Type::VAR) {
					this->setUseDef(operand.index, transitionIdx, isDefinition);
				}
			}
		}
	} // else if (not e.isUndef()) {}
}

void graph::extractUseDefFromTransition(TransitionIdx transitionIdx) {
	const chp::transition &transition = this->transitions[transitionIdx];
	extractUseDefFromExpression(transitionIdx, transition.guard);

	const arithmetic::Choice &choice = transition.action;
	for (const auto &term : choice.terms) {
		for (const auto &action : term.actions) {
			extractUseDefFromExpression(transitionIdx, action.lvalue, true);

			//TODO: isDefinition parameter could be more robust ":=" assignment operand matching
			extractUseDefFromExpression(transitionIdx, action.rvalue);
		}
	}
}

void graph::computeUseDefChains() {
	cout << endl << "computing useDef chains." << endl;
	this->useDefChainsReady = false;

	for (TransitionIdx transition_idx = 0; transition_idx < this->transitions.size(); transition_idx++) {
		if (not this->transitions.is_valid(transition_idx)) { continue; }

		extractUseDefFromTransition(transition_idx);
	}

	//TODO(steven.kneiser): we're migrating off this->useDefChains to this->useDefs
	for (auto &[varIdx, chain] : this->useDefChains) {
		this->useDefs.push_back(chain);
	}

	this->useDefChainsReady = true;
	cout << "useDef chains computed." << endl;
}

//TODO(steven.kneiser): int -> VarIdx w/ numeric_limits<size_t>::max() for flag
pair<int, vector<TransitionIdx>> graph::getPreviousDefinitions(petri::iterator transitionIt, const vector<petri::iterator> &prevTransitionIts) {
	//TODO: rename prevTransitionIts param, now that I include current def
	//TODO: return all redefinitions of the same var so it can be enumerated (the LAST one in this vector is the reaching def!)
	//TODO: perf, cache this inside each transition, so there's a running set? Where should the analysis data be stored?

	// If there's no assignment, skip
	VarIdx varAssigned;

	//TODO: isAssignment() expression helper in chp::transition?
	bool isAssignment = false;
	const chp::transition &transition = this->transitions[transitionIt.index];
	const arithmetic::Choice &choice = transition.action;
	for (const arithmetic::Parallel &term : choice.terms) {
		for (const arithmetic::Action &action : term.actions) {
			if (not action.lvalue.isUndef()) {
				isAssignment = true;
				varAssigned = arithmetic::lvalueBase(action.lvalue, action.lvalue.top);
			}
			break;
		}
	}

	if (not isAssignment) {
		clog << "===> " << transitionIt.index << ": N/A" << endl;
		return pair<int, vector<TransitionIdx>>(-1, {});
	}

	// Filter down previous transitions to only assignments
	vector<TransitionIdx> prevDefs;
	for (auto prevTransitionIt = prevTransitionIts.begin(); prevTransitionIt != prevTransitionIts.end() - 1; prevTransitionIt++) {
		const chp::transition &prevTransition = this->transitions[prevTransitionIt->index];

		const arithmetic::Choice &prevChoice = prevTransition.action;
		for (const arithmetic::Parallel &term : prevChoice.terms) {
			for (const arithmetic::Action &action : term.actions) {
				if (action.lvalue.isUndef()) { continue; }

				// Filter down previous assignments to only assignments of the same var
				VarIdx prevVarAssigned = arithmetic::lvalueBase(action.lvalue, action.lvalue.top);
				if (varAssigned == prevVarAssigned) {
					prevDefs.push_back(prevTransitionIt->index);
				}
			}
		}
	}

	// The last transition in the return vector just got redefined, so "kill" it in the containing block
	clog << "===> " << transitionIt.index << ": ";
	std::copy(prevDefs.begin(), prevDefs.end(), ostream_iterator<TransitionIdx>(clog, ", "));
	clog << endl;
	return pair<int, vector<TransitionIdx>>(varAssigned, prevDefs);
}

void graph::increaseBlockVarToDSAIndex(BlockIdx blockIdx, VarIdx varIdx, VarDSAIdx dsaCountAfter) {
	//TODO: assumes this call & all params are a valid copy assignment. Safeguard if calling under new conditions

	// Grab old DSA count from postDefs
	chp::graph::controlFlowBlock &block = this->controlFlowGraph[blockIdx];
	string varName = this->vars[varIdx].name;
	VarDSAIdx dsaCountBefore = block.postDefs[varIdx];

	clog << " TODO: B[" << blockIdx << "] << " << varName << "_" << dsaCountAfter << " := " << varName << "_" << dsaCountBefore << endl;

	// First, clip block-exiting arc
	petri::iterator tailTransitionIt = block.transitions.back();
	//chp::transition &tailTransition = this->transitions[tailTransitionIt.index];

	//petri::iterator outboundArc = this->out(tailTransitionIt)[0];
	petri::iterator mergePlace = this->next(tailTransitionIt)[0];
	petri::iterator outboundArc = this->arc_between(tailTransitionIt, mergePlace);
	//TODO: verify arc returned is valid: if (outboundArc == petri::iterator()) { cerr << endl; }
	clog << " :: " << outboundArc << endl;


	// Insert new "v_new := v_old;" copy-assignment at the end of the block
	// TODO: update postDefs here in this function or above? ...probably above, doubly-so?
	arithmetic::Action newCopyAssignment;
	VarIdx preVarIdx = this->getEnumeratedVar(varIdx, dsaCountBefore);
	VarIdx postVarIdx = this->getEnumeratedVar(varIdx, dsaCountAfter);
	newCopyAssignment.lvalue = arithmetic::Expression::varOf(postVarIdx);
	newCopyAssignment.rvalue = arithmetic::Expression::varOf(preVarIdx);

	//TODO: ugh, I should re-use petri/graph.h::insert_after
	chp::transition newCopyAssignmentTransition(
			arithmetic::Expression::vdd(), arithmetic::Choice({{newCopyAssignment}}));
	TransitionIdx newTransitionIdx = this->transitions.insert(newCopyAssignmentTransition);

	// Insert copy-assignment after the block's last transition
	this->super::erase_arc(outboundArc);
	//this->super::mark_modified();  //TODO: required by petri? not used in insert_after...

	petri::iterator newCopyAssignmentTransitionIt(petri::transition::type, newTransitionIdx);
	this->super::connect(newCopyAssignmentTransitionIt, mergePlace);
	this->super::connect(tailTransitionIt, newCopyAssignmentTransitionIt);

	block.transitions.push_back(newCopyAssignmentTransitionIt);
	//block.last = newCopyAssignmentTransitionIt;
	block.gens[newTransitionIdx] = postVarIdx;
	block.postDefs[postVarIdx] = dsaCountAfter;
}

unordered_map<VarIdx, VarDSAIdx> graph::mergeDefinitionsBeforeBlock(BlockIdx blockIdx) {
	controlFlowBlock &block = this->controlFlowGraph[blockIdx];
	unordered_map<VarIdx, TransitionIdx> liveDefinitions;

	//unordered_map<size_t, unordered_map<size_t, size_t>> DSAIndexByVar;
	//unordered_map<size_t, size_t> from_block_idx, dsa_index;
	//unordered_map<size_t, size_t> var_idx, from_block_idx, dsa_index;
	//unordered_map<pair<size_t, size_t>, size_t> dsaIndicesByBlock;  // [var_idx, from_block_idx] => var_count (a.k.a. DSA Index)
	unordered_map<VarIdx, unordered_map<BlockIdx, VarDSAIdx>> dsaIndexPerBlockPerVar;  // var_idx -> [ from_block_idx -> var_count (a.k.a. DSA Index) ]

	// For each variable, aggregate DSA indices by input block source
	for (BlockIdx inBlockIdx : block.ins) {
		const controlFlowBlock &in_block = this->controlFlowGraph[inBlockIdx];

		for (pair<VarIdx, VarDSAIdx> inDef : in_block.postDefs) {
			dsaIndexPerBlockPerVar[inDef.first][inBlockIdx] = inDef.second;
		}
	}

	// Identify any post-selection variables that need additional copy assignments
	//TODO: block.preDefs merge/union(in_block.postDef for in_block in block.ins)
	for (const auto &[varIdx, dsaIndexPerBlock] : dsaIndexPerBlockPerVar) {
		//dsaIndexPerBlock[]
		// Start block with highest DSA index, in case of mismatch
		auto maxIt = std::max_element(dsaIndexPerBlock.begin(), dsaIndexPerBlock.end());
		BlockIdx deepestBlock = maxIt->first;
		TransitionIdx maxIndex = maxIt->second;
		liveDefinitions[varIdx] = maxIndex;

		string varName = this->vars[varIdx].name;
		clog << "? merge postDef? `" << varName << "` has DSA index `" << maxIndex << "` in deepest block `" << deepestBlock << "`" << endl;

		// Synchronize any input block postDefs that fell behind peers
		// by appending a copy-assignment to the new max
		// (e.g. one in-block didn't touch var x, but another redefined it twice)
		for (auto [blockIdx, dsaCount] : dsaIndexPerBlock) {
			if (dsaCount < maxIndex) {  //TODO: make method idempotent instead of checking? nah, too clever for now
				this->increaseBlockVarToDSAIndex(blockIdx, varIdx, maxIndex);
			}
		}
	}

	return liveDefinitions;
}


VarIdx graph::getEnumeratedVar(VarIdx varIdx, VarDSAIdx num, string delimiter) {
	string enumeratedName = this->vars[varIdx].name + delimiter + std::to_string(num);

	int enumeratedVarIdx = this->netIndex(enumeratedName);  //TODO(steven.kneiser): is implicit int -> size_t(VarIdx)  safe?
	if (enumeratedVarIdx == -1) {
		enumeratedVarIdx = this->vars.size();

		chp::variable enumeratedVar(enumeratedName);
		this->create(enumeratedVar);
		//TODO: consider multiple isochronic regions (e.g. populating .remote's)
	}

	return enumeratedVarIdx;
}


void graph::convertToDSA() {
	this->computeControlFlowGraph();

	// Populate pre- & post- reaching definitions
	// Use [forward] iterative "Worklist" algorithm for data flow (remarkably stable, block-order-invariant)
	queue<BlockIdx> worklist;
	set<BlockIdx> inWorklist;

	//TODO: empty should just be an early-out?
	if (not this->controlFlowGraph.empty()) {
		worklist.push(this->controlFlowGraph[0].uid);
		inWorklist.insert(this->controlFlowGraph[0].uid);
	}

	size_t workCount = 0;
	while (not worklist.empty()) {
		workCount++;

		clog << endl << ">> queue: ";
		queue<BlockIdx> worklistSnapshot(worklist);
		while (not worklistSnapshot.empty()) {
			clog << worklistSnapshot.front() << " ";
			worklistSnapshot.pop();
		}
		clog << endl;

		//TODO: remove watchdog once we fully support DSA repetition (including nested repetition)
		if (workCount > MAX_DSA_ITERATION) {
			cerr << "worklist watchdog Woof!" << endl;
			break;
		}

		BlockIdx blockIdx = worklist.front();
		worklist.pop();
		inWorklist.erase(blockIdx);
		controlFlowBlock &block = this->controlFlowGraph[blockIdx];

		clog << "-=-=-=-=-=-=-=-=-=-=-=-=- worklist count: " << workCount << " <><> block popped from queue: " << blockIdx << endl;

		// Populate pre- definitions based on in-blocks
		unordered_map<VarIdx, VarDSAIdx> liveDefinitions;
		if (not block.reset) {
			liveDefinitions = this->mergeDefinitionsBeforeBlock(blockIdx);
		}
		block.preDefs = liveDefinitions;

		// Reclassify each assignment in this block's transitions as (re)definitions
		//   & (re)enumerate vars with more up-to-date DSA-form indices
		//
		// The "worklist" algorithm iteratively refines
		//   so here we might have a new round of pre-Block defintions to propagate
		for (petri::iterator transitionIt : block.transitions) {
			TransitionIdx transitionIdx = transitionIt.index;

			// Replace "killed" defintion with new redefinition
			//TODO: what about Reset, where a first-def is technically a redef, but not yet recognized as redef?
			bool isRedefinition = false;
			if (block.kills.contains(transitionIdx)) {
				isRedefinition = true;
				VarIdx redefinedVar = block.gens[transitionIdx];
				//TransitionIdx prev_defining_transition = block.kills[transitionIdx];  //TODO: did we deprecate .kills? We need to search it down
				liveDefinitions[redefinedVar]++;

				// Append first-time definition
			} else if (block.gens.contains(transitionIdx)) {
				VarIdx definedVar = block.gens[transitionIdx];
				liveDefinitions[definedVar] = INITIAL_DSA_VALUE;
			}

			if (not liveDefinitions.empty()) {
				//std::for_each(liveDefinitions.begin(), liveDefinitions.end(), [this](auto &d){ clog << this->vars[d.first].name << "[" << d.second << "], "; }); clog << endl;

				// Enumerate current expression w/ DSA indices
				Mapping<size_t> postDefIndices(std::numeric_limits<size_t>::max(), true);
				for (auto [varIdx, varDSAIdx] : liveDefinitions) {
					VarIdx enumeratedVar = this->getEnumeratedVar(varIdx, varDSAIdx);
					postDefIndices.set(varIdx, enumeratedVar);
				}

				//TODO: give these 2 mappings EVEN MORE explicit naming with some sort of "rename" postfix
				Mapping<size_t> preDefIndices(postDefIndices);
				if (isRedefinition) {
					VarIdx redefinedVar = block.gens[transitionIdx];
					VarDSAIdx redefinedVarDSAIdx = liveDefinitions[redefinedVar];
					//VarIdx enumeratedVarAfter = this->getEnumeratedVar(redefinedVar, redefinedVarDSAIdx);
					VarIdx enumeratedVarBefore = this->getEnumeratedVar(redefinedVar, redefinedVarDSAIdx - 1);
					preDefIndices.set(redefinedVar, enumeratedVarBefore);
				}

				chp::transition &transition = this->transitions[transitionIdx];
				transition.guard.applyVars(preDefIndices);

				arithmetic::Choice &choice = transition.action;
				for (arithmetic::Parallel &term : choice.terms) {
					for (arithmetic::Action &action : term.actions) {
						action.lvalue.applyVars(postDefIndices);
						action.rvalue.applyVars(preDefIndices);
					}
				}

			} else {
				clog << "umpty-dumpty" << endl;
			}
			//clog << "L>> " << transitionIt.lvalue << endl;
			//clog << "R>> " << transitionIt.rvalue << endl;
		}

		// Update post-definitions, if local transformations came out differently this time
		// NOTE: if local transformations are deterministic, wouldn't this be equivalent to "were pre-Defs different from last time?"
		// NOTE: for theoretical reference, block.postPef := transfer(blockIdx, block.preDefs) // b.gen u (b.predef - b.kill)
		if (liveDefinitions != block.postDefs) {
			clog << " >> novel merge> ";
			std::for_each(liveDefinitions.begin(), liveDefinitions.end(), [this](auto &d){
					clog << this->vars[d.first].name << "[" << d.second << "], "; });
			clog << endl;
			block.postDefs = liveDefinitions;

			for (BlockIdx out : block.outs) {
				if (not inWorklist.contains(out)) {
					worklist.push(out);
					inWorklist.insert(out);
				}
			}
		}
	}


	// If any reset-defined vars get redefined in process loop,
	//   we need to match the reset var to the highest DSA index (a.k.a. last redefinition)

	//TODO: tentative algo:
	//  - if there are reset vars, find all reset blocks
	//  - if, when attempting to merge the reset block in's, we notice a reset var with a DSA index, overwrite that reset var AND merge it in via a copy-assignment
	//  - the copy-assignment makes it a loop-carried dependency!
	// 1) If there are any reset vars, visit all reset blocks
	if (not this->reset.empty()) {  // and (not this->reset[0].encodings.values.empty())

		VarIdx varIdx = 0;
		for (arithmetic::Value &varReset : this->reset[0].encodings.values) {
			if (varReset.state != arithmetic::Value::StateType::VALID) { continue; }

			string varName = this->vars[varIdx].name;
			int initVal = varReset.ival;
			cout << endl << "reset> " << varName << " := " << std::to_string(initVal) << endl;

			//TODO: perf: don't recompute! Would block.preDefs suffice here?
			unordered_map<VarIdx, VarDSAIdx> endLoopDefinitions;
			//VarIdx initialEnumeratedVar = std::numeric_limits<size_t>::max();
			VarIdx fullyEnumeratedVar = std::numeric_limits<size_t>::max();

			for (controlFlowBlock &block : this->controlFlowGraph) {
				//TODO: for perf, just cache the reset blocks from first-traversal
				if (not block.reset) { continue; }

				// 2) Study the merge of this reset block in's for redefined/DSA-enumerated vars who also defined in reset
				if (endLoopDefinitions.empty()) {
					endLoopDefinitions = this->mergeDefinitionsBeforeBlock(block.uid);  //TODO: remove premature optimization
				}
				if (endLoopDefinitions.contains(varIdx)) {

					// 3) we've found a reset-defined var who gets redefined in the loop (a.k.a. loop-carried dependency)
					//      which means we need to overwrite the reset var with the "final" loop-ending DSA index
					//NOTE: this is a careful idempotent guarding of what should not need be repeated upon subsequent reset blocks
					if (fullyEnumeratedVar == std::numeric_limits<size_t>::max()) {
						VarDSAIdx varDSAIdx = endLoopDefinitions[varIdx];
						//initialEnumeratedVar = this->getEnumeratedVar(varIdx, 0);  //TODO: doesn't this need to be added to vars too? Ah, THIS should be the reset! Overwrite reset with this enumeration &  ...
						fullyEnumeratedVar = this->getEnumeratedVar(varIdx, varDSAIdx);

						//TODO: AHA! Precisely BECAUSE I've overwritten this->vars!!! It's now free to cascade whenever I enumerate that changed var
						//Two ways to handle this: I probably SHOULD do this once before traversing ANY of these reset blocks, so that it's not a renumeration but a simple targeted replace ...OR I be extra mindful of the now changed value here blah blah I like the first approach
						//The other way involves the normalization step of denumerateVar(), which will be tempting to pollute everywhere

						//this->vars[varIdx] = this->vars[fullyEnumeratedVar];
						//TODO: do a more surgical replace instead of just overwriting the original. This leaves an ambiguous duplicate.
						//   chp::graph.vars might tolerate duplicates but we shouldn't pollute that namespace
						// ...new IDEA: just leave it as is for the var_0??? Why did we NEED to overwrite? Ah, but we already updated the encodings, silly!

						if (this->reset[0].encodings.values.size() < fullyEnumeratedVar) {
							this->reset[0].encodings.values.resize(fullyEnumeratedVar + 1);
						}

						arithmetic::Value newResetVal((int)initVal);
						this->reset[0].encodings.set(fullyEnumeratedVar, newResetVal, true);
						//varReset.state = arithmetic::Value::StateType::UNKNOWN; // oops, this doesn't overwrite original
						//TODO: confirm: set as UNKNOWN, UNSTABLE, or other? this feels right.
						//this->reset[0].encodings.values[varIdx].state = arithmetic::Value::StateType::UNKNOWN;
						//TODO: uh oh, what if multiple are re-using this one? well they should get set to VALID manually anyways
						this->reset[0].encodings.setU(varIdx);
					}
					cout << "     > pre-pend `" << this->vars[fullyEnumeratedVar].name
						<< " := " << initVal << "` to block #" << block.uid << endl;
					//TODO: perhaps instead I should reflect upon what SHOULD be repeated? just the insert?

					// 4) ...AND we must merge it into the loop via inserting a "x_0 := x_final" copy-assignment
					// Insert copy-assignment to internalize & synchronize DSA index for loop-begin DSA w/ loop-end
					arithmetic::Action loopCopyAssignment;
					VarIdx preLoopVarIdx = varIdx;
					VarIdx postLoopVarIdx = fullyEnumeratedVar;
					loopCopyAssignment.lvalue = arithmetic::Expression::varOf(preLoopVarIdx);
					loopCopyAssignment.rvalue = arithmetic::Expression::varOf(postLoopVarIdx);

					chp::transition loopCopyAssignmentTransition(
							arithmetic::Expression::vdd(), arithmetic::Choice({{loopCopyAssignment}}));
					petri::iterator loopsFirstTransitionIt = block.transitions.front();
					petri::iterator loopCopyAssignmentTransitionIt = this->super::insert_before(loopsFirstTransitionIt, loopCopyAssignmentTransition);

					block.transitions.insert(block.transitions.begin(), loopCopyAssignmentTransitionIt);
					TransitionIdx loopCopyAssignmentTransitionIdx = loopCopyAssignmentTransitionIt.index;
					block.gens[loopCopyAssignmentTransitionIdx] = preLoopVarIdx;  // should match the assignment lvalue
				}
			}
			varIdx++;
		}
	}

	clog << "DSA'd." << endl;
}

vector<VarIdx> getVarsFromExpression(const arithmetic::Expression &e) {
	if (e.isUndef()) { return {}; }
	if (e.top.isVar() && e.size() == 0) { return {e.top.index}; }

	vector<VarIdx> vars;
	for (const arithmetic::Operand &subExpr : e.exprIndex()) {

		// Iterate across all sub-expression leaves
		//TODO: introduce some simpler "walkLeaves"-esque helper method into Expression?
		const arithmetic::Operation &operation = *e.getExpr(subExpr.index);
		for (const arithmetic::Operand &operand : operation.operands) {
			if (operand.type == arithmetic::Operand::Type::VAR) {
				vars.push_back(operand.index);
			}
		}
	}

	return vars;
}

//TODO: Clean up this sloppy algorithmic solution. No need to fully-traverse again.
vector<VarIdx> findInputChannelsInExpression(const arithmetic::Expression &e) {
	if (e.isUndef() || (e.top.isVar() && e.size() == 0)) { return {}; }

	vector<VarIdx> inputChannels;
	for (const arithmetic::Operand &subExpr : e.exprIndex()) {
		// Iterate across all sub-expression leaves
		const arithmetic::Operation &operation = *e.getExpr(subExpr.index);
		for (const arithmetic::Operand &operand : operation.operands) {
			if (operand.cnst.sval == "recv") {
				inputChannels.push_back(e.sub.elems.elems[0].operands[0].index);
			}
		}
	}

	return inputChannels;
}

//TODO: Clean up this sloppy algorithmic solution. No need to fully-traverse again.
vector<VarIdx> findOutputChannelsInExpression(const arithmetic::Expression &e) {
	if (e.isUndef() || (e.top.isVar() && e.size() == 0)) { return {}; }

	vector<VarIdx> outputChannels;
	for (const arithmetic::Operand &subExpr : e.exprIndex()) {
		// Iterate across all sub-expression leaves
		const arithmetic::Operation &operation = *e.getExpr(subExpr.index);
		for (const arithmetic::Operand &operand : operation.operands) {
			if (operand.cnst.sval == "send") {
				outputChannels.push_back(e.sub.elems.elems[0].operands[0].index);
			}
		}
	}

	return outputChannels;
}

//string graph::to_string(const Expression &e) {
//	//TODO: hoist arithmetic/algorithm.cpp::to_string() in here with the call from Expression::to_string(debug=true) to get variable pretty-printing done nicely
//	std::ostringstream result;
//	if (debug) {
//		result << "top: " << top << endl;
//		vector<Operand> idx = ops.exprIndex();
//		for (auto i = idx.rbegin(); i != idx.rend(); i++) {
//			result << *ops.getExpr(i->index) << endl;
//		}
//
//	} else {
//		index_vector<string> strs;
//		for (ConstUpIterator i(ops, {top}); not i.done(); ++i) {
//			Operator func = Operation::operators[i->func];
//			std::ostringstream oss;
//			oss << "(";
//			oss << func.prefix;
//			for (int j = 0; j < (int)i->operands.size(); j++) {
//				if (i->operands[j].isExpr()) {
//					oss << strs[i->operands[j].index];
//				} else {
//					oss << i->operands[j];
//				}
//				if (j == 0 and not func.trigger.empty()) {
//					oss << func.trigger;
//				} else if (j == (int)i->operands.size()-1) {
//					oss << func.postfix;
//				} else {
//					oss << func.infix;
//				}
//			}
//
//			oss << ")";
//			strs.emplace_at(i->op().index, oss.str());
//		}
//
//		if (top.isExpr()) {
//			result << strs[top.index];
//		} else {
//			result << top;
//		}
//	}
//	return result.str();
//
//}


//petri::iterator graph::splitVarUses() { }
//petri::iterator graph::renameVarUseAferTransition(TransitionIdx insertionTransitionIdx, VarDSAIdx preVar, VarDSAIdx postVar) {
//
//	arithmetic::Action reassignment;
//	reassignment.lvalue = arithmetic::Expression::varOf(postVar);
//	reassignment.rvalue = arithmetic::Expression::varOf(preVar);
//
//	chp::transition renameTransition(
//			arithmetic::Expression::vdd(), arithmetic::Choice({{reassignment}}));
//	//TransitionIdx forkTransitionIdx = this->transitions.insert(forkAssignmentTransition);
//	//petri::iterator forkIt(petri::transition::type, forkTransitionIdx);
//
//	//TODO: extract this out (to be computed before when identifying transition, this way I can use it either right after the definiton or somewhere else)
//	useDefChain &varUseDefChain = this->useDefChains[preVar];
//	if (varUseDefChain.defs.empty()) { continue; }
//	TransitionIdx insertionTransitionIdx = varUseDefChain.defs[0];
//	//clog << this->transitions[insertionTransitionIdx] << endl;
//
//	petri::iterator insertionPoint(petri::transition::type, insertionTransitionIdx);
//	return this->super::insert_after(insertionPoint, renameTransition);
//}

//TransitionIdx graph::getVarDefTransition(VarIdx varIdx) {
//	useDefChain &chain = this->getVarUseDefChain(varIdx);
//	return chain.defs[0];
//}

//enum struct ChannelType {
//	NONE,
//	SEND,
//	RECV
//};
//
//struct VarRef {
//	VarIdx idx;
//	ChannelType type;
//	VarIdx channelIdx;
//};


bool isDisqualifyingItemInExpression(const Expression &e, const set<ProjectionItem> &items, bool debug) {
	vector<VarIdx> vars = getVarsFromExpression(e);
	vector<VarIdx> recvs = findInputChannelsInExpression(e);
	vector<VarIdx> sends = findOutputChannelsInExpression(e);
	set<VarIdx> recvsLookup(recvs.begin(), recvs.end());
	set<VarIdx> sendsLookup(sends.begin(), sends.end());

	set<ProjectionItem> exprItems;
	for (VarIdx var : vars) {

		if (sendsLookup.contains(var)) {
			exprItems.insert(ProjectionItem(var, true, true));

		} else if (recvsLookup.contains(var)) {
			exprItems.insert(ProjectionItem(var, true, false));

		} else {
			exprItems.insert(ProjectionItem(var));
		}
	}

	vector<ProjectionItem> sharedItems;
	std::ranges::set_difference(exprItems, items, std::back_inserter(sharedItems));
	if (debug) {
		clog << "found: ";
		std::for_each(sharedItems.begin(), sharedItems.end(), [](const ProjectionItem &item) { clog << item << ", "; });
		clog << "  ...in " << e;
		clog << endl;
	}
	return not sharedItems.empty();
}


void graph::remapVarInTransition(VarIdx from, VarIdx to, TransitionIdx transitionIdx, bool remapGuard, bool remapLHS, bool remapRHS) {
	if (not this->transitions.is_valid(transitionIdx)) { return; }
	chp::transition &transition = this->transitions[transitionIdx];

	//TODO(steven.kneiser): perf optimization: just preserve Mapping. Perhaps this should just be called "remapTransition()"
	Mapping<VarIdx> varSubstitution(std::numeric_limits<VarIdx>::max(), true);
	varSubstitution.set(from, to);

	if (remapGuard) { transition.guard.applyVars(varSubstitution); }
	arithmetic::Choice &choice = transition.action;
	for (arithmetic::Parallel &term : choice.terms) {
		for (arithmetic::Action &action : term.actions) {
			if (remapLHS) { action.lvalue.applyVars(varSubstitution); }
			if (remapRHS) { action.rvalue.applyVars(varSubstitution); }
			//TODO(steven.kneiser): support multiple uses of a var in an RHS expr that need CopyProcesses to remap each usage to a seperate branch?
		}
	}

	//TODO(steven.kneiser): perhaps HERE is where useDef should be updated?? lower-level and more atomic down here. Seems sensible@!!
	//  ...I'm already noticing this pattern of prepending every remap call with appropriate useDef pruning which is also just a sloppy way of more precisely retargeting
}


//NOTE(steven.kneiser): Currently, this rewrite is a minimal subsitution, only for remapping transitions that use one var to another
//  ...meh, I actually like the usability of the name "inAllTransitions" too.
//  Ideally, we make the atomic "remap-in-1-Transition()" more surgical/composable/extensible, that we encapsulate remappings with that helper.
//  I like the thought of separate InAllTransitions() & some sort of "InTransitionsByUseDef()"
//TODO(steven.kneiser): "substituteVarInTransitions()" a more intuitive name>
//TODO(steven.kneiser): remake as a light wrapper around some more atomic "remapVarInTransition()"
void graph::remapVarInEachTransition(VarIdx from, VarIdx to, bool rewriteDefinitions, bool rewriteUses) {  //bool rewriteGuard, bool rewriteLexprs, bool rewriteRexprs
	clog << "rewriting var." << endl;

	if (from >= this->vars.size()) { clog << "'from' var @" << from << " is out-of-range for this->vars: " << this->vars.size() << endl; return; }
	if (  to >= this->vars.size()) { clog << "  'to' var @" <<   to << " is out-of-range for this->vars: " << this->vars.size() << endl; return; }
	clog << "  from `" << this->vars[from].name << "` to `" << this->vars[to].name << "`" << endl;

	if (not this->useDefChainsReady) { this->computeUseDefChains(); }
	if (not this->useDefChains.contains(from)) { clog << "'from' var @" << from << " -> `" << this->vars[from].name << "` not found in this->useDefChains" << endl; return; }
	const useDefChain &useDefChain = this->useDefChains[from];
	//TODO(steven.kneiser): not necessarry, but might we ever want to explore the `to` useDefChain?

	// Substitute `to` for `from`
	set<TransitionIdx> transitionsToRewrite;
	//if (useDefChain.defs.empty()) { continue; }  // no definition when dependency is a recv'd/used input-channel
	if (rewriteDefinitions) { transitionsToRewrite.insert(useDefChain.defs.begin(), useDefChain.defs.end()); }
	//if (useDefChain.uses.empty()) { continue; }
	if (rewriteUses) { transitionsToRewrite.insert(useDefChain.uses.begin(), useDefChain.uses.end()); }
	if (transitionsToRewrite.empty()) { return; }

	Mapping<VarIdx> varSubstitution(std::numeric_limits<VarIdx>::max(), true);
	varSubstitution.set(from, to);

	//TODO(steven.kneiser): Update ProjectionSets too! Oh crap, will there be any only-partial updates due to def vs use rewrites?
	//set<ProjectionItem> &p = projectionSets[user];
	//p.erase(ProjectionItem(dependency));
	//p.insert(ProjectionItem(varUsageIdx));

	//TODO(steven.kneiser): is it safer to just walk all valid transitions for now? would be simpler to not rely on useDef,
	//  but perhaps would rely too strongly on DSA/single-use, when there are multi-uses that need to be forked w/ Copy Processes
	//  no problem, just be more mindful in the calls to this from Copy Process creation
	

	for (TransitionIdx transitionIdx : transitionsToRewrite) {
		if (not this->transitions.is_valid(transitionIdx)) { continue; }  //NOTE(steven.kneiser): redundant safety-check with helper, kept for future reference
		this->remapVarInTransition(from, to, transitionIdx);
	}

	clog << "rewrote var." << endl;
}


// Rewrite "g -> b := a" with a new Channel, c, as "g -> c.send(a)" & "g -> b := c.recv()" in parallel
//TODO(steven.kneiser): let's prefer seperation of concerns -> -> "g -> skip; (c.send(a) || b := c.recv())"
void graph::rewriteAssignmentAsChannel(TransitionIdx transitionIdx, VarIdx channelIdx) {
	clog << "rewriting assignment as channel." << endl;

	if (not this->transitions.is_valid(transitionIdx)) { return; }
	const chp::transition &transition = this->transitions[transitionIdx];
	//TODO(steven.kneiser): const?
	VarIdx varAssigned;

	//TODO(steven.kneiser): isAssignment() expression helper in chp::transition? ...it could return the VarIdx of the varAssigned! Return an optional?
	bool isAssignment = false;
	Expression rexpr;
	const arithmetic::Choice &choice = transition.action;
	for (const arithmetic::Parallel &term : choice.terms) {
		for (const arithmetic::Action &action : term.actions) {
			if (not action.lvalue.isUndef()) {
				isAssignment = true;
				varAssigned = arithmetic::lvalueBase(action.lvalue, action.lvalue.top);
				rexpr = action.rvalue;
			}
			break;  //TODO(steven.kneiser): why was this here, even in the `getPreviousDefinitions()` above that I pasted it from?
		}
	}
	if (not isAssignment) { return; }

	// Construct "c.send(a)" transition
	arithmetic::Action sendAction;
	arithmetic::Expression sendExpr = arithmetic::call(
			"send",
			{arithmetic::Expression::varOf(channelIdx), rexpr}
			);
	sendAction.lvalue = arithmetic::Expression::undef();
	sendAction.rvalue = sendExpr;
	chp::transition sendTransition(
			arithmetic::Expression::vdd(), arithmetic::Choice({{sendAction}}));
	clog << "++ " << channelIdx << endl
		<< "+> " << sendExpr << endl;

	// Construct "b = c.recv()"
	arithmetic::Action recvAction;
	arithmetic::Expression recvExpr = arithmetic::call(
			"recv",
			{arithmetic::Expression::varOf(channelIdx)}
			);
	recvAction.lvalue = arithmetic::Expression::varOf(varAssigned);
	recvAction.rvalue = recvExpr;
	chp::transition recvTransition(
			arithmetic::Expression::vdd(), arithmetic::Choice({{recvAction}}));
	clog << "<+ " << recvExpr << endl;

	// Replace this transition with new children
	petri::iterator transitionIt(petri::transition::type, transitionIdx);

	//TODO(steven.kneiser): do we need to preserve guard too? guard both new expressions?
	//      ...for simplicity we could introduce a "g -> skip" beforehand, instead of distributing it
	// ahhh, we need to crawl for all, not just isAssignment prop (e.g. rval needed for

	// Construct "g -> skip" if guard, g, exists
	petri::iterator sendTransitionIt;
	if (not areSame(transition.guard, arithmetic::Expression::vdd())) {	 //TODO(steven.kneiser): what's the most idiomatic "if guard isn't empty"?
		chp::transition guardTransition(transition.guard);
		petri::iterator guardTransitionIt = this->super::insert_after(transitionIt, guardTransition);
		//TODO(steven.kneiser): What about all the inTransitions+Places to transitionIt? Verify that pinch works precisely
		sendTransitionIt = this->super::insert_after(guardTransitionIt, sendTransition);

	} else {
		//TODO(steven.kneiser): What about all the inTransitions+Places to transitionIt? Verify that pinch works precisely
		sendTransitionIt = this->super::insert_after(transitionIt, sendTransition);
	}

	////TODO(steven.kneiser): shouldn't insert_alongside() eloquently handle all these already?
	//for (petri::iterator outPlace : this->next(sendTransitionIt)) {
	//	for (petri::iterator to : this->next(outPlace)) {
	//		petri::iterator recvTransitionIt = this->super::insert_alongside(transitionIt, to, recvTransition);
	//	}
	//}

	petri::iterator dummyTailIt = this->super::insert_after(sendTransitionIt, chp::transition());
	petri::iterator recvTransitionIt = this->super::insert_alongside(transitionIt, dummyTailIt, recvTransition);
	//TODO: this->pinch(dummyTailIt);

	
	//TODO(steven.kneiser): Finally, update the relevant useDef's to preserve their correctness!
	//  We can update this properly with new info or at least leave UseDef's like a todolist for replacement
	//    ...let's start by just pruning
	//this->getVarDefTransition();
	vector<VarIdx> sendVars = getVarsFromExpression(sendExpr);
	for (VarIdx sendVar : sendVars) {

		// Substitute new sendTransition for previous assignment
		//TODO(steven.kneiser): perf optimization: modify useDefChain in-place
		vector<VarIdx> &varUses = this->useDefChains[sendVar].uses;
		auto it = std::find(varUses.begin(), varUses.end(), transitionIt.index);
		if (it != varUses.end()) { varUses.erase(it); }
		varUses.push_back(sendTransitionIt.index);
		this->useDefChains[sendVar].uses = varUses;
	}

	this->pinch(transitionIt);  // Remove the original assignment

	clog << "rewrote assignment as channel." << endl;
}


// Scrape Transition for either var defined or channel being sent/output on
//TODO(steven.kneiser): find more readable, future-proof name than "USING var" ...as in the Def var in a use-def chain (because it needs to abstract the union of these two types
bool graph::isTargetVarOfTransition(VarIdx targetVar, TransitionIdx transitionIdx) {
	if (not this->transitions.is_valid(transitionIdx)) { return false; }
	const chp::transition &transition = this->transitions[transitionIdx];
	const arithmetic::Choice &choice = transition.action;

	// Does this transition define <user> var or send on <user> channel?
	const arithmetic::Expression targetVarAsExpr = Expression::varOf(targetVar);
	for (const arithmetic::Parallel &term : choice.terms) {
		for (const arithmetic::Action &action : term.actions) {
			if (areSame(action.lvalue, targetVarAsExpr)) { return true; }  //TODO(steven.kneiser): perf optimization: is there a quicker, more precise predicate?

			vector<VarIdx> outputChannels = findOutputChannelsInExpression(action.rvalue);
			if (std::find(outputChannels.begin(), outputChannels.end(), targetVar) != outputChannels.end()) {
				return true;
			}
		}
	}
	return false;
}


//TODO(steven.kneiser): impl logic for creating a CopyProc. This is just a skeleton.
UseDefIdx graph::getCopyProcess(VarIdx varIdx) {
	auto useDefMatchIt = std::find_if(this->useDefs.begin(), this->useDefs.end(),
			[varIdx](const useDefChain &chain) {
				return chain.varIdx == varIdx;
			});

	if (useDefMatchIt != this->useDefs.end()) {
		UseDefIdx useDefIdx = useDefMatchIt - this->useDefs.begin();
		return useDefIdx;
	}

	// If none exist, careate new Copy Process
	useDefChain useDef;
	//UseDefChain useDef = this->createCopyProcess(varIdx):
	this->useDefs.push_back(useDef);

	return this->useDefs.size() - 1;
}


//TODO(steven.kneiser): Why not just pass VarIdx ...since you're going to index into useDefs anyways ...no need to pass set?
//TODO(steven.kneiser): Done, now re-introduce usageRename remappings for DSA indexing
void graph::rewriteAssignmentAsCopyProcess(TransitionIdx transitionIdx, const set<VarIdx> &uses) {
	clog << "rewriting assignment as Copy Process." << endl;

	if (not this->transitions.is_valid(transitionIdx)) { return; }
	const chp::transition &transition = this->transitions[transitionIdx];

	// Extract terms of assignment
	//TODO(steven.kneiser): this is a perfect case-study for future chp::transition helpers!
	VarIdx varAssigned;
	bool isAssignment = false;
	//TODO(steven.kneiser): isAssignment() expression helper in chp::transition? ...it could return the VarIdx of the varAssigned! Return an optional?
	Expression lexpr, rexpr;
	const arithmetic::Choice &choice = transition.action;
	for (const arithmetic::Parallel &term : choice.terms) {
		for (const arithmetic::Action &action : term.actions) {
			if (not action.lvalue.isUndef()) {
				isAssignment = true;
				varAssigned = arithmetic::lvalueBase(action.lvalue, action.lvalue.top);

				lexpr = action.lvalue;
				rexpr = action.rvalue;
				////vector<VarIdx> leftVars = getVarsFromExpression(action.lvalue);

				////TODO: is_definition parameter could be more robust ":=" assignment operand matching
				////vector<VarIdx> rightVars = getVarsFromExpression(action.rvalue);
				////vector<VarIdx> inputChannelsUsed = findInputChannelsInExpression(action.rvalue);
				////vector<VarIdx> outputChannelsUsed = findOutputChannelsInExpression(action.rvalue);  //TODO: are you confident in the ordering out of this algorithm?
			}
			break;  //TODO(steven.kneiser): why was this here, even in the `getPreviousDefinitions()` above that I pasted it from? ...probably an old simplifying assumption that it'll be 1 transition
		}
	}
	if (not isAssignment) { return; }  //TODO(steven.kneiser): worth a debug message?

	if (not this->useDefChains.contains(varAssigned)) { clog << "useDefChain not found for varIdx " << varAssigned << ". Unable to rewrite base fork as Channel communication" << endl; return; } //continue;  //TODO(steven.kneiser): should never happen if this is detected as multi-use vairable,
	//   ...unless the generation of other CopyProcesses collide here (possible on recursive rewrites?) This doesn't seem to fail gracefully, just silently
	useDefChain &varAssignedUseDefChain = this->useDefChains[varAssigned];
	//TODO(steven.kneiser): update useDefChain by writing this back at the end of this func (or does this reference hold?)


		
		if (varAssignedUseDefChain.defs.empty()) { clog << "ERROR: no definition for var `" << varAssignedUseDefChain.name << "` found. Skipping all branch appending." << endl; return; }
		TransitionIdx defTransitionIdx = varAssignedUseDefChain.defs[0];

		if (not varAssignedUseDefChain.hasCopyProcess()) { clog << "ERROR: no Copy Process found for var `" << varAssignedUseDefChain.name << "`. Skipping all branch appending." << endl; return; }
		petri::iterator forkIt(petri::transition::type, varAssignedUseDefChain.copyProcess);



	// Create forking assignments of branches
	//TODO(steven.kneiser): Clean up the awkward "--0" tail workaround (tempting because getEnumeratedVar helper always trails int, which can awkwardly mix with DSA-enumerated vars from userName)
	VarIdx varForkIdx = this->getEnumeratedVar(varAssigned, 0, "~NEO_fork--");

	//arithmetic::Action forkAssignment(Expression::varOf(varForkIdx), rexpr);
	////TODO(steven.kneiser): don't forget to include guard, g, if there is one! (repeat the impl in rewriteAssignmentAsChannel() )
	//chp::transition forkTransition(Expression::vdd(), arithmetic::Choice({{forkAssignment}}));
	//petri::iterator forkIt = this->super::insert_after(transitionIt, forkTransition);


	petri::iterator dummyHeadIt = this->super::insert_after(forkIt, chp::transition());
	petri::iterator dummyTailIt = this->super::insert_after(dummyHeadIt, chp::transition());


	//varAssignedUseDefChain.copyProcess = varForkIdx;  //TODO(steven.kneiser): absolutely disgusting, egregious type violation to explore one development idea. It just needs to be ANYTHING to help us dedup


	vector<petri::iterator> branchIts;
	for (VarIdx use : uses) {  //TODO(steven.kneiser): better named as "user : users"???
		if (use >= this->vars.size()) { continue; }
		string userName = this->vars[use].name;

		//TODO(steven.kneiser): Clean up the awkward "--0" tail workaround (tempting because getEnumeratedVar helper always trails int, which can awkwardly mix with DSA-enumerated vars from userName)
		VarIdx varBranchIdx = this->getEnumeratedVar(varAssigned, 0, "~branch~" + userName + "--");
		arithmetic::Action branchAssignment(Expression::varOf(varBranchIdx), Expression::varOf(varForkIdx));
		chp::transition branchTransition(Expression::vdd(), arithmetic::Choice({{branchAssignment}}));

		petri::iterator branchIt = this->super::insert_alongside(forkIt, dummyTailIt, branchTransition);
		branchIts.push_back(branchIt);


		// Find this user's transitionIdx (could be better merged with "uses" parameter of this function?)
		for (TransitionIdx usingTransitionIdx : varAssignedUseDefChain.uses) {
			if (not this->transitions.is_valid(usingTransitionIdx)) { continue; }  // redundant for future-proof safety
			if (not this->isTargetVarOfTransition(use, usingTransitionIdx)) { continue; }

			//TODO(steven.kneiser): wait, wait if this skips (e.g. pythonic `for-else` or `nobreak` clause) and we never find anything ...but proceed to remapVar anyway? what should be the default case?


			this->remapVarInTransition(varAssigned, varBranchIdx, usingTransitionIdx);

			//TODO(steven.kneiser): Finally, update the relevant useDef's to preserve their correctness!
			//  We can update this properly with new info or at least leave UseDef's like a todolist for replacement

			// Substitute new sendTransition for previous "using" Transition, assigning to this user or sending on this user's channel
			//TODO(steven.kneiser): perf optimization: modify useDefChain in-place
			if (this->useDefChains.contains(varAssigned)) {
				vector<VarIdx> &varUses = this->useDefChains[varAssigned].uses;
				//NOTE(steven.kneiser): VERY dangerous to mutate an object we're iterating over,
				//   ...but this is okay because we're breaking the iteration after this match

				auto it = std::find(varUses.begin(), varUses.end(), usingTransitionIdx);
				if (it != varUses.end()) { varUses.erase(it); }
				////varUses.push_back(branchIt.index);  //TODO(steven.kneiser): ummmmm this only makes sense on channel REWRITE, not here where we're merely replacing this with a branch
				////   ...the TRANSITION isn't new like a channel-rewrite, there's a new def but we have to do a channel rewrite here to justify
				////   (e.g. the ideal would be `b_1~branch~<user>` needs to point to this new branch iter as the the def iter)

				//TODO(steven.kneiser): oh crap, now that we predefine this list WE'RE MODIFYING IT WHILE ITERATING OVER IT! see varAssignedUseDefChain ....ugh
				// IDEA: okay, let's batch all these "toOverwrite" updates to useDefs for after the branches are made ...perhaps even after the fork is done?
				// ahh, this can be safe if we gaurantee we break upon any mutation?
				//TODO(steven.kneiser): pushd pushd pushd RETVRN HERE
				//  ... aha, the right answer is to transform this from a range-based for-loop to an iterator-based one, and pass that iterator to erase

				////this->useDefChains[varAssigned].uses = varUses;   // unneeded since we modified reference
			}

			break;  // if isTargetVarOfTransition(), then we've found what we've already processed our match

			//TODO(steven.kneiser): oof yikes, I'm preserving the useDefChain of varAssigned, "b_1", here
			//   ...instead of substituting for varForkIdx, "b_1~branch~<user>--0", which it now SHOULD be (specifically in the INDEX of the useDefChains)

			//TODO(steven.kneiser): should useDefChains really be a .uses -> map[varIdx-of-user] -> transitionIdx-of-usage? or vice versa? Should be a Mapping<size_t> (more precisely, VarIdx <-> TransitionIdx>)
		}


		VarIdx branchChannelIdx = this->getEnumeratedVar(varAssigned, 0, "~BRANCH_CHAN~" + userName + "--");  //TODO(steven.kneiser): get full proper name
		this->rewriteAssignmentAsChannel(branchIt.index, branchChannelIdx);
	}




	// Rewrite assignments as channels, isolating this Copy Process for Projection in Process Decomposition
	//TODO(steven.kneiser): Like everywhere else, clean up the awkward "--0" tail workaround (tempting because getEnumeratedVar helper always trails int, which can awkwardly mix with DSA-enumerated vars from userName)
	VarIdx forkChannelIdx = this->getEnumeratedVar(varAssigned, 0, "~NEO_FORK_CHAN--");
	//this->rewriteAssignmentAsChannel(forkIt.index, forkChannelIdx);  //TODO(steven.kneiser): silenced since we now wait until after ALL Copy Process forks & branches are done to channel-substitute them at the very end


	// Remove the original assignment
	petri::iterator transitionIt(petri::transition::type, transitionIdx);
	this->pinch(transitionIt);

	clog << "rewrote assignment as Copy Process." << endl;
}


//TODO: find what's shared between these two helpers (direct vs copy) & can be wrapped (e.g. assignment->petri? rewriteAssignmentAsChannel() ???)
//TODO: verfiy that function name functionally matches whatever eventual type signature
void graph::rewriteEachSingleUseVarAsDirectChannel(
		const unordered_map<VarIdx, set<VarIdx>> &invertedDependencySets,
		unordered_map<VarIdx, set<ProjectionItem>> &projectionSets) {
	clog << "rewriting each single-use var as a direct channel." << endl;

	for (const auto &[dependency, users] : invertedDependencySets) {
		clog << endl << "\\/\\/\\/\\/\\/\\/\\/\\/ (single-dep) "  // R7->8 ...why did this feel that way?
			<< this->vars[dependency].name << endl;

		size_t useCount = users.size();  //TODO: OOPS! Should still be useDefCounter, but I just need useDef counter instead of keeping count, to ALSO get a trace back to WHICH depndencySet dependency it is used under, which we need to ultimately trace down WHICH transition is it USED in that needs to be remapped with a copy branch
		if (useCount != 1) { continue; }
		VarIdx user = *users.begin();

		useDefChain &dependencyUseDefChain = this->useDefChains[dependency];
		if (dependencyUseDefChain.defs.empty()) { continue; }  // no definition when dependency is a recv'd/used input-channel

		// Substitute "x_usage_n" for x in usage
		VarIdx varUsageIdx = this->getEnumeratedVar(dependency, 0, "~lone--");
		this->remapVarInEachTransition(dependency, varUsageIdx);  //false, true
		//TODO(steven.kneiser): there exists a powerful parallel here to the need to retrieve the transition FROM the var's target/def-er/user that we do for Copy Processes
		// Ideally, we should just be doing a singular surgical remapping in "this" one transition, not sloppily innefficiently search ALL transitions

		// Rewrite "x = ..." as "CHAN.send(x)" & "x_usage_n = CHAN.recv()" in parallel
		TransitionIdx defTransitionIdx = dependencyUseDefChain.defs[0];
		petri::iterator defTransitionIt(petri::transition::type, defTransitionIdx);
		VarIdx channelIdx = this->getEnumeratedVar(dependency, 0, "~LONE_CHAN--");
		this->rewriteAssignmentAsChannel(defTransitionIt.index, channelIdx);

		projectionSets[dependency].insert(ProjectionItem(channelIdx, true, true));
		projectionSets[user].insert(ProjectionItem(channelIdx, true, false));
		//TODO(steven.kneiser): verify nothing needs to be REMOVED from Projection Sets after rewrite
	}

	clog << "rewrote each single-use var as a direct channel." << endl;
}


void graph::rewriteEachMultiUseVarAsCopyProcess(
		const unordered_map<VarIdx, set<VarIdx>> &invertedDependencySets,
		unordered_map<VarIdx, set<ProjectionItem>> &projectionSets) {
	clog << "rewriting each multi-use var as a Copy Process." << endl;

	for (const auto &[dependency, users] : invertedDependencySets) {
		size_t useCount = users.size();
		if (useCount < 2) { continue; }
		clog << endl << "\\/\\/\\/\\/\\/ (multi-dep) "
			<< this->vars[dependency].name << ": " << useCount
			<< ((useCount > 1) ? "!" : "") << endl;

		////// Insert new "x_fork := x;" copy-assignment immediately after x assignment
		////// This serves as the base of a fork, splitting/parallelizing out to every use/reference
		//VarIdx varForkIdx = this->getEnumeratedVar(dependency, 0, "~fork--");
		//projectionSets[varForkIdx].insert(ProjectionItem(varForkIdx));

		//////TODO: is this much recalculation needed to retrace definition?
		//useDefChain &dependencyUseDefChain = this->useDefChains[dependency];
		//if (dependencyUseDefChain.defs.empty()) { continue; }

		//TransitionIdx defTransitionIdx = dependencyUseDefChain.defs[0];
		//petri::iterator defTransitionIt(petri::transition::type, defTransitionIdx);
		////TODO: append useDef after inserting new assignment?  nah, because
		////   because decomp/synthesis is a destructive operation (if rerun, we would recalculate the CFG & use-defs anyways

		VarIdx varForkIdx = this->getEnumeratedVar(dependency, 0, "~NEO_fork--");

		UseDefIdx copyProcessChainIdx = dependency;  //TODO(steven.kneiser): eww, egregious type violation until this->useDefs migration
		if (not this->useDefChains.contains(copyProcessChainIdx)) { clog << "useDefChain not found at index " << copyProcessChainIdx << ". Unable to rewrite base fork as Channel communication" << endl; continue; }  //TODO(steven.kneiser): migrate to newer this->useDefs
		const useDefChain &chain = this->useDefChains[copyProcessChainIdx];
		if (chain.defs.empty()) { clog << "ERROR: no definition found in useDefChain for var `" << chain.name << "`. Skipping all branch additions." << endl; continue; }
		TransitionIdx defTransitionIdx = chain.defs[0];

		// Insert "this.guard -> CHAN.send(x); x_fork := CHAN.recv()"
		VarIdx forkChannelIdx = this->getEnumeratedVar(dependency, 0, "~NEO_FORK_CHAN--");
		clog << "++ " << forkChannelIdx << endl;
		projectionSets[dependency].insert(ProjectionItem(forkChannelIdx, true, true));
		projectionSets[varForkIdx].insert(ProjectionItem(forkChannelIdx, true, false));


		//TODO(steven.kneiser): restudy the bound of this function ...should this projectionSets update be done internally? or perhaps more logic should be pulled out?
		//   i.e. does this funciton do too much? too little?
		// at least the users should be shifted to either a single var or the entire useDef, or get re-retrieved internally for better decoupling?
		//  ...I don't like how small/hollowed out this outer func has become ...perhaps it should be left as a well-commented section of IT's parent: rewriteAssignmentsAsChannels()
		this->rewriteAssignmentAsCopyProcess(defTransitionIdx, users);  //forkChannelIdx);
	}

	clog << "rewrote each multi-use var as a Copy Process." << endl;
}


//TODO(steven.kneiser): verify that channel rewrites to branches only happen AFTER predicates-bodies/segments/bounds have been duplicated or decomposed
void graph::rewriteEachGuardVarUsedInMultiDefinitionSelectionsAsCopyProcess(
		const unordered_map<VarIdx, set<VarIdx>> &invertedDependencySets,
		unordered_map<VarIdx, set<ProjectionItem>> &projectionSets) {
	clog << "rewriting each guard var used in multi-definition selections as a Copy Process." << endl;

	// Identify selections w/ multi-definition sequences,
	// where guards/predicates need to fork into branching copies
	// (in case we split between those defintions in projection)
	for (const controlFlowBlock &block : this->controlFlowGraph) {
		if (block.outs.size() < 2) { continue; }  // Ignore non-selections

		// Filter for only outgoing CONDITIONAL-splits, not parallel-splits
		//   (e.g. single outgoing split-place, not if this lastTransition is a split-transition)
		petri::iterator lastTransition = block.transitions.back();
		if (this->next(lastTransition).size() > 1) { continue; }
		//TODO: verify: this->next or this->out?
		//TODO: verify this wasn't too harsh a filter. what if a heterogenous split whereby there's a proper selection but in parallel with something else?
		//  ...there could be one out-place that's a conditional split with multiple branches next to another place leading to another straightline program
		//TODO(steven.kneiser): agreed, make this more robust to weird "(if x elif y) or (w-) and (z+)" etc

		set<VarIdx> allOutGuardVars;
		vector<Expression> outGuards;
		unordered_map<TransitionIdx, VarIdx> outGens;

		for (BlockIdx outBlockIdx : block.outs) {
			const controlFlowBlock &outBlock = this->controlFlowGraph[outBlockIdx];
			outGens.insert(outBlock.gens.begin(), outBlock.gens.end());

			petri::iterator outTransitionIt = outBlock.transitions.front();
			const chp::transition &outTransition = this->transitions[outTransitionIt.index];
			//TODO(steven.kneiser): outTransition.is_valid() revalidation check even needed?
			Expression outGuard = outTransition.guard;
			outGuards.push_back(outGuard);

			//TODO(steven.kneiser): only scrape outGuardVars from initial transition, not subsequents [at least not yet]
			//   ...this actually seems better: we should handle simple awwaits differently from selection predicates
			vector<VarIdx> outGuardVars = getVarsFromExpression(outGuard);
			allOutGuardVars.insert(outGuardVars.begin(), outGuardVars.end());
		}

		if (outGens.size() < 2) { continue; }  // Ignore selections not enclosing multiple definitions

		// Great, we've found a multi-def selection [with a guard to split]! 
		//TODO(steven.kneiser): encapsulate in a "this->splitGuard(guardVar, vector<TransitionIdx> branchDefinitions, ...)"??

		// Just for debugging
		clog << endl << " # # # # # # # # (block #" << block.uid << ")" << endl;
		clog << "* outGens)" << endl;
		for (auto &[transitionIdx, varIdx] : outGens) { clog << "  - " << this->vars[varIdx].name << " @ T" << transitionIdx << endl; }
		clog << "* allOutGuardVars)" << endl;
		for (VarIdx varIdx : allOutGuardVars) { clog << "  - " << this->vars[varIdx].name << endl; }
		clog << endl;


		//  now 1) each of these outGuards now need a copy process
		for (VarIdx guardVarIdx : allOutGuardVars) {
			if (guardVarIdx >= this->vars.size()) { clog << "ERROR: var at index " << guardVarIdx << "is not in this->vars. Skipping." << endl; continue; }
			string guardVarName = this->vars[guardVarIdx].name;
			cout << endl << ">/< guardVar to split: " << guardVarName << endl;

			//// First, detect if guardVar already has a copyProcess we can build atop (any chance it's already been done?)
			//// If not detected, create a new CopyProcess
			//NOTE(steven.kneiser): no longer necessary w/ batched CopyProcs beforehand ...perhaps even if we migrate to createVarDefBranch API
			// ^ comment ready to be pruned
			//NOTE(steven.kneiser): Copy Process fork for guardVar could be made here, but we already detected the need for it & batch created them up-front

			// Per each definition (per guard),  ...
			size_t guardSplitCount = 0;
			for (auto &[defTransitionIdx, defVarIdx] : outGens) {

				// Per definition inside branch, create branch of guardVar (the literal split)

				//NOTE(steven.kneiser): the canonical defVar likely won't match the local variation (e.g. DSA made `d` -> `d_1` and other Decomp might make `d_1` -> `d_1~lone--0` etc etc)
				//TODO(steven.kneiser): source the more relevant DSA++ variant of defVarIdx from defTransitonIdx
				VarIdx localDefVarIdx;
				if (not this->transitions.is_valid(defTransitionIdx)) { cerr << "ERROR: defTransitionIdx @ T" << defTransitionIdx << " isn't valid. Skipping ahead." << endl; continue; }
				chp::transition &defTransition = this->transitions[defTransitionIdx];
				arithmetic::Choice &choice = defTransition.action;
				for (arithmetic::Parallel &term : choice.terms) {
					for (arithmetic::Action &action : term.actions) {
						if (not action.lvalue.isUndef()) {
							localDefVarIdx = action.lvalue.top.index;  //TODO(steven.kneiser): unsafe: don't assume lvalue is the var, might even just be a light container expression containing the var?
						}
					}
					//break;?
				}

				if (localDefVarIdx >= this->vars.size()) { clog << "ERROR: var at index " << localDefVarIdx << "is not in this->vars. Skipping." << endl; continue; }
				string localDefVarName = this->vars[localDefVarIdx].name;

				//TODO(steven.kneiser): ugh, swap the name orderings (& idx vs name orientation)
				VarIdx newBranchVarIdx = this->getEnumeratedVar(guardVarIdx, guardSplitCount, "~gsplit-" + localDefVarName + "~");  //TODO: study other naming examples
				cout << " ==>  " << this->vars[newBranchVarIdx].name << "  @ " << newBranchVarIdx << endl;

				//TransitionIdx branchTransitionIdx = this->createVarDefBranch(localDefVarIdx, newBranchVarIdx, defTransitionIdx);
				TransitionIdx branchTransitionIdx = this->createVarDefBranch(guardVarIdx, newBranchVarIdx); //, defTransitionIdx);
				cout << "   >/<  T" << branchTransitionIdx << endl;
				//TODO(steven.kneiser): pushd RETVRN HERE to use this new guardVarBranch to name to determine new duplicates of the branch defs (appropriately renamed)

				//// 3) rename target Transition w/ new branch var
				//if (not this->transitions.is_valid(targetTransitionIdx)) { cerr << "ERROR: targetTransitionIdx @ T" << targetTransitionIdx << " isn't valid. No graceful failure." << endl; return 0; }
				//chp::transition &targetTransition = this->transitions[targetTransitionIdx];

				//Mapping<VarIdx> varRename(std::numeric_limits<VarIdx>::max(), true);
				//varRename.set(sourceVarIdx, branchVarIdx);

				//arithmetic::Choice &choice = targetTransition.action;
				//targetTransition.guard.applyVars(varRename);
				//for (arithmetic::Parallel &term : choice.terms) {
				//	for (arithmetic::Action &action : term.actions) {
				//		action.lvalue.applyVars(varRename);
				//		action.rvalue.applyVars(varRename);
				//	}
				//	//break;?
				//}

				petri::iterator defTransitionIt(petri::transition::type, defTransitionIdx);
				this->pinch(defTransitionIt);


				//TODO(steven.kneiser): then 4) document this properly in ProjectionSets ...should these be plumbed into createVarDefBranch() as default behavior?
				//   Meh, let's start here & integrate it if we later discover the desire for ALWAYS. This feels like something that should be part of analysis:useDefs, NOT presumed to always be related to Projection (i.e. that helper should probably stay tight & focused instead of assuming it's exclusively useful for Projection)
				guardSplitCount++;
			}
		}

		//TODO(steven.kneiser): now that we've identified & set the stage for projection, save these steps for when the time is right
		//  then 2) insert copies of this entire block??? ...or save for detection later in projection (with some explicit "splitGuards" hook)
		//  also 3) properly substitute/replace/rewrite rule with respective new guardVar copy
		//  then 4) document this properly in ProjectionSets
	}

	clog << "rewrote each guard var used in multi-definition selections as a Copy Process." << endl;
}


//TODO(steven.kneiser): save this sort of puzzle piece for right after the this->useDefChains -> this->useDefs migration
//////TODO(steven.kneiser): get idx vs ref? ref is a much cleaner API, but this is more lightweight & matches local style.
//////   ...I don't like the verbosity & repeated bounds-checking the idx imposes though, even though we'd be doing "validity"-esque checks anyways even if it wasn't literal bounds-checking
////useDefChain& graph::getUseDefByVarIdx(VarIdx targetVarIdx) {
////	//TODO(steven.kneiser): perf optimization: use better structures for better indexing & much faster look-up
////	auto it = std::find_if(this->useDefChains.begin(), this->useDefChains.end(),
////			[targetVarIdx](useDefChain &chain) {
////				return chain.varIdx == targetVarIdx;
////			});
////
////	if (it != this->useDefChains.end()) {
////		return *it;
////	} else {
////		return useDefChain();  //TODO(steven.kneiser): some "null" case is needed here
////	}
////}


//TODO(steven.kneiser): this will likely require some documenting or detection of previously made CopyProcesses
//void graph::splitGuard() {}
//TransitionIdx duplicateBranch(petri::segment seg) {}  // or (petri::iterator from, petri::iterator to) {}
TransitionIdx graph::createVarDefFork(VarIdx sourceVarIdx) {
	//IDEA(steven.kneiser): we can get clever and re-use rewriteAssignmentAsDirectChannel() but with a non-channel var
	return 0;  //TODO(steven.kneiser): impl, then use for createCopyProcessForksForMultiUseVars() ...migrate impl from there
}


TransitionIdx graph::createVarDefBranch(VarIdx sourceVarIdx, VarIdx branchVarIdx) { //, TransitionIdx targetTransitionIdx
	////clog << "create var def branch: <TO DO>" << endl;
	// 0) if no var fork & this is the first time, just leave (or make?) a direct channel

	//TODO(steven.kneiser): this stuff is great, but should get moreso done in future var/useDef canonicalization work
	// ... IDEALLY, this should just take any input variable (not just THE forkVar), get canonicalized, then lookup the forkVar

	////TODO(steven.kneiser): sourceVarIdx should be inferred from branchVarIdx singletons when useDefChains are properly complete, pointing everyone to their canoncial sourceVar
	//// 1) if var fork doesn't exist as a base for branching, create one (& doument in useDefChain)	
	////TODO(steven.kneiser):     ... this should include the rewriting of the zeroth branch to use new forkVar, after it's created of-course  --
	//UseDefIdx sourceUseDefIdx = this->getUseDefIdxByVar(sourceVarIdx);
	if (not this->useDefChains.contains(sourceVarIdx)) {  // bound-check useDefChains. Create one if it doesn't exist? Abort?

		return 0;  //TODO(steven.kneiser): new branch
	}
	useDefChain &sourceChain = this->useDefChains[sourceVarIdx];

	if (not sourceChain.hasCopyProcess()) {
		//this->createVarDefFork(sourceChain.varIdx);

		return 0;  //TODO(steven.kneiser): new branch
	}


	//TODO(steven.kneiser): prune/delete this header? I actually have even more conviction in it
	TransitionIdx forkTransitionIdx = sourceChain.copyProcess;  //TODO(steven.kneiser): clean up. for now, we'll test the egregious type violation of UseDefIdx actually matching the VarIdx in this->vars
	VarIdx varForkIdx = this->getEnumeratedVar(sourceVarIdx, 0, "~NEO_fork--");
	if (varForkIdx >= this->vars.size()) { cerr << "ERROR: varForkIdx @ " << varForkIdx << " doesn't exist in this->vars. No graceful failure." << endl; return 0; }






	//// 2) Create branch transition atop sourceVar's CopyProcess fork transition
	///////VarIdx varForkIdx = sourceChain.copyProcess;  //TODO(steven.kneiser): clean up. for now, we'll test the egregious type violation of UseDefIdx actually matching the VarIdx in this->vars
	/////if (varForkIdx >= this->vars.size()) { cerr << "ERROR: varForkIdx @ " << varForkIdx << " doesn't exist in this->vars. No graceful failure." << endl; return 0; }

	///////TODO(steven.kneiser): are source & fork the best taxonomy for disambiguating these chains? Make the parameters to this func match! Let this inform or vice versa.

	///////UseDefIdx forkUseDefIdx = this->getUseDefIdxByVar(varForkIdx);
	/////if (not this->useDefChains.contains(varForkIdx)) { cerr << "ERROR: " << endl; return 0; }
	/////const useDefChain &forkChain = this->useDefChains[varForkIdx];  //TODO: egregious type violation again, this varIdx should match the UseDefIdx if we go that other route

	/////if (forkChain.defs.empty()) { cerr << "ERROR: forkChain.defs is empty. No graceful failure." << endl; return 0; }
	/////TransitionIdx forkTransitionIdx = forkChain.defs[0];
	//TODO(steven.kneiser): pushd pushd RETVRN HERE to finish this Part 2, THEN fix build errors & verify all Part 1-3 are functioning correctly!
	//   ...if they are, then straight to updating ProjectionSets in the parent above, then straight to studying whether they work in Projection or not!
	// ...erm, do we need to ensure this happens exlusively BEFORE channel insertion? (probably, channel-insertion feels like it should be the VERY LAST thing in rewriteAssignmentsAsChannels())

	if (not this->transitions.is_valid(forkTransitionIdx)) { cerr << "ERROR: forkTransitionIdx @ T" << forkTransitionIdx << " isn't valid. No graceful failure." << endl; return 0; }
	chp::transition &forkTransition = this->transitions[forkTransitionIdx];

	arithmetic::Action branchAssignment(Expression::varOf(branchVarIdx), Expression::varOf(varForkIdx));
	chp::transition branchTransition(Expression::vdd(), arithmetic::Choice({{branchAssignment}}));
	//TODO(steven.kneiser): preserve guard g?? Don't see how one might be here, but just to be safe/thorough? ...to save future stumblers

	petri::iterator forkTransitionIt(petri::transition::type, forkTransitionIdx);
	//petri::iterator branchTransitionIt = this->insert_after(forkTransitionIt, branchTransition);

	vector<petri::iterator> forkTailIts = this->next(forkTransitionIt);
	if (forkTailIts.empty()) { cerr << "ERROR: no petri::next() children for forkTransition at index " << forkTransitionIdx << ". No graceful failure." << endl; return 0; }
	petri::iterator forkDummyTailIt = forkTailIts[0];

	vector<petri::iterator> forkDummyTailIts = this->next(forkDummyTailIt);
	if (forkDummyTailIts.empty()) { cerr << "ERROR: no petri::next() children for forkTransition at index " << forkTransitionIdx << ". No graceful failure." << endl; return 0; }
	petri::iterator forkDoubleDummyTailIt = forkDummyTailIts[0];

	petri::iterator branchTransitionIt = this->insert_alongside(forkTransitionIt, forkDoubleDummyTailIt, branchTransition);
	//TODO(steven.kneiser): RETVRN HERE verify the transition indexes compared to what my iter labels here CLAIM
	//  ...also why is my fork showing up AFTER 


	// -> FROM rewriteAsgnAsCopyProc
		////if (use >= this->vars.size()) { continue; }
		////string userName = this->vars[use].name;

		//////TODO(steven.kneiser): Clean up the awkward "--0" tail workaround (tempting because getEnumeratedVar helper always trails int, which can awkwardly mix with DSA-enumerated vars from userName)
		////VarIdx varBranchIdx = this->getEnumeratedVar(varAssigned, 0, "~branch~" + userName + "--");
		////arithmetic::Action branchAssignment(Expression::varOf(varBranchIdx), Expression::varOf(varForkIdx));
		////chp::transition branchTransition(Expression::vdd(), arithmetic::Choice({{branchAssignment}}));

		////petri::iterator branchIt = this->super::insert_alongside(forkIt, dummyTailIt, branchTransition);

	return branchTransitionIt.index;
}
//TODO(steven.kneiser): aHA, we should return a petri::iterator or some other type that enables "invailidity" or more obvious null response to protect future travelers


// For every multi-use variable in useDefChains, create a fork to branch off of
void graph::createCopyProcessForksForMultiUseVars() {
	clog << "creating all Copy Processes in batch." << endl;

	for (auto &[varIdx, chain] : this->useDefChains) {
		if (chain.uses.size() > 1) {
			clog << endl << "==> COPY: " << chain.name << endl << endl;
			//TODO(steven.kneiser): now abstract into this->createVarDefFork(varIdx) helper

			////VarIdx varForkIdx = this->getEnumeratedVar(varIdx, 0, "~fork--");
			////projectionSets[varForkIdx].insert(ProjectionItem(varForkIdx));

			////////TODO: is this much recalculation needed to retrace definition?
			////useDefChain &dependencyUseDefChain = this->useDefChains[varIdx];
			////if (dependencyUseDefChain.defs.empty()) { continue; }

			////TransitionIdx defTransitionIdx = varIdx.defs[0];
			////petri::iterator defTransitionIt(petri::transition::type, defTransitionIdx);



			//TODO(steven.kneiser): handle properly, instead of quiet failure?
			if (chain.defs.empty()) { clog << "ERROR: copyVar definition not found for var `" << chain.name << "`?? skipping." << endl; continue; }
			TransitionIdx defTransitionIdx = chain.defs[0];

			//TODO(steven.kneiser): handle properly, instead of quiet failure?
			if (not this->transitions.is_valid(defTransitionIdx)) { clog << "ERROR: copyVar definition not a valid transition at index " << defTransitionIdx << "?? skipping." << endl; continue; }
			const chp::transition &defTransition = this->transitions[defTransitionIdx];

			Expression rexpr;
			const arithmetic::Choice &choice = defTransition.action;
			for (const arithmetic::Parallel &term : choice.terms) {
				for (const arithmetic::Action &action : term.actions) {
					if (not action.lvalue.isUndef()) {
						rexpr = action.rvalue;
					}
					break;  //TODO(steven.kneiser): why was this here, even in the `getPreviousDefinitions()` above that I pasted it from? ...probably an old simplifying assumption that it'll be 1 transition
				}
			}

			VarIdx varForkIdx = this->getEnumeratedVar(varIdx, 0, "~NEO_fork--");
			VarIdx varSourceIdx = this->getEnumeratedVar(varIdx, 0, "~lone--");
			//chain.copyProcess = varForkIdx;  //TODO(steven.kneiser): egregious type violation, should be UseDefIdx
																			//  ...where we look it up via "chain.copyProcess.def/s[0]",

			// Now create fork transition
			//TODO(steven.kneiser): perf optimization: this an opportunity to directly embed the original rval/rexpr instead of a redundant assignment/channel-communication before the fork
			arithmetic::Action forkAssignment(Expression::varOf(varForkIdx), rexpr); //Expression::varOf(varSourceIdx)
			//TODO(steven.kneiser): don't forget to include guard, g, if there is one! (repeat the impl in rewriteAssignmentAsChannel() )
			chp::transition forkTransition(Expression::vdd(), arithmetic::Choice({{forkAssignment}}));
			petri::iterator defTransitionIt(petri::transition::type, defTransitionIdx);
			petri::iterator forkIt = this->super::insert_after(defTransitionIt, forkTransition);

			//NOTE(steven.kneiser): this "double dummy-tail" is my current algorithmic workaround to enable persistent branching via petri::insert_alongside()'s in parallal
			petri::iterator dummyTailIt = this->super::insert_after(forkIt, chp::transition());
			//petri::iterator doubleDummyTailIt = this->super::insert_after(dummyTailIt, chp::transition());
			//petri::iterator tripleDummyTailIt = this->super::insert_after(doubleDummyTailIt, chp::transition());

			chain.copyProcess = forkIt.index;  //TODO(steven.kneiser): egregious type violation, should be UseDefIdx
																			//  ...where we look it up via "chain.copyProcess.def/s[0]",


			this->copyProcessChainIdxs.insert(chain.varIdx);  //TODO(steven.kneiser): egregious type violation, should be UseDefIdx
																			//  ...where we look it up via "chain.copyProcess.def/s[0]",
			//this->copyProcessChainIdxs.insert(varForkIdx);  //TODO(steven.kneiser): egregious type violation, should be UseDefIdx

			//this->pinch(dummyTailIt);
			this->pinch(defTransitionIt); //TODO(steven.kneiser): umm, why not just rename the lvar of defTransition?
			//  ...ahh, this seperation might make it much easier to write the algorithm for R_5 -> R_6 of polymorphic input channels
		}
	}

	clog << "created all Copy Processes in batch." << endl;
}


void graph::rewriteAssignmentsAsChannels(
		const unordered_map<VarIdx, set<VarIdx>> &invertedDependencySets,
		unordered_map<VarIdx, set<ProjectionItem>> &projectionSets) {
	clog << "rewriting assignments as channels." << endl;



	//// if (debug) {} ??
#ifdef GRAPHVIZ_SUPPORTED
	std::filesystem::path debugDirPath = std::filesystem::current_path() / "build" / "dbg";
	string prefix = "";
	string mid0_filename = (debugDirPath / (prefix + this->name + "_analysis_mid0.png")).string();
	string mid0_dot = chp::export_graph(*this, true, false).to_string();
	gvdot::render(mid0_filename, mid0_dot);
#endif




	//TODO(steven.kneiser): wait, where are these getting cut now? Do we no longer cut them? No! Now we just instantiate subprocesses! Snip them from the codebase!
	//set<VarIdx> _copiedVars;  //TODO(steven.kneiser): dedup guard splitting & copy-variables with shared todo set
	//unordered_map<VarIdx, set<TransitionIdx>> copyProcesses; //copySets?
	this->createCopyProcessForksForMultiUseVars();


	//// if (debug) {} ??
#ifdef GRAPHVIZ_SUPPORTED
	//std::filesystem::path debugDirPath = std::filesystem::current_path() / "build" / "dbg";
	//string prefix = "";
	string cp_analysis_filename = (debugDirPath / (prefix + this->name + "_analysis_cp.png")).string();
	string cp_analysis_dot = chp::export_analysis(*this, true, true).to_string();
	gvdot::render(cp_analysis_filename, cp_analysis_dot);
#endif

#ifdef GRAPHVIZ_SUPPORTED
	//std::filesystem::path debugDirPath = std::filesystem::current_path() / "build" / "dbg";
	//string prefix = "";
	string mid1_filename = (debugDirPath / (prefix + this->name + "_analysis_mid1.png")).string();
	string mid1_dot = chp::export_graph(*this, true, false).to_string();
	gvdot::render(mid1_filename, mid1_dot);
#endif




	//NOTE(steven.kneiser): these guard vars, even though often not referenced or "used" directly in definition, are certainly "used" indirectly
	//    ...to select the specific control-flow branches where that definition is executed
	this->rewriteEachGuardVarUsedInMultiDefinitionSelectionsAsCopyProcess(invertedDependencySets, projectionSets);



	//// if (debug) {} ??
#ifdef GRAPHVIZ_SUPPORTED
	//std::filesystem::path debugDirPath = std::filesystem::current_path() / "build" / "dbg";
	//string prefix = "";
	string mid2_filename = (debugDirPath / (prefix + this->name + "_analysis_mid2.png")).string();
	string mid2_dot = chp::export_graph(*this, true, false).to_string();
	gvdot::render(mid2_filename, mid2_dot);
#endif



	//TODO(steven.kneiser): ugh, what's the right way to deduplicate the copy processes from these seperate methods? How should these be pre-merged?
	this->rewriteEachMultiUseVarAsCopyProcess(invertedDependencySets, projectionSets);



	//// if (debug) {} ??
#ifdef GRAPHVIZ_SUPPORTED
	//std::filesystem::path debugDirPath = std::filesystem::current_path() / "build" / "dbg";
	//string prefix = "";
	string mid3_filename = (debugDirPath / (prefix + this->name + "_analysis_mid3.png")).string();
	string mid3_dot = chp::export_graph(*this, true, false).to_string();
	gvdot::render(mid3_filename, mid3_dot);
#endif



	// NOTE(steven.kneiser): we intentionally convert all multi-use vars before any single-use vars
	//TODO(steven.kneiser): perf optimize these mutually-exclusive subsets from 2 for-loops to 1
	this->rewriteEachSingleUseVarAsDirectChannel(invertedDependencySets, projectionSets);

	//TODO(steven.kneiser): these funcs would be cleaner if they surfaced the I/O decision of "okay now rewrite that one"
	//    ...it seems much cleaner to have someone rewrite/change the assignment, then merely provide a func that accepts the name of the channel to rewrite that assignment as surface THAT policy decision.NNNBBB




	//// if (debug) {} ??
#ifdef GRAPHVIZ_SUPPORTED
	//std::filesystem::path debugDirPath = std::filesystem::current_path() / "build" / "dbg";
	//string prefix = "";
	string mid4_filename = (debugDirPath / (prefix + this->name + "_analysis_mid4.png")).string();
	string mid4_dot = chp::export_graph(*this, true, false).to_string();
	gvdot::render(mid4_filename, mid4_dot);
#endif



	// Rewrite the base fork of Copy Processes as a Channel communication,
	//   now that branches/arms have been attached & converted to Channels as well
	for (UseDefIdx copyProcessChainIdx : this->copyProcessChainIdxs) {  //TODO(steven.kneiser): verify we this value is now properly a UseDefIdx, not a hacky VarIdx (from before this->useDefs migration)
		if (not this->useDefChains.contains(copyProcessChainIdx)) { clog << "useDefChain not found at index " << copyProcessChainIdx << ". Unable to rewrite base fork as Channel communication" << endl; continue; }  //TODO(steven.kneiser): migrate to newer this->useDefs
		const useDefChain &chain = this->useDefChains[copyProcessChainIdx];

		if (not chain.hasCopyProcess()) { continue; }  //NOTE(steven.kneiser): redundant, but safe & exmple for future best-practice

		// Prune dummy tail
		//NOTE(steven.kneiser): we finished using this scaffolding, designed for cleaner fork branching (especially when debugging multiple rewrite steps)
		//  ...this assumes our forkTransition (the base of the Copy Process) only has one out-place connecting directly to the dummy skip-transition, pointed to by every branchTransition
		//VarIdx varForIdx = chain.varIdx;
		if (chain.defs.empty()) { error("", "chain is empty", __FILE__, __LINE__); continue; }
		TransitionIdx forkTransitionIdx = chain.defs[0];
		petri::iterator forkTransitionIt(petri::transition::type, forkTransitionIdx);
		//vector<petri::iterator> outPlaceIts = this->next(forkTransitionIt);
		//if (outPlacesIts.empty()) { cerr << "ERROR: no petri::next() children for forkTransition at index " << forkTransitionIdx << ". No graceful failure." << endl; return 0; }

		//NOTE(steven.kneiser): assuming there exists at least one & only one skip-transition, which is the dummy tail pointed to by other branch transitions
		bool matchFound = false;
		for (petri::iterator outPlaceIt : this->next(forkTransitionIt)) {
			for (petri::iterator outTransitionIt : this->next(outPlaceIt)) {

				if (outTransitionIt.index >= this->transitions.size()) { internal("", "outTransitionIt.index is out-of-bounds", __FILE__, __LINE__); continue; }
				chp::transition &outTransition = this->transitions[outTransitionIt.index];
				if (not this->transitions.is_valid(outTransitionIt.index)) { internal("", "outTransition isn't valid", __FILE__, __LINE__); continue; }

				// Is this a skip-transition?
				if (outTransition.is_vacuous()) { // and outTransition.action.empty()) {
					bool matchFound = true;
					this->erase(outPlaceIt);
					//this->pinch(outTransitionIt);
					break;
				}
			}
			if (matchFound) { break; }
		}

		// Rewrite Copy Process forks as channels
		VarIdx forkChannelIdx = this->getEnumeratedVar(chain.varIdx, 0, "~NEO_FORK_CHAN--");
		this->rewriteAssignmentAsChannel(chain.copyProcess, forkChannelIdx);
	}



	//// if (debug) {} ??
#ifdef GRAPHVIZ_SUPPORTED
	//std::filesystem::path debugDirPath = std::filesystem::current_path() / "build" / "dbg";
	//string prefix = "";
	string mid5_filename = (debugDirPath / (prefix + this->name + "_analysis_mid5.png")).string();
	string mid5_dot = chp::export_graph(*this, true, false).to_string();
	gvdot::render(mid5_filename, mid5_dot);
#endif



	clog << "rewrote assignments as channels." << endl;
}


unordered_map<VarIdx, set<ProjectionItem>> graph::computeProjectionSets() {
	clog << endl << "computing projection sets." << endl;

	unordered_map<VarIdx, vector<VarIdx>> dependencySets;
	unordered_map<VarIdx, set<ProjectionItem>> projectionSets;
	//TODO: assign more efficiently after populating dependencySets in full
	//TODO(steven.kneiser): document intended diff between depSets & projSets ...is it just using ProjectionItem's properly for channels?
	//    ...since depSets are mainly for computing invDepSets while projSets are for the ultimate final projection

	//TODO(steven.kneiser): great work! Now clean out all "in/out channel" comments & detection schemes now that we have ProjectionItems. Review if this is still the cleanest


	//
	// 1) Build Dependency Sets
	//
	//TODO: vector -> set? t'sin the name bro -- WAIT, they're NOT sets! they can have multi-uses (do we support those yet?)
	for (TransitionIdx transitionIdx = 0; transitionIdx < this->transitions.size(); transitionIdx++) {
		if (not this->transitions.is_valid(transitionIdx)) { continue; }

		// Extract Dependency Set from transition, if there is any
		const chp::transition &transition = this->transitions[transitionIdx];
		vector<VarIdx> guardVars = getVarsFromExpression(transition.guard);

		const arithmetic::Choice &choice = transition.action;
		for (const auto &term : choice.terms) {
			for (const auto &action : term.actions) {
				vector<VarIdx> leftVars = getVarsFromExpression(action.lvalue);

				//TODO: is_definition parameter could be more robust ":=" assignment operand matching
				vector<VarIdx> rightVars = getVarsFromExpression(action.rvalue);
				vector<VarIdx> inputChannelsUsed = findInputChannelsInExpression(action.rvalue);
				vector<VarIdx> outputChannelsUsed = findOutputChannelsInExpression(action.rvalue);  //TODO: are you confident in the ordering out of this algorithm?

				// If not assignment, check for output-channel (a.k.a. "send()") which is assignment-ish
				//TODO: there must be a better way to pre-index not just channels vs vars DURING synthesis but beforehand
				//  AND it should somehow index the partition between input vs output channels (perhaps, in-only, out-only, and bi-use'd?)
				//  within "bi-used" we can separate "internal-communication only" channels vs bi-used chans w/ external side-effects/dependencies
				if (leftVars.empty()) {
					if (outputChannelsUsed.empty()) { continue; }

					VarIdx outputChannel = outputChannelsUsed[0]; //TODO: support more than one outputChannel per transition
					clog << " ! CHAN<" << this->vars[outputChannel].name << ">" << endl;

					// Prune redundant self from right-hand side
					rightVars.erase(std::remove(rightVars.begin(), rightVars.end(), outputChannel), rightVars.end());
					dependencySets[outputChannel].insert(dependencySets[outputChannel].begin(), rightVars.begin(), rightVars.end());
					projectionSets[outputChannel].insert(rightVars.begin(), rightVars.end());
					//TODO(steven.kneiser): why, for example, do these projectionSets not get inserted the same as the for-loop? Shouldn't it be either?
					//   Etiher we're matching depSets or doing the for-loop I would assume

					for (VarIdx var : rightVars) {
						if (find(inputChannelsUsed.begin(), inputChannelsUsed.end(), var) != inputChannelsUsed.end()) {
							projectionSets[outputChannel].insert(ProjectionItem(var, true, false));

						} else {
							projectionSets[outputChannel].insert(ProjectionItem(var));
						}
					}
					projectionSets[outputChannel].insert(ProjectionItem(outputChannel, true, true));

					// Not an output channel, set DependencySet
				} else {
					//TODO(steven.kneiser): Support more complex assignments like multi-variable tuples
					//  or vector-assignment in lvalue, instead of a lone variable (e.g. `(x, y) = (5, 4)`, maybe even `-x = 3`??)
					VarIdx assignedVar = leftVars[0];
					dependencySets[assignedVar].insert(dependencySets[assignedVar].begin(), rightVars.begin(), rightVars.end());
					//projectionSets[assignedVar].insert(rightVars.begin(), rightVars.end());

					for (VarIdx var : rightVars) {
						if (find(inputChannelsUsed.begin(), inputChannelsUsed.end(), var) != inputChannelsUsed.end()) {
							projectionSets[assignedVar].insert(ProjectionItem(var, true, false));

						} else {
							projectionSets[assignedVar].insert(ProjectionItem(var));
						}
					}
					projectionSets[assignedVar].insert(ProjectionItem(assignedVar));
				}
			}
		}
	}


	//TODO(steven.kneiser): print & dehug/verify useDefChains again
	clog << endl << " ~~> ~~> ~~> DepSets" << endl;
	std::for_each(dependencySets.begin(), dependencySets.end(), [this](auto &dep) {
			clog << this->vars[dep.first].name << " <- ";
			std::transform(dep.second.begin(), dep.second.end(), ostream_iterator<string>(clog, ", "), [this](VarIdx varIdx) { return this->vars[varIdx].name; });
			clog << endl;
			});

	//TODO(steven.kneiser): verify Dependency Sets & ensure only channel-Sends are included
	//TODO(steven.kneiser): verifying these Sets are a great test suite opportunity
	clog << endl << " ~> ~> ~> defs/targets: ";
	for (const auto &[varIdx, _] : dependencySets) { clog << this->vars[varIdx].name << " "; }
	clog << endl;


	//
	// 2) Invert Dependency Sets, to detect which users depend on this var's definition
	//
	std::unordered_map<VarIdx, set<VarIdx>> invertedDependencySets;
	for (const auto &[target, dependencies] : dependencySets) {
		for (VarIdx dependency : dependencies) {
			invertedDependencySets[dependency].insert(target);

			if (not dependencySets.contains(dependency)) {
				clog << " ? CHAN<" << this->vars[dependency].name << ">" << endl;
			}
		}
	}

	clog << endl << " <~~ <~~ <~~ InvDepSets" << endl;
	std::for_each(invertedDependencySets.begin(), invertedDependencySets.end(), [this](auto &dep) {
			clog << this->vars[dep.first].name << " <- ";
			std::transform(dep.second.begin(), dep.second.end(), ostream_iterator<string>(clog, ", "), [this](VarIdx varIdx) { return this->vars[varIdx].name; });
			clog << endl;
			});


	//
	// 3) Insert copy variables & internal-communication channels
	//
	this->rewriteAssignmentsAsChannels(invertedDependencySets, projectionSets);

	clog << endl << "  # ## ### < PS> ### ## #  " << endl;
	auto toString = [this](const ProjectionItem &p) -> string {
		return this->vars[p.index].name + (p.isChannel ? (p.isSend ? "!" : "?") : "");
	};

	std::for_each(projectionSets.begin(), projectionSets.end(),
			[this, &toString](auto &dep) {
			clog << "  <( " << this->vars[dep.first].name << " )>  ";

			std::transform(dep.second.begin(), dep.second.end(), ostream_iterator<string>(clog, ", "),
					[&toString](const ProjectionItem &item) {
					return toString(item);
					});
			clog << endl;
			});
	clog << "  # ## ### </PS> ### ## #  " << endl << endl;

	return projectionSets;
}


//TODO: for arithmetic::Expression perf, cache vector of vars present whenever Expression is updated
//
//
// Data-driven Decomposition
//
//
vector<graph> graph::project() {
	clog << endl << "projecting." << endl;

	vector<chp::graph> processes;
	unordered_map<VarIdx, set<ProjectionItem>> projectionSets = this->computeProjectionSets();

	for (const auto& [var, items] : projectionSets) {
		chp::graph process = *this;
		process.name += + "_" + this->vars[var].name;
		clog << "extracting: " << process.name << endl;
		vector<TransitionIdx> toDelete;
		set<ProjectionItem> varItems(items.begin(), items.end());

		// Find transitions of a duplicate chp::graph that don't contain any projected component
		for (TransitionIdx transitionIdx = 0; transitionIdx < process.transitions.size(); transitionIdx++) {
			//TODO(steven.kneiser): We should just make a VaildIterator filter/view of the index_vector so we don't pollute the codebase with this foreseeable "I forgot" footgun:
			if (not process.transitions.is_valid(transitionIdx)) { continue; }
			const chp::transition &transition = process.transitions[transitionIdx];

			if (isDisqualifyingItemInExpression(transition.guard, varItems)) {
				isDisqualifyingItemInExpression(transition.guard, varItems, true);  // Repeat to log debug info
				toDelete.push_back(transitionIdx);
				continue;
			}

			bool disqualifyingItemFound = false;
			const arithmetic::Choice &action = transition.action;
			for (const arithmetic::Parallel &term : action.terms) {
				for (const arithmetic::Action &action : term.actions) {

					if (isDisqualifyingItemInExpression(action.lvalue, varItems)) {
						isDisqualifyingItemInExpression(action.lvalue, varItems, true);
						disqualifyingItemFound = true;
						break;
					}
					if (isDisqualifyingItemInExpression(action.rvalue, varItems)) {
						disqualifyingItemFound = true;
						isDisqualifyingItemInExpression(action.rvalue, varItems, true);
						break;
					}
				}
				if (disqualifyingItemFound) { break; }
			}
			if (disqualifyingItemFound) {
				toDelete.push_back(transitionIdx);
			}
		}
		clog << "garbage collected: " << toDelete.size() << endl;

		// Prune components outside the projection
		size_t watchDog = 0;
		for (TransitionIdx transitionIdx : toDelete) {
			//petri::iterator irrelevantTransitionIt(petri::transition::type, transitionIdx);
			//process.super::pinch(irrelevantTransitionIt);
			chp::transition &transition = process.transitions[transitionIdx];
			transition.guard = Expression::vdd();
			transition.action = arithmetic::Choice({{}});

			if (watchDog > MAX_PROCESS_COUNT) { cerr << "projection watchdog woof!" << endl; break; }
			watchDog++;
		}
		//process.post_process(true, false);
		process.reduce(true, false, false); //true);

		processes.push_back(process);
		clog << "extracted." << endl;
	}

	clog << "projected." << endl;
	return processes;
}


}
