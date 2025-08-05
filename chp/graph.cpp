#include "graph.h"
#include <common/message.h>
#include <common/text.h>
#include <common/mapping.h>

#include <chp/simulator.h>

#include <interpret_arithmetic/export.h>

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
	uid = -1;
	// initialize as a skip
	expr = arithmetic::Operation(arithmetic::Operation::IDENTITY, {arithmetic::Operand::boolOf(true)});
}

transition::transition(int uid, arithmetic::Operation expr) {
	this->uid = uid;
	this->expr = expr;
}

transition::~transition()
{

}

transition transition::merge(int composition, const transition &t0, const transition &t1) {
	if (t0.is_vacuous()) {
		return t1;
	} else if (t1.is_vacuous()) {
		return t0;
	}
	return transition();
}

bool transition::mergeable(int composition, const transition &t0, const transition &t1) {
	return t0.is_vacuous() or t1.is_vacuous();
}

bool transition::is_infeasible() const {
	return expr.func == arithmetic::Operation::IDENTITY
		and expr.operands.size() == 1u
		and expr.operands[0].isConst()
		and expr.operands[0].cnst.isNeutral();
}

bool transition::is_vacuous() const {
	return expr.func == arithmetic::Operation::IDENTITY
		and expr.operands.size() == 1u
		and expr.operands[0].isConst()
		and expr.operands[0].cnst.isValid();
}

ostream &operator<<(ostream &os, const transition &t) {
	os << t.expr << " -> v" << t.uid << "=" << t.expr.op();
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

vector<arithmetic::Operand> graph::exprIndex() const {
	vector<arithmetic::Operand> result;
	for (auto i = transitions.begin(); i != transitions.end(); i++) {
		if (not i->expr.isUndef()) {
			result.push_back(i->expr.op());
		}
	}
	return result;
}

// I need an efficient way to map expressions to transitions
// that can be updated...
const arithmetic::Operation *graph::getExpr(size_t index) const {
	// TODO(edward.bingham) We need to use index_vector in petri before we can do this
	// return &transitions[index].expr;

	for (auto i = transitions.begin(); i != transitions.end(); i++) {
		if (i->expr.exprIndex == index) {
			return &(i->expr);
		}
	}
	return nullptr;
}

bool graph::setExpr(arithmetic::Operation o) {
	for (auto i = transitions.begin(); i != transitions.end(); i++) {
		if (i->expr.exprIndex == o.exprIndex) {
			i->expr = o;
			return true;
		}
	}
	return false;

	// TODO(edward.bingham) we need to use index_vector
	/*if (o.exprIndex >= transitions.size()) {
		// TODO(edward.bingham) create a new transition
	}
	transitions[o.exprIndex].expr = o;
	return true;*/
}

arithmetic::Operand graph::pushExpr(arithmetic::Operation o) {
	// TODO(edward.bingham) we need to use index_vector
	o.exprIndex = transitions.size();
	transitions.push_back(transition(-1, o));
	return o.op();
}

bool graph::eraseExpr(size_t index) {
	// TODO(edward.bingham) we need to use index_vector
	for (int i = 0; i < (int)transitions.size(); i++) {
		if (transitions[i].expr.exprIndex == index) {
			pinch(petri::iterator(transition::type, i));
			return true;
		}
	}
	return false;
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

petri::mapping graph::merge(graph g) {
	mapping netMap((int)g.vars.size());

	// Add all of the vars and look for duplicates
	int count = (int)vars.size();
	for (int i = 0; i < (int)g.vars.size(); i++) {
		netMap.nets[i] = (int)vars.size();
		vector<int> remote;
		for (int j = 0; j < count; j++) {
			if (vars[j].name == g.vars[i].name) {
				if (vars[j].region == g.vars[i].region) {
					netMap.nets[i] = j;
				}
				remote.push_back(j);
			}
		}

		if (netMap.nets[i] >= (int)vars.size()) {
			vars.push_back(g.vars[i]);
			vars.back().remote = remote;
		}
	}

	// Fill in the remote vars
	for (int i = 0; i < (int)netMap.nets.size(); i++) {
		int k = netMap.nets[i];
		for (int j = 0; j < (int)g.vars[i].remote.size(); j++) {
			vars[k].remote.push_back(netMap.map(g.vars[i].remote[j]));
		}
		sort(vars[k].remote.begin(), vars[k].remote.end());
		vars[k].remote.erase(unique(vars[k].remote.begin(), vars[k].remote.end()), vars[k].remote.end());
	}

	for (int i = 0; i < (int)g.transitions.size(); i++) {
		g.transitions[i].expr.apply(netMap);
	}

	// Remap all expressions to new vars
	return super::merge(g);
}


void graph::post_process(bool proper_nesting, bool aggressive) {
	// Handle Reset Behavior
	bool change = true;
	/*while (change)
	{
		super::reduce(proper_nesting, aggressive);
		change = false;

		for (int i = 0; i < (int)reset.size(); i++)
		{
			simulator::super sim(this, reset[i]);
			sim.enabled();

			change = false;
			for (int j = 0; j < (int)sim.ready.size() and !change; j++)
			{
				bool firable = transitions[sim.ready[j].index].action.terms.size() <= 1;
				for (int k = 0; k < (int)sim.ready[j].tokens.size() and firable; k++)
				{
					for (int l = 0; l < (int)arcs[petri::transition::type].size() and firable; l++)
						if (arcs[petri::transition::type][l].to.index == sim.tokens[sim.ready[j].tokens[k]].index)
							firable = false;
					for (int l = 0; l < (int)arcs[petri::place::type].size() and firable; l++)
						if (arcs[petri::place::type][l].from.index == sim.tokens[sim.ready[j].tokens[k]].index and arcs[petri::place::type][l].to.index != sim.ready[j].index)
							firable = false;
				}

				if (firable) {
					petri::enabled_transition t = sim.fire(j);
					reset[i].tokens = sim.tokens;
					for (int k = (int)transitions[t.index].action.terms.size()-1; k >= 0; k--) {
						int idx = i;
						if (k > 0) {
							idx = reset.size();
							reset.push_back(reset[i]);
						}

						arithmetic::State guard_action;
						passesGuard(reset[idx].encodings, reset[idx].encodings, transitions[t.index].guard, &guard_action);
						// TODO(edward.bingham) set up a global encoding and actually simulate the guards
						// reset[idx].encodings &= guard_action;

						arithmetic::State local = transitions[t.index].action.terms[k].evaluate(reset[idx].encodings);
						arithmetic::State remote = local.remote(remote_groups());

						reset[idx].encodings = localAssign(reset[idx].encodings, remote, true);
					}

					change = true;
				}
			}
		}
	}*/

	change = true;
	while (change) {
		super::reduce(proper_nesting, aggressive);

		// Remove skips
		change = false;
		for (petri::iterator i(transition::type, 0); i < (int)transitions.size() and not change; i++) {
			if (transitions[i.index].is_vacuous()) {
				vector<petri::iterator> n = next(i); // places
				if (n.size() > 1) {
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
		/*for (petri::iterator i(place::type, 0); i < (int)places.size() and !change; i++) {
			vector<petri::iterator> p = prev(i);
			vector<petri::iterator> active, passive;
			for (int k = 0; k < (int)p.size(); k++) {
				if (transitions[p[k].index].action.is_passive()) {
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
							disconnect(petri::iterator(petri::transition::type, l));
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
			continue;

		for (petri::iterator i(transition::type, 0); i < (int)transitions.size() and !change; i++) {
			if (transitions[i.index].action.is_passive()) {
				vector<petri::iterator> nn = next(next(i)); // transitions
				for (int l = 0; l < (int)nn.size(); l++) {
					transitions[nn[l].index] = transition::merge(petri::sequence, transitions[i.index], transitions[nn[l].index]);
				}

				pinch(i);
				change = true;
			}
		}
		if (change)
			continue;*/
	}
}

void graph::decompose() {
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
}

void expand() {
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

arithmetic::Expression graph::exclusion(int index) const {
	// TODO(edward.bingham) fix after rearch
	arithmetic::Expression result;
	/*vector<int> p = prev(transition::type, index);
	
	for (int i = 0; i < (int)p.size(); i++) {
		vector<int> n = next(place::type, p[i]);
		if (n.size() > 1) {
			for (int j = 0; j < (int)n.size(); j++) {
				if (n[j] != index) {
					result = result | transitions[n[j]].expr;
				}
			}
		}
	}*/
	return result;
}

}
