#include "graph.h"

#include <queue>
#include <ranges>

#include <arithmetic/expression.h>
#include <chp/simulator.h>
#include <common/message.h>
#include <common/text.h>
#include <common/mapping.h>
#include <interpret_arithmetic/export.h>

//TODO: delete after development
#include <algorithm>
#include <interpret_chp/export_dot.h>

//TODO: nice. Now substitute them for readability [at least in graph::project()]. added to header.
//typedef size_t TransitionIdx;
//typedef size_t VarIdx;

using arithmetic::Expression;

namespace chp
{

place::place()
{
	arbiter = false;
}

place::~place()
{

}

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

transition::~transition()
{

}

transition transition::merge(int composition, const transition &t0, const transition &t1) {
	if (composition == petri::parallel or composition == petri::sequence) {
		transition result(t0.guard & t1.guard, false);
		for (int i = 0; i < (int)t0.action.terms.size(); i++) {
			for (int j = 0; j < (int)t1.action.terms.size(); j++) {
				result.action.terms.push_back(arithmetic::Parallel());
				result.action.terms.back().actions.insert(result.action.terms.back().actions.end(), t0.action.terms[i].actions.begin(), t0.action.terms[i].actions.end());
				result.action.terms.back().actions.insert(result.action.terms.back().actions.end(), t1.action.terms[j].actions.begin(), t1.action.terms[j].actions.end());
			}
		}
		result.guard.minimize();
		return result;

	} else if (composition == petri::choice) {
		transition result(t0.guard | t1.guard, false);
		result.action.terms.insert(result.action.terms.end(), t0.action.terms.begin(), t0.action.terms.end());
		result.action.terms.insert(result.action.terms.end(), t1.action.terms.begin(), t1.action.terms.end());
		result.guard.minimize();
		return result;
	}
	//result.guard.minimize();
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

variable::~variable() {
}

graph::graph()
{
}

graph::~graph()
{

}

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
int graph::create(variable n) {
	int uid = vars.size();
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
				bool firable = transitions[sim.ready[j].index].action.terms.size() <= 1;
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

	change = true;
	while (change) {
		super::reduce(proper_nesting, aggressive);

		// Remove skips
		change = false;
		for (petri::iterator i(transition::type, 0); i < (int)transitions.size() and not change; i++) {
			if (not is_valid(i)) continue;

			if (transitions[i.index].is_vacuous()) {
				vector<petri::iterator> n = next(i); // places
				if (n.size() > 1) {
					//cout << "removing skip T" << i.index << ": " << transitions[i.index].guard << "->" << transitions[i.index].action << endl;
					vector<petri::iterator> p = prev(i); // places
					vector<vector<petri::iterator> > pp;
					for (int j = 0; j < (int)p.size(); j++) {
						pp.push_back(prev(p[j]));
					}

					for (int k = (int)arcs[petri::transition::type].size()-1; k >= 0; k--) {
						if (arcs[petri::transition::type][k].from == i) {
							erase_arc(petri::iterator(petri::transition::type, k));
						}
					}

					vector<petri::iterator> copies;
					copies.push_back(i);
					for (int k = 0; k < (int)n.size(); k++) {
						if (k > 0) {
							copies.push_back(copy(i));
							for (int l = 0; l < (int)p.size(); l++) {
								petri::iterator x = copy(p[l]);
								connect(pp[l], x);
								connect(x, copies.back());
							}
						}
						connect(copies.back(), n[k]);
					}
					//cout << "removed skip" << endl;
					change = true;
				}
			}
		}
		if (change)
			continue;

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
		}*/
	}
}

vector<graph> graph::decompose() {  //chp::graph &g) {}
	// TODO: Return new additional subgraphs (optional: pass w/ self for forest of processes)
	cout << endl << "\\_,.=~-^*'\"`\\_,.=~-^*'\"`\\_,.=~-^*'\"`\\_,.=~-^*'\"`\\_,.=~-^*'\"`\\_,.=~-^*'\"`\\_,.=~-^*'\"`\\_,.=~-^*'\"`" << endl << endl;
	cout << "decomposing: " << this->name << endl;

	this->convertToDSA();
	this->computeUseDefChains();
	vector<graph> processes = this->project();

	cout << "decomposed." << endl;
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
	state &marking = this->reset[0];  //TODO: handle multiple resets in this->reset?
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

	// Find starting node & populate entry block
	petri::iterator init_transition;
	for (size_t transition_idx = 0; transition_idx < this->transitions.size(); transition_idx++) {
		init_transition = petri::iterator(transition::type, transition_idx);
		if (this->is_valid(init_transition)) {
			break;
		}
	}

	if (init_transition.index == -1) {
		cout << "CFG is empty" << endl;
		this->controlFlowGraphReady = true;
		return;
	}

	// Populate initial block
	chp::graph::controlFlowBlock current_block(
			0,
			{},
			{},
			init_transition,
			init_transition,
			{});
	this->controlFlowGraph.push_back(current_block);

	// Crawl transitions breadth-first to cluster into Control-Flow Graph blocks
	//   with path-tracking to uncover most-relevant "Reaching Definitions"
	petri::iterator current_it;
	vector<petri::iterator> current_path;  // Full path to current node, including current node
	std::queue<vector<petri::iterator>> queue;
	std::set<petri::iterator> visited;
	queue.push({init_transition});

	while (not queue.empty()) {
		vector<petri::iterator> current_path = queue.front();
		queue.pop();
		//if (not current_path.empty()) {
		//	current_path = vector<petri::iterator>(current_path.begin(), current_path.end() - 1);
		//}

		current_it = current_path.back();
		visited.insert(current_it);

		size_t current_block_uid = this->transitionToBlock[current_it.index];
		chp::graph::controlFlowBlock current_block = this->controlFlowGraph[current_block_uid];

		// If statement is an assignment, document gen-kill sets
		auto [var_assigned, prevDefinitions] = this->getPreviousDefinitions(current_it, current_path);
		if (var_assigned != -1) {
			this->controlFlowGraph[current_block.uid].gens[current_it.index] = var_assigned;

			if (not prevDefinitions.empty()) {
				size_t transitionToKill = prevDefinitions.back();
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
				size_t next_block_uid = this->transitionToBlock[next_transition_it.index];
				if (next_block_uid == 0) { continue; }  //TODO: remove hack after testing. This cut unrolls the unconiditional loopback from program end-to-beginning for repetition-intolerant DSA enumeration. Once nested repetitions are supported, this can go.
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
				size_t new_block_uid = this->controlFlowGraph.size();
				chp::graph::controlFlowBlock new_block(
						new_block_uid,
						{current_block.uid},
						{},
						next_transition_it,
						next_transition_it,
						{next_transition_it});
				this->controlFlowGraph.push_back(new_block);
				this->transitionToBlock[next_transition_it.index] = new_block_uid;
				this->controlFlowGraph[current_block.uid].outs.insert(new_block_uid);

				current_path.push_back(next_transition_it);
				queue.push(current_path);
				continue;
			}

			// Default-case (sequence): Append to current block
			this->controlFlowGraph[current_block_uid].last = next_transition_it;
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
				size_t next_block_uid = this->transitionToBlock[next_transition_it.index];
				this->controlFlowGraph[next_block_uid].ins.insert(current_block.uid);
				this->controlFlowGraph[current_block.uid].outs.insert(next_block_uid);
				continue;
			}

			// Create new block
			size_t new_block_uid = this->controlFlowGraph.size();
			chp::graph::controlFlowBlock new_block(
					new_block_uid,
					{current_block.uid},
					{},
					next_transition_it,
					next_transition_it,
					{next_transition_it});
			this->controlFlowGraph.push_back(new_block);
			this->transitionToBlock[next_transition_it.index] = new_block_uid;
			this->controlFlowGraph[current_block.uid].outs.insert(new_block_uid);

			if (!visited.contains(next_transition_it)) {
				current_path.push_back(next_transition_it);
				queue.push(current_path);
			}
		}
	}

	this->controlFlowGraphReady = true;
}

void graph::setUseDef(size_t chp_var_idx, size_t transition_idx, bool is_definition) {
	string chp_var_name = this->netAt(chp_var_idx);

	// New variable? Record its name
	if (this->useDefChains.find(chp_var_idx) == this->useDefChains.end()) {
		this->useDefChains[chp_var_idx].name = chp_var_name;
		//this->useDefChains[chp_var_idx].index = chp_var_idx;
	}

	if (is_definition) {
		this->useDefChains[chp_var_idx].defs.push_back(transition_idx);
		//this->defs[chp_var_name].push_back(transition_idx);
		cout << "DEF " << chp_var_name << " @ " << transition_idx << endl;

	} else {
		this->useDefChains[chp_var_idx].uses.push_back(transition_idx);
		//this->uses[chp_var_name].push_back(transition_idx);
		cout << "use " << chp_var_name << " @ " << transition_idx << endl;
	}
}

void graph::extractUseDefFromExpression(size_t transition_idx, const arithmetic::Expression &e, bool is_definition) {
	if (is_definition && e.top.isVar() && e.size() == 0) {
		this->setUseDef(e.top.index, transition_idx, true);

	} else if (not e.isUndef()) { //if (e.isExpr()) {
		for (const arithmetic::Operand &sub_expr : e.exprIndex()) {

			// Iterate across all sub-expression leaves
			//TODO: introduce some simpler "walkLeaves"-esque helper method into Expression?
			const arithmetic::Operation &operation = *e.getExpr(sub_expr.index);
			for (const arithmetic::Operand &operand : operation.operands) {
				if (operand.type == arithmetic::Operand::Type::VAR) {
					this->setUseDef(operand.index, transition_idx, is_definition);
				}
			}
		}
	} // else if (not e.isUndef()) {}
}

void graph::extractUseDefFromTransition(size_t transition_idx) {
	const chp::transition &tran = this->transitions[transition_idx];
	extractUseDefFromExpression(transition_idx, tran.guard);

	const arithmetic::Choice &action = tran.action;
	for (const auto &term : action.terms) {
		for (const auto &action : term.actions) {
			extractUseDefFromExpression(transition_idx, action.lvalue, true);

			//TODO: is_definition parameter could be more robust ":=" assignment operand matching
			extractUseDefFromExpression(transition_idx, action.rvalue);
		}
	}
}

void graph::computeUseDefChains() {
	this->useDefChainsReady = false;

	for (size_t transition_idx = 0; transition_idx < this->transitions.size(); transition_idx++) {
		petri::iterator t_it(transition::type, transition_idx);
		if (not this->is_valid(t_it)) { continue; }

		extractUseDefFromTransition(transition_idx);
	}

	this->useDefChainsReady = true;
}

pair<int, vector<size_t>> graph::getPreviousDefinitions(petri::iterator transition_it, const vector<petri::iterator> &prev_transitions) {
	//TODO: rename prev_transitions param, now that I include current def
	//TODO: return all redefinitions of the same var so it can be enumerated (the LAST one in this vector is the reaching def!)
	//TODO: perf, cache this inside each transition, so there's a running set? Where should the analysis data be stored?

	// If there's no assignment, skip
	size_t var_assigned;

	//TODO: isAssignment() expression helper in chp::transition?
	bool is_assignment = false;
	const chp::transition &transition = this->transitions[transition_it.index];
	const arithmetic::Choice &action = transition.action;
	for (const arithmetic::Parallel &term : action.terms) {
		for (const arithmetic::Action &action : term.actions) {
			if (not action.lvalue.isUndef()) {
				is_assignment = true;
				var_assigned = arithmetic::lvalueBase(action.lvalue, action.lvalue.top);
			}
			break;
		}
	}

	if (not is_assignment) {
		cout << "===> " << transition_it.index << ": N/A" << endl;
		return pair<int, vector<size_t>>(-1, {});
	}

	// Filter down previous transitions to only assignments
	vector<size_t> prev_defs;
	for (auto prev_transition_it = prev_transitions.begin(); prev_transition_it != prev_transitions.end() - 1; prev_transition_it++) {
		const chp::transition &prev_transition = this->transitions[prev_transition_it->index];

		const arithmetic::Choice &prev_action = prev_transition.action;
		for (const arithmetic::Parallel &term : prev_action.terms) {
			for (const arithmetic::Action &action : term.actions) {
				if (action.lvalue.isUndef()) { continue; }

				// Filter down previous assignments to only assignments of the same var
				size_t prev_var_assigned = arithmetic::lvalueBase(action.lvalue, action.lvalue.top);
				if (var_assigned == prev_var_assigned) {
					prev_defs.push_back(prev_transition_it->index);
				}
			}
		}
	}

	// The last transition in the return vector just got redefined, so "kill" it in the containing block
	cout << "===> " << transition_it.index << ": ";
	std::copy(prev_defs.begin(), prev_defs.end(), ostream_iterator<size_t>(std::cout, ", "));
	cout << endl;
	return pair<int, vector<size_t>>(var_assigned, prev_defs);
}

void graph::increaseBlockVarToDSAIndex(size_t blockIdx, size_t varIdx, size_t dsaCountAfter) {
	//TODO: assumes this call & all params are a valid copy assignment. Safeguard if calling under new conditions

	// Grab old DSA count from postDefs
	chp::graph::controlFlowBlock &block = this->controlFlowGraph[blockIdx];
	string varName = this->vars[varIdx].name;
	size_t dsaCountBefore = block.postDefs[varIdx];

	cout << " TODO: B[" << blockIdx << "] << " << varName << "_" << dsaCountAfter << " := " << varName << "_" << dsaCountBefore << endl;

	// First, clip block-exiting arc
	petri::iterator tailTransitionIt = block.transitions.back();
	//chp::transition &tailTransition = this->transitions[tailTransitionIt.index];

	//petri::iterator outboundArc = this->out(tailTransitionIt)[0];
	petri::iterator mergePlace = this->next(tailTransitionIt)[0];
	petri::iterator outboundArc = this->arc_between(tailTransitionIt, mergePlace);
	//TODO: verify arc returned is valid: if (outboundArc == petri::iterator()) { cerr << endl; }
	cout << " :: " << outboundArc << endl;


	// Insert new "v_new := v_old;" copy-assignment at the end of the block
	// TODO: update postDefs here in this function or above? ...probably above, doubly-so?
	arithmetic::Action newCopyAssignment;
	size_t preVarIdx = this->getEnumeratedVar(varIdx, dsaCountBefore);
	size_t postVarIdx = this->getEnumeratedVar(varIdx, dsaCountAfter);
	newCopyAssignment.lvalue = arithmetic::Expression::varOf(postVarIdx);
	newCopyAssignment.rvalue = arithmetic::Expression::varOf(preVarIdx);

	//TODO: ugh, I should re-use petri/graph.h::insert_after
	chp::transition newCopyAssignmentTransition(
			arithmetic::Expression::vdd(), arithmetic::Choice({{newCopyAssignment}}));
	size_t newTransitionIdx = this->transitions.insert(newCopyAssignmentTransition);

	// Insert copy-assignment after the block's last transition
	this->super::erase_arc(outboundArc);
	this->super::mark_modified();  //TODO: required by petri? not used in insert_after...

	petri::iterator newCopyAssignmentTransitionIt(petri::transition::type, newTransitionIdx);
	this->super::connect(newCopyAssignmentTransitionIt, mergePlace);
	this->super::connect(tailTransitionIt, newCopyAssignmentTransitionIt);

	block.transitions.push_back(newCopyAssignmentTransitionIt);
	block.last = newCopyAssignmentTransitionIt;
	block.gens[newTransitionIdx] = postVarIdx;
	block.postDefs[postVarIdx] = dsaCountAfter;
}

unordered_map<size_t, size_t> graph::mergeDefinitionsBeforeBlock(size_t blockId) {
	controlFlowBlock &block = this->controlFlowGraph[blockId];
	unordered_map<size_t, size_t> liveDefinitions;

	//unordered_map<size_t, unordered_map<size_t, size_t>> DSAIndexByVar;
	//unordered_map<size_t, size_t> from_block_idx, dsa_index;
	//unordered_map<size_t, size_t> var_idx, from_block_idx, dsa_index;
	//unordered_map<pair<size_t, size_t>, size_t> dsaIndicesByBlock;  // [var_idx, from_block_idx] => var_count (a.k.a. DSA Index)
	unordered_map<size_t, unordered_map<size_t, size_t>> dsaIndexPerBlockPerVar;  // var_idx -> [ from_block_idx -> var_count (a.k.a. DSA Index) ]

	// For each variable, aggregate DSA indices by input block source
	for (size_t inBlockIdx : block.ins) {
		const controlFlowBlock &in_block = this->controlFlowGraph[inBlockIdx];

		for (pair<size_t, size_t> inDef : in_block.postDefs) {
			dsaIndexPerBlockPerVar[inDef.first][inBlockIdx] = inDef.second;
		}
	}

	// Identify any post-selection variables that need additional copy assignments
	//TODO: block.preDefs merge/union(in_block.postDef for in_block in block.ins)
	for (const auto &[varIdx, dsaIndexPerBlock] : dsaIndexPerBlockPerVar) {
		//dsaIndexPerBlock[]
		// Start block with highest DSA index, in case of mismatch
		auto maxIt = std::max_element(dsaIndexPerBlock.begin(), dsaIndexPerBlock.end());
		size_t deepestBlock = maxIt->first;
		size_t maxIndex = maxIt->second;
		liveDefinitions[varIdx] = maxIndex;
		cout << "*** " << this->vars[varIdx].name << "[" << deepestBlock << "]: " << maxIndex << endl;

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

size_t graph::getEnumeratedVar(size_t varIdx, size_t num, string delimiter) {
	string enumeratedName = this->vars[varIdx].name + delimiter + std::to_string(num);

	int enumeratedVarIdx = this->netIndex(enumeratedName);
	if (enumeratedVarIdx == -1) {
		enumeratedVarIdx = this->vars.size();

		chp::variable enumeratedVar(enumeratedName);
		this->vars.push_back(enumeratedVar);
	}

	return enumeratedVarIdx;
}

void _debugPrint(queue<size_t> s) {
	cout << ">> ";
	while (!s.empty()) {
		cout << s.front() << " ";
		s.pop();
	}
	cout << endl;
}

void graph::convertToDSA() {
	this->computeControlFlowGraph();
	//this->computeUseDefChains();

	// Populate pre- & post- reaching definitions
	// Use [forward] iterative "Worklist" algorithm for data flow (remarkably stable, block-order-invariant)
	queue<size_t> worklist;
	//for (auto blockIt = this->controlFlowGraph.begin(); blockIt != this->controlFlowGraph.end(); ++blockIt) {
	//	worklist.push(blockIt->uid);  // Populate stack with first element at the top
	//}
	if (not this->controlFlowGraph.empty()) {
		worklist.push(this->controlFlowGraph[0].uid);
	}

	size_t workCount = 0;
	while (not worklist.empty()) {
		workCount++;

		cout << endl;
		_debugPrint(worklist);

		size_t blockId = worklist.front();
		worklist.pop();
		controlFlowBlock &block = this->controlFlowGraph[blockId];

		cout << "-=-=-=-=-=-=-=-=-=-=-=-=- " << workCount << " <><> " << blockId << endl;

		// Populate pre- definitions based on in-blocks
		unordered_map<size_t, size_t> liveDefinitions = this->mergeDefinitionsBeforeBlock(blockId);
		block.preDefs = liveDefinitions;

		// Enumerate block-internal vars in DSA form
		unordered_map<size_t, vector<vector<size_t>>> blockVarIndices; // var -> transition_idxs vector [def] of vectors[uses]

		for (petri::iterator transitionIt : block.transitions) {
			size_t transitionIdx = transitionIt.index;

			// Replace "killed" defintion with new redefinition
			bool isRedefinition = false;
			if (block.kills.contains(transitionIdx)) {
				isRedefinition = true;
				size_t redefinedVar = block.gens[transitionIdx];
				//size_t prev_defining_transition = block.kills[transitionIdx];
				liveDefinitions[redefinedVar]++;

			// Append first-time definition
			} else if (block.gens.contains(transitionIdx)) {
				size_t definedVar = block.gens[transitionIdx];
				liveDefinitions[definedVar] = 0;
			}

			if (not liveDefinitions.empty()) {
				//std::for_each(liveDefinitions.begin(), liveDefinitions.end(), [this](auto &d){ cout << this->vars[d.first].name << "[" << d.second << "], "; }); cout << endl;

				// Enumerate current expression w/ DSA indices
				Mapping<size_t> postDefIndices(std::numeric_limits<size_t>::max(), true);
				for (auto [varIdx, dsaIdx] : liveDefinitions) {
					size_t dsaVarIdx = this->getEnumeratedVar(varIdx, dsaIdx);
					postDefIndices.set(varIdx, dsaVarIdx);
				}

				//TODO: give these 2 mappings EVEN MORE explicit naming with some sort of "rename" postfix
				Mapping<size_t> preDefIndices(postDefIndices);
				if (isRedefinition) {
					size_t redefinedVar = block.gens[transitionIdx];
					size_t enumeratedVarAfter = liveDefinitions[redefinedVar];
					size_t enumeratedVarBefore = this->getEnumeratedVar(redefinedVar, enumeratedVarAfter - 1);
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
				cout << "umpty-dumpty" << endl;
			}
			//cout << "L>> " << transitionIt.lvalue << endl;
			//cout << "R>> " << transitionIt.rvalue << endl;
		}

		// Populate post-definitions based on local transformations, if any occurred
		//TODO: block.postPef = transfer(blockId, block.preDefs) // b.gen u (b.predef - b.kill)
		if (liveDefinitions != block.postDefs) {
			std::for_each(liveDefinitions.begin(), liveDefinitions.end(), [this](auto &d){
				cout << this->vars[d.first].name << "[" << d.second << "], "; });
			cout << endl;
			block.postDefs = liveDefinitions;

			for (const size_t &out : block.outs) {
				worklist.push(out);
			}
		}
	}

	cout << "DSA'd." << endl;
}

vector<size_t> getVarsFromExpression(const arithmetic::Expression &e) {
	if (e.isUndef()) { return {}; }
	if (e.top.isVar() && e.size() == 0) { return {e.top.index}; }

	vector<size_t> vars;
	for (const arithmetic::Operand &sub_expr : e.exprIndex()) {

		// Iterate across all sub-expression leaves
		//TODO: introduce some simpler "walkLeaves"-esque helper method into Expression?
		const arithmetic::Operation &operation = *e.getExpr(sub_expr.index);
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
	for (const arithmetic::Operand &sub_expr : e.exprIndex()) {
		// Iterate across all sub-expression leaves
		const arithmetic::Operation &operation = *e.getExpr(sub_expr.index);
		for (const arithmetic::Operand &operand : operation.operands) {
			if (operand.cnst.sval == "recv") {
				inputChannels.push_back(e.sub.elems.elems[0].operands[0].index);
			}
		}
	}

	return inputChannels;
}

//TODO: Clean up this sloppy algorithmic solution. No need to fully-traverse again.
vector<size_t> findOutputChannelsInExpression(const arithmetic::Expression &e) {
	if (e.isUndef() || (e.top.isVar() && e.size() == 0)) { return {}; }

	vector<VarIdx> outputChannels;
	for (const arithmetic::Operand &sub_expr : e.exprIndex()) {
		// Iterate across all sub-expression leaves
		const arithmetic::Operation &operation = *e.getExpr(sub_expr.index);
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
//petri::iterator graph::renameVarUseAferTransition(size_t insertionTransitionIdx, size_t preVar, size_t postVar) {
//
//	arithmetic::Action reassignment;
//	reassignment.lvalue = arithmetic::Expression::varOf(postVar);
//	reassignment.rvalue = arithmetic::Expression::varOf(preVar);
//
//	chp::transition renameTransition(
//			arithmetic::Expression::vdd(), arithmetic::Choice({{reassignment}}));
//	//size_t forkTransitionIdx = this->transitions.insert(forkAssignmentTransition);
//	//petri::iterator forkIt(petri::transition::type, forkTransitionIdx);
//
//	//TODO: extract this out (to be computed before when identifying transition, this way I can use it either right after the definiton or somewhere else)
//	useDefChain &varUseDefChain = this->useDefChains[preVar];
//	if (varUseDefChain.defs.empty()) { continue; }
//	size_t insertionTransitionIdx = varUseDefChain.defs[0];
//	//cout << this->transitions[insertionTransitionIdx] << endl;
//
//	petri::iterator insertionPoint(petri::transition::type, insertionTransitionIdx);
//	return this->super::insert_after(insertionPoint, renameTransition);
//}

//size_t graph::getVarDefTransition(size_t varIdx) {
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
//	size_t idx;
//	ChannelType type;
//	size_t channelIdx;
//};


bool isProjectionItemInExpression(const Expression &e, const set<ProjectionItem> &items) {
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
	std::ranges::set_intersection(items, exprItems, std::back_inserter(sharedItems));
	return not sharedItems.empty();
}


// Data-driven Decomposition
vector<graph> graph::project() {
	cout << endl << "projecting." << endl;

	unordered_map<VarIdx, vector<ProjectionItem>> projectionSets;  //TODO: assign more efficiently after populating dependencySets in full
	set<VarIdx> inputChannels, outputChannels;

	set<VarIdx> internalChannels;
	unordered_map<VarIdx, vector<VarIdx>> channelRecvs;  // only internal channels
	unordered_map<VarIdx, vector<VarIdx>> channelSends;  // only internal channels

	//set<VarIdx> recvChannels; //TODO: unordered obvi
	//set<VarIdx> sendChannels; //TODO: unordered obvi
	//auto isInternalChannelRecv = [&channelRecvs](VarIdx var) -> bool {
	//	return std::find(intRecvs.begin(), intRecvs.end(), varIdx) != intRecvs.end();
	//};

	//[this, &channelSends, &channelRecvs, &dep, &inputChannels, &outputChannels](VarIdx varIdx) {
	//	vector<VarIdx> &intRecvs = channelRecvs[dep.first];
	//	vector<VarIdx> &intSends = channelSends[dep.first];
	//	set<VarIdx> &extRecvs = inputChannels;
	//	set<VarIdx> &extSends = outputChannels;
	//	bool isInternalChannelRecv = std::find(intRecvs.begin(), intRecvs.end(), varIdx) != intRecvs.end();
	//	bool isInternalChannelSend = std::find(intSends.begin(), intSends.end(), varIdx) != intSends.end();
	//	bool isExternalChannelRecv = extRecvs.contains(varIdx);
	//	bool isExternalChannelSend = extSends.contains(varIdx);

	//
	// 1) Build Dependency Sets
	//
	//TODO: unordered_sets, obviously
	unordered_map<VarIdx, vector<VarIdx>> dependencySets;  //TODO: vector -> set? t'sin the name bro -- WAIT, they're NOT sets!
	for (TransitionIdx transitionIdx = 0; transitionIdx < this->transitions.size(); transitionIdx++) {
		petri::iterator t_it(transition::type, transitionIdx);
		if (not this->is_valid(t_it)) { continue; }

		// Extract Dependency Set from transition, if there is any
		const chp::transition &tran = this->transitions[transitionIdx];
		vector<VarIdx> guardVars = getVarsFromExpression(tran.guard);

		const arithmetic::Choice &action = tran.action;
		for (const auto &term : action.terms) {
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
					outputChannels.insert(outputChannel);
					cout << " ! CHAN<" << this->vars[outputChannel].name << ">" << endl;

					// Prune redundant self from right-hand side
					rightVars.erase(std::remove(rightVars.begin(), rightVars.end(), outputChannel), rightVars.end());
					dependencySets[outputChannel].insert(dependencySets[outputChannel].begin(), rightVars.begin(), rightVars.end());
					//projectionSets[outputChannel].insert(projectionSets[outputChannel].begin(), rightVars.begin(), rightVars.end());
					projectionSets[outputChannel].insert(projectionSets[outputChannel].begin(), rightVars.begin(), rightVars.end());

					for (VarIdx var : rightVars) {
						if (find(inputChannelsUsed.begin(), inputChannelsUsed.end(), var) != inputChannelsUsed.end()) {
							projectionSets[outputChannel].push_back(ProjectionItem(var, true, false));

						} else {
							projectionSets[outputChannel].push_back(ProjectionItem(var));
						}
					}
					//projectionSets[outputChannel].push_back(outputChannel);
					projectionSets[outputChannel].push_back(ProjectionItem(outputChannel, true, true));
					// aha, these projectionSets need to be overwritten with new variable. Done.

				// Not an output channel, set DependencySet 
				} else {
					//TODO: what if lvalue is a larger expression than just 1 left-var on top? ...or "-x = 3"
					// Assume for now, left-hand is always direct single-assignment
					VarIdx assignedVar = leftVars[0];
					dependencySets[assignedVar].insert(dependencySets[assignedVar].begin(), rightVars.begin(), rightVars.end());
					//projectionSets[assignedVar].insert(projectionSets[assignedVar].begin(), rightVars.begin(), rightVars.end());
					//projectionSets[assignedVar].push_back(assignedVar);
					//projectionSets[assignedVar].insert(projectionSets[assignedVar].begin(), rightVars.begin(), rightVars.end());
					for (VarIdx var : rightVars) {
						if (find(inputChannelsUsed.begin(), inputChannelsUsed.end(), var) != inputChannelsUsed.end()) {
							projectionSets[assignedVar].push_back(ProjectionItem(var, true, false));

						} else {
							projectionSets[assignedVar].push_back(ProjectionItem(var));
						}
					}
					projectionSets[assignedVar].push_back(ProjectionItem(assignedVar));
				}
			}
		}
	}

	//TODO: find less sloppy opportunity to index external input-channels
	// Deduce external input-channels via existence in DependencySets values but not keys
	for (auto const &[target, dependencies] : dependencySets) {
		for (VarIdx dependency : dependencies) {
			if (not dependencySets.contains(dependency)) {
				inputChannels.insert(dependency);
				cout << " ? CHAN<" << this->vars[dependency].name << ">" << endl;
			}
		}
	}

	//TODO: umm, hwat? what was I trying with this transformation? Ahh, was this the previous, independent rendering of dependencySets?
	//for (auto &[aVarIdx, aUseDefChain] : this->useDefChains) {
	//	for (auto &[bVarIdx, bUseDefChain] : this->useDefChains) {
	//		//dependencySets[varIdx].push_back();
	//		if (aVarIdx == bVarIdx) { continue; }

	//		if (std::find(aUseDefChain.uses.begin(), aUseDefChain.uses.end(), bVarIdx) != aUseDefChain.uses.end()) {
	//			dependencySets[bVarIdx].push_back(aVarIdx);
	//		}
	//	}
	//}

	//TODO: print & dehug/verify useDefChains again
	cout << " ~~> ~~> ~~> " << endl;
	std::for_each(dependencySets.begin(), dependencySets.end(), [this](auto &dep) {
			cout << this->vars[dep.first].name << " <- ";
			std::transform(dep.second.begin(), dep.second.end(), ostream_iterator<string>(cout, ", "), [this](VarIdx varIdx) { return this->vars[varIdx].name; });
			cout << endl;
			});

	//TODO: verify Dependency Sets & ensure only channel-Sends are included
	//TODO: this is a great test suite to write
	cout << " ~> ~> ~> ";
	for (auto &[varIdx,v] : dependencySets) { cout << this->vars[varIdx].name << " "; }
	cout << endl;


	//
	// 2) Insert copy variables
	//
	//TODO: perhaps a Mapping<size_t> is more appropriate?
	//TODO: leverage newly introduced TransitionIdx vs VarIdx typedefs for readability
	//  useDef:        def_idx -> <use_idx>
	//  dependencySet: def_var -> <dep_var>
	//  ???:           dep_var -> <def_var>   aHa! inverted/reversed/transposed dependencySet
	//std::unordered_map<size_t, size_t> dependencyUseCounter;
	//for (auto const &[target, dependencies] : dependencySets) {
	//	for (size_t dependency : dependencies) {
	//		dependencyUseCounter[dependency]++;
	//	}
	//}
	set<petri::iterator> umbilicalCords;  //TODO: where best to place this? doesn't matter at all atm, but more legibility will be nice
	std::unordered_map<size_t, set<size_t>> invertedDependencySet;  // value from dependencySet values -> set of keys in dependencySet who CONTAIN/map-to this value
	for (auto const &[target, dependencies] : dependencySets) {
		for (size_t dependency : dependencies) {
			invertedDependencySet[dependency].insert(target);
		}
	}

	cout << " <~~ <~~ <~~ " << endl;
	std::for_each(invertedDependencySet.begin(), invertedDependencySet.end(), [this](auto &dep) {
			cout << this->vars[dep.first].name << " <- ";
			std::transform(dep.second.begin(), dep.second.end(), ostream_iterator<string>(cout, ", "), [this](size_t varIdx) { return this->vars[varIdx].name; });
			cout << endl;
			});

	// Identify multi-use variables that need a copy process to fork their dataflow
	for (auto const &[dependency, users] : invertedDependencySet) {
		size_t useCount = users.size();
		//cout << " __ " << this->vars[dependency].name << ": " << useCount
		//	<< ((useCount > 1) ? "!" : "") << endl;
		if (useCount < 2) { continue; }

		//// Insert new "x_fork := x;" copy-assignment immediately after x assignment
		//// This serves as the base of a fork, splitting/parallelizing out to every use/reference
		VarIdx varForkIdx = this->getEnumeratedVar(dependency, 0, "_fork");
		projectionSets[varForkIdx].push_back(ProjectionItem(varForkIdx));

		//arithmetic::Action forkAssignment;
		//forkAssignment.lvalue = arithmetic::Expression::varOf(varForkIdx);
		//forkAssignment.rvalue = arithmetic::Expression::varOf(dependency);
		//chp::transition forkAssignmentTransition(
		//		arithmetic::Expression::vdd(), arithmetic::Choice({{forkAssignment}}));
		////size_t forkTransitionIdx = this->transitions.insert(forkAssignmentTransition);
		////petri::iterator forkIt(petri::transition::type, forkTransitionIdx);

		////TODO: is this much recalculation needed to retrace definition?
		useDefChain &dependencyUseDefChain = this->useDefChains[dependency];
		if (dependencyUseDefChain.defs.empty()) { continue; }

		size_t defTransitionIdx = dependencyUseDefChain.defs[0];
		////cout << this->transitions[defTransitionIdx] << endl;
		petri::iterator defTransitionIt(petri::transition::type, defTransitionIdx);
		//petri::iterator forkAssignmentIt = this->super::insert_after(defTransitionIt, forkAssignmentTransition);
		////TODO: append useDef after inserting new assignment? nah




		// Insert "this.guard -> CHAN.send(x); x_fork := CHAN.recv()"
		VarIdx forkChannelIdx = this->getEnumeratedVar(dependency, 0, "_FORK_CHAN");
		//projectionSets[dependency].push_back(varForkIdx); //TODO: be careful of renamed x vs x_fork which I'm still ambiguous what I prefer, but I've already remapped everywhere to x_fork wherever appropriate
		cout << endl << "++ " << forkChannelIdx << endl;

		Expression channelSendExpr = arithmetic::call(
				"send",
				{Expression::varOf(forkChannelIdx), Expression::varOf(dependency)} //nope, varForkIdx//TODO: nope, now... dependency//varForkIdx
				);
		cout << "++ +  +> " << channelSendExpr << endl;
		arithmetic::Action forkSend(Expression::undef(), channelSendExpr);
		Expression guard = this->transitions[defTransitionIdx].guard;
		chp::transition forkSendTransition(guard, arithmetic::Choice({{forkSend}}));

		petri::iterator forkSendTransitionIt = this->super::insert_after(defTransitionIt, forkSendTransition);
		projectionSets[dependency].push_back(ProjectionItem(forkChannelIdx, true, true));
		channelSends[dependency].push_back(forkChannelIdx);


		//TODO: make all other arithmetic::Action constructors more legible like this? ... ugh, now too dense, but still somewhat better (ah, use namespaces)
		Expression channelRecvExpr = arithmetic::call("recv", {Expression::varOf(forkChannelIdx)});
		cout << "++ +  <+ " << channelRecvExpr << endl;
		arithmetic::Action forkRecv(Expression::varOf(varForkIdx), channelRecvExpr);
		chp::transition forkRecvTransition(Expression::vdd(), arithmetic::Choice({{forkRecv}}));

		petri::iterator umbilicalCordIt = this->super::insert_after(forkSendTransitionIt, chp::transition());
		umbilicalCords.insert(umbilicalCordIt);
		petri::iterator forkRecvTransitionIt = this->super::insert_after(umbilicalCordIt, forkRecvTransition);
		//internalChannels[transitionIdx] = make_pair(forkSendTransitionIt.index, forkRecvTransitionIt.index);
		projectionSets[varForkIdx].push_back(ProjectionItem(forkChannelIdx, true, false));
		channelRecvs[dependency].push_back(forkChannelIdx);



		//TODO: SLOPPY HACK to get next transition as hook for spawning parallel branchs with the same source & target
		//TODO: not even bound-checked. absolutely disgusting.
		petri::iterator originalUmbilicalCordIt = this->next(forkRecvTransitionIt)[0];
		petri::iterator branchTargetTransitionIt = this->next(originalUmbilicalCordIt)[0];
		//TODO: yikes, this structural decision probably isn't needed. I imagine a recv with multiple parallel outs doesn't fit into this fork. I only considered the next transition, back to the documented approach.

		size_t copyCount = 0;
		for (VarIdx user : users) {
			//for (TransitionIdx dependencyUseTransition : dependencyUseDefChain.uses)
			//VarIdx varCopyIdx = this->getEnumeratedVar(dependency, copyCount, "_branch");
			//cout << "^%$ " << this->vars[varCopyIdx].name << endl;

			//// Insert the "x_cp_n := x_fork" copies
			//arithmetic::Action branchAssignment;
			//branchAssignment.lvalue = arithmetic::Expression::varOf(varCopyIdx);
			//branchAssignment.rvalue = arithmetic::Expression::varOf(varForkIdx);
			//chp::transition branchReassignmentTransition(
			//		arithmetic::Expression::vdd(), arithmetic::Choice({{branchAssignment}}));

			//petri::iterator branchReassignmentTransitionIt = this->super::insert_after(forkAssignmentIt, branchReassignmentTransition);
			////TODO: append useDef after inserting new assignment? nah

			//Mapping<size_t> branchRename(std::numeric_limits<size_t>::max(), true);
			//branchRename.set(dependency, varCopyIdx);

			//chp::transition &branchUseTransition = this->transitions[dependencyUseTransition];
			////TODO: replace all this->transitions lookups with this->at(t_idx) ?? nah, that's for special-purpose term_index
			//branchUseTransition.guard.applyVars(branchRename);

			//arithmetic::Choice &choice = branchUseTransition.action;
			//for (arithmetic::Parallel &term : choice.terms) {
			//	for (arithmetic::Action &action : term.actions) {
			//		action.rvalue.applyVars(branchRename);
			//		//TODO: we actually need to branch on each individual usage INCLUDING multi-use in a single expression!
			//	}
			//}



			// Insert "CHAN.send(x_fork); x_usage_n = CHAN.recv()" internal-communication channels in-place of assignment
			cout << endl << "\\/\\/\\/\\/\\/ multi-dep" << endl;
			//useDefChain &dependencyUseDefChain = this->useDefChains[dependency];
			//if (dependencyUseDefChain.defs.empty()) { continue; }  // no definition when dependency is a recv'd input-channel
			//TransitionIdx defTransitionIdx = dependencyUseDefChain.defs[0];
			//petri::iterator defTransitionIt(petri::transition::type, defTransitionIdx);
			VarIdx channelIdx = this->getEnumeratedVar(dependency, copyCount, "_BRANCH_CHAN");


			arithmetic::Action usageSend;
			arithmetic::Expression channelSendExpr = arithmetic::call(
					"send",
					{arithmetic::Expression::varOf(channelIdx), arithmetic::Expression::varOf(varForkIdx)}
					);
			usageSend.lvalue = arithmetic::Expression::undef();
			usageSend.rvalue = channelSendExpr;
			chp::transition branchSendTransition(
					arithmetic::Expression::vdd(), arithmetic::Choice({{usageSend}}));
			//TODO: certainly not, but quadruple-check that there isn't a guard worth preserving here. Definitely not, but I'm sleepy & prefer over-documenting assumptions


			// Create a new branch off the fork
			size_t branchHeadIdx = this->places.emplace(chp::place());
			petri::iterator branchHeadIt(petri::place::type, branchHeadIdx);
			this->super::connect(forkRecvTransitionIt, branchHeadIt);


			cout << endl << "++ " << channelIdx << endl;
			cout << "++ +  +> " << channelSendExpr << endl;
			petri::iterator branchSendTransitionIt = this->super::insert_after(branchHeadIt, branchSendTransition);
			projectionSets[varForkIdx].push_back(ProjectionItem(channelIdx, true, true));
			channelSends[varForkIdx].push_back(channelIdx);


			arithmetic::Action usageRecv;
			arithmetic::Expression channelRecvExpr = arithmetic::call(
					"recv",
					{arithmetic::Expression::varOf(channelIdx)}
					);
			VarIdx varUsageIdx = this->getEnumeratedVar(dependency, copyCount, "_branch");
			usageRecv.lvalue = arithmetic::Expression::varOf(varUsageIdx);
			usageRecv.rvalue = channelRecvExpr;
			chp::transition branchRecvTransition(
					arithmetic::Expression::vdd(), arithmetic::Choice({{usageRecv}}));

			cout << "++ +  <+ " << channelRecvExpr << endl;

			petri::iterator branchRecvTransitionIt = this->super::insert_after(branchSendTransitionIt, branchRecvTransition);
			petri::iterator branchTailIt = this->next(branchRecvTransitionIt)[0];
			this->super::connect(branchTailIt, branchTargetTransitionIt);
			//internalChannels[transitionIdx] = make_pair(branchSendTransitionIt.index, branchRecvTransitionIt.index);
			projectionSets[user].push_back(ProjectionItem(channelIdx, true, false));
			channelRecvs[user].push_back(channelIdx);



			// Substitute "x_cp_n" for x usages
			Mapping<size_t> usageRename(std::numeric_limits<size_t>::max(), true);
			usageRename.set(dependency, varUsageIdx);
			//TODO: or is it fork-instead-of-dependency that needs to be replaced? triple-check
			for (VarIdx user : users) {
				vector<ProjectionItem> &p = projectionSets[user];
				p.erase(std::remove(p.begin(), p.end(), ProjectionItem(dependency)), p.end());
				p.push_back(ProjectionItem(varUsageIdx));
			}

			////useDefChain &userUseDefChain = this->useDefChains[user];
			////if (userUseDefChain.defs.empty()) { continue; }  //TODO: necessary? Is this the right way or can I just use uses[-]
			//TransitionIdx usageTransitionIdx = this->useDefChains[user].defs[0];
			if (dependencyUseDefChain.uses.empty()) { continue; }
			TransitionIdx use = dependencyUseDefChain.uses[copyCount];  //TODO: sloppy, is this the same branch ordering? I suspect not. This feels like it should do a lookup on the invertedDependency user's definition
			//for (TransitionIdx use : dependencyUseDefChain.uses)
			chp::transition &usageTransition = this->transitions[use]; //usageTransitionIdx
			usageTransition.guard.applyVars(usageRename);

			arithmetic::Choice &choice = usageTransition.action;
			for (arithmetic::Parallel &term : choice.terms) {
				for (arithmetic::Action &action : term.actions) {
					action.rvalue.applyVars(usageRename);
					//TODO: what if there are multiple uses of the same variable within this assignment?
				}
			}

			copyCount++;
		}

		this->super::erase(originalUmbilicalCordIt);
		cout << endl;
	}

	//
	// 3) Insert internal-communication channels
	//
	//unordered_map<VarIdx, vector<VarIdx>> projectionSets;
	//unordered_map<size_t, pair<size_t, size_t>> internalChannels;  // oldAssignmentTransitionIdx -> <sendTransitionIdx, recvTransitionIdx>
	//set<petri::iterator> umbilicalCords;

	//// Crawl for every assignment
	//TODO: traverse more efficiently, ideally combining this operation into previous traversals
	//size_t transitionIdx = 0;
	//for (chp::transition &transition : this->transitions) {
	//	arithmetic::Choice &choice = transition.action;
	//	for (arithmetic::Parallel &term : choice.terms) {
	//		for (arithmetic::Action &action : term.actions) {
	//			if (action.lvalue.isUndef()) { continue; }

	//			// Separate assignment into send+recv over a new internal-only channel
	//			vector<size_t> leftVars = getVarsFromExpression(action.lvalue);
	//			vector<size_t> rightVars = getVarsFromExpression(action.rvalue);
	//			if (leftVars.empty() or rightVars.empty()) { continue; }
	//			//TODO: support multi-assignment [when leftVars.size() > 1]
	//			//TODO: what about constant assignment [when rightVars.empty()]?

	//			petri::iterator thisTransitionIt(petri::transition::type, transitionIdx);
	//			arithmetic::Expression Guard = transition.guard;
	//			arithmetic::Expression LHS = action.lvalue;
	//			arithmetic::Expression RHS = action.rvalue;
	//			size_t channelIdx = this->getEnumeratedVar(leftVars[0], 0, "_CHAN");
	//			//TODO: prevent channel name collision with malicious var name >:)
	//			//TODO: don't do both halves if one is an external communication

	//			// Insert "this.guard -> CHANNEL.send(this.rhs)"
	//			arithmetic::Action internalSend;
	//			arithmetic::Expression channelSendExpr = arithmetic::call(
	//					"send",
	//					{arithmetic::Expression::varOf(channelIdx), RHS}
	//					);
	//			internalSend.lvalue = arithmetic::Expression::undef();
	//			internalSend.rvalue = channelSendExpr;
	//			chp::transition internalSendTransition(
	//				Guard, arithmetic::Choice({{internalSend}}));

	//			cout << endl << "++ " << channelIdx << endl;
	//			cout << "++ +  +> " << channelSendExpr << endl;
	//			petri::iterator internalSendTransitionIt = this->super::insert_after(thisTransitionIt, internalSendTransition);


	//			// Insert "this.lhs := CHANNEL.recv()"
	//			arithmetic::Action internalRecv;
	//			arithmetic::Expression channelRecvExpr = arithmetic::call(
	//					"recv",
	//					{arithmetic::Expression::varOf(channelIdx)}
	//					);
	//			internalRecv.lvalue = LHS;
	//			internalRecv.rvalue = channelRecvExpr;
	//			chp::transition internalRecvTransition(
	//					arithmetic::Expression::vdd(), arithmetic::Choice({{internalRecv}}));

	//			cout << "++ +  <+ " << channelRecvExpr << endl;

	//			petri::iterator umbilicalCordIt = this->super::insert_after(internalSendTransitionIt, chp::transition());
	//			umbilicalCords.insert(umbilicalCordIt);
	//			petri::iterator internalRecvTransitionIt = this->super::insert_after(umbilicalCordIt, internalRecvTransition);
	//			//internalChannels[transitionIdx] = make_pair(internalSendTransitionIt.index, internalRecvTransitionIt.index);
	//		}
	//	}
	//	transitionIdx++;
	//}

	//// Snip the umbilical cords
	//for (auto umbilicalCord : umbilicalCords) {
	//	//this->erase_arc(umbilicalCord, internalSendTransitionIt);
	//	//this->erase_arc(umbilicalCord, internalRecvTransitionIt);
	//	//this->erase_arc(this->in(umbilicalCord)[0]);
	//	//this->erase_arc(this->out(umbilicalCord)[0]);
	//	//auto [inTransitions, outTransitions] = this->super::erase(umbilicalCord);
	//	//this->pinch(umbilicalCord);
	//	this->super::erase(umbilicalCord);
	//	//TODO: Reconnect place-heads & -tails of orphaned straightline programs so they represent petri processes
	//}



	// Identify multi-use variables that need to fork into branching copies
	//TODO: rename "targt, dependencies" now that it's an INVERTED dependency set. More like "dependency, users"
	for (auto const &[dependency, users] : invertedDependencySet) {
		size_t useCount = users.size();  //TODO: OOPS! Should still be useDefCounter, but I just need useDef counter instead of keeping count, to ALSO get a trace back to WHICH depndencySet dependency it is used under, which we need to ultimately trace down WHICH transition is it USED in that needs to be remapped with a copy branch
		cout << " __ " << this->vars[dependency].name << ": " << useCount
			<< ((useCount > 1) ? "!" : "") << endl;
		if (useCount != 1) { continue; }
		VarIdx user = *users.begin(); //users[0];


		// Insert "CHAN.send(x); x_usage_n = CHAN.recv()" after definition
		cout << endl << "\\/\\/\\/\\/\\/\\/\\/\\/ single-dep R7->8" << endl;
		useDefChain &dependencyUseDefChain = this->useDefChains[dependency];
		if (dependencyUseDefChain.defs.empty()) { continue; }  // no definition when dependency is a recv'd input-channel

		TransitionIdx defTransitionIdx = dependencyUseDefChain.defs[0];
		petri::iterator defTransitionIt(petri::transition::type, defTransitionIdx);
		VarIdx channelIdx = this->getEnumeratedVar(dependency, 0, "_LONE_CHAN");


		arithmetic::Action usageSend;
		arithmetic::Expression channelSendExpr = arithmetic::call(
				"send",
				{arithmetic::Expression::varOf(channelIdx), arithmetic::Expression::varOf(dependency)}
				);
		usageSend.lvalue = arithmetic::Expression::undef();
		usageSend.rvalue = channelSendExpr;
		chp::transition internalSendTransition(
				arithmetic::Expression::vdd(), arithmetic::Choice({{usageSend}}));

		cout << endl << "++ " << channelIdx << endl;
		cout << "++ +  +> " << channelSendExpr << endl;
		petri::iterator internalSendTransitionIt = this->super::insert_after(defTransitionIt, internalSendTransition);
		projectionSets[dependency].push_back(ProjectionItem(channelIdx, true, true));
		channelSends[dependency].push_back(channelIdx);


		arithmetic::Action usageRecv;
		arithmetic::Expression channelRecvExpr = arithmetic::call(
				"recv",
				{arithmetic::Expression::varOf(channelIdx)}
				);
		VarIdx varUsageIdx = this->getEnumeratedVar(dependency, 0, "_lone");
		usageRecv.lvalue = arithmetic::Expression::varOf(varUsageIdx);
		usageRecv.rvalue = channelRecvExpr;
		chp::transition internalRecvTransition(
				arithmetic::Expression::vdd(), arithmetic::Choice({{usageRecv}}));

		cout << "++ +  <+ " << channelRecvExpr << endl;

		petri::iterator umbilicalCordIt = this->super::insert_after(internalSendTransitionIt, chp::transition());
		umbilicalCords.insert(umbilicalCordIt);
		petri::iterator internalRecvTransitionIt = this->super::insert_after(umbilicalCordIt, internalRecvTransition);
		//internalChannels[transitionIdx] = make_pair(internalSendTransitionIt.index, internalRecvTransitionIt.index);
		projectionSets[user].push_back(ProjectionItem(channelIdx, true, false));
		channelRecvs[user].push_back(channelIdx);


		// Substitute "x_usage_n" for x in usage
		Mapping<size_t> usageRename(std::numeric_limits<size_t>::max(), true);
		usageRename.set(dependency, varUsageIdx);
		for (VarIdx user : users) {
			vector<ProjectionItem> &p = projectionSets[user];
			p.erase(std::remove(p.begin(), p.end(), ProjectionItem(dependency)), p.end());
			p.push_back(ProjectionItem(varUsageIdx));
		}

		//useDefChain &userUseDefChain = this->useDefChains[user];
		////TODO: oops, this below filters out channels (
		//if (userUseDefChain.defs.empty()) { continue; }  //TODO: necessary? Is this the right way or can I just use uses[-]
		//TransitionIdx usageTransitionIdx = this->useDefChains[user].defs[0];
		if (dependencyUseDefChain.uses.empty()) { continue; }
		for (TransitionIdx use : dependencyUseDefChain.uses) { //TODO: shouldn't this always be .size()==1? It's a lone var? Maybe used in guard of a non-assignment!
			chp::transition &usageTransition = this->transitions[use]; //usageTransitionIdx];
			usageTransition.guard.applyVars(usageRename);

			arithmetic::Choice &choice = usageTransition.action;
			for (arithmetic::Parallel &term : choice.terms) {
				for (arithmetic::Action &action : term.actions) {
					action.rvalue.applyVars(usageRename);
					//TODO: what if there are multiple uses of the same variable within this assignment? seems less an issue the more I consider it
				}
			}
		}
	}

	//TODO: very fun, but the snip is not the right approach, the Projection Sets help us white-list what to pick
	//for (petri::iterator umbilicalCord : umbilicalCords) {
	//	this->super::erase(umbilicalCord);
	//}



	//
	// 4) Build Projection Sets
	//
	cout << endl << "  # ## ### < PS> ### ## #  " << endl;
	auto toString = [this](const ProjectionItem &p) -> string {
		return this->vars[p.index].name + (p.isChannel ? (p.isSend ? "!" : "?") : "");
	};

	//std::for_each(projectionSets.begin(), projectionSets.end(), [this, &channelSends, &channelRecvs, &inputChannels, &outputChannels, &toString](auto &dep) {
	std::for_each(projectionSets.begin(), projectionSets.end(), [this, &toString](auto &dep) {
			cout << "  <( " << this->vars[dep.first].name << " )>  ";
			std::transform(dep.second.begin(), dep.second.end(), ostream_iterator<string>(cout, ", "),
					//[this, &channelSends, &channelRecvs, &dep, &inputChannels, &outputChannels, &toString](const ProjectionItem &item) {
					[&toString](const ProjectionItem &item) {
						//vector<VarIdx> &intRecvs = channelRecvs[dep.first];
						//vector<VarIdx> &intSends = channelSends[dep.first];
						//set<VarIdx> &extRecvs = inputChannels;
						//set<VarIdx> &extSends = outputChannels;
						//bool isInternalChannelRecv = std::find(intRecvs.begin(), intRecvs.end(), item) != intRecvs.end();
						//bool isInternalChannelSend = std::find(intSends.begin(), intSends.end(), item) != intSends.end();
						//bool isExternalChannelRecv = extRecvs.contains(item);
						//bool isExternalChannelSend = extSends.contains(item);
						//return this->vars[item].name + (isInternalChannelSend ? "!" : (isInternalChannelRecv ? "?" : "")) + (isExternalChannelSend ? "!" : (isExternalChannelRecv ? "?" : ""));
						return toString(item);
					});
			cout << endl;
			});
	cout << "  # ## ### </PS> ### ## #  " << endl;


	//
	// 5) Project
	//
	vector<chp::graph> processes;

	size_t pid = 0;
	for (const auto& [var, items] : projectionSets) {
		if (pid > 2) { break; }
		chp::graph process = *this;
		process.name += + "_" + this->vars[var].name;
		cout << "extracting: " << process.name << endl;
		vector<TransitionIdx> toDelete;
		set<ProjectionItem> varItemsLookup(items.begin(), items.end());

		//TODO: RETVRN HERE ah, make sure to get send vs recv & internal vs external right
		//TODO: RETVRN HERE now use findInputChannelsInExpression & findOutputChannelsInExpression
		// . ... maybe even find-in-transition helpers? nah.
		//TODO: convert this into a helper

		// Find transitions of a duplicate chp::graph that don't contain any projected component
		for (TransitionIdx transitionIdx = 0; transitionIdx < process.transitions.size(); transitionIdx++) {
			const chp::transition &transition = process.transitions[transitionIdx];
			if (isProjectionItemInExpression(transition.guard, varItemsLookup)) { continue; }

			bool matchFound = false;
			const arithmetic::Choice &action = transition.action;
			for (const arithmetic::Parallel &term : action.terms) {
				for (const arithmetic::Action &action : term.actions) {

					if (isProjectionItemInExpression(action.lvalue, varItemsLookup)) {
						matchFound = true;
						break;
					}
					if (isProjectionItemInExpression(action.rvalue, varItemsLookup)) {
						matchFound = true;
						break;
					}
				}
				if (matchFound) { break; }
			}
			if (matchFound) { continue; }

			toDelete.push_back(transitionIdx);
		}
		cout << "garbage collected: " << toDelete.size() << endl;

		// Prunce components outside the projection
		size_t watchDog = 0;
		for (TransitionIdx transitionIdx : toDelete) {
			petri::iterator irrelevantTransitionIt(petri::transition::type, transitionIdx);
			process.super::pinch(irrelevantTransitionIt);

			if (watchDog > 2) { cout << "woof!" << endl; break; }
			watchDog++;
		}

		processes.push_back(process);
		cout << "extracted." << endl;
		pid++;
	}

	cout << "projected." << endl;
	return processes;
}

}
