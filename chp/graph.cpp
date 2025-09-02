#include "graph.h"

#include <arithmetic/expression.h>
#include <common/message.h>
#include <common/text.h>
#include <common/mapping.h>
#include <chp/simulator.h>
#include <interpret_arithmetic/export.h>
#include <queue>

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
	guard = arithmetic::Expression::boolOf(true);
	action = true;
}

transition::transition(arithmetic::Expression guard, arithmetic::Choice assign) {
	this->guard = guard;
	this->action = assign;
}

transition::~transition()
{

}

transition transition::merge(int composition, const transition &t0, const transition &t1)
{
	transition result;
	if (composition == petri::parallel or composition == petri::sequence)
	{
		for (int i = 0; i < (int)t0.action.terms.size(); i++)
			for (int j = 0; j < (int)t1.action.terms.size(); j++)
			{
				result.action.terms.push_back(arithmetic::Parallel());
				result.action.terms.back().actions.insert(result.action.terms.back().actions.end(), t0.action.terms[i].actions.begin(), t0.action.terms[i].actions.end());
				result.action.terms.back().actions.insert(result.action.terms.back().actions.end(), t1.action.terms[j].actions.begin(), t1.action.terms[j].actions.end());
			}
	}
	else if (composition == petri::choice)
	{
		result.action.terms.insert(result.action.terms.end(), t0.action.terms.begin(), t0.action.terms.end());
		result.action.terms.insert(result.action.terms.end(), t1.action.terms.begin(), t1.action.terms.end());
	}
	return result;
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
		if (not g.transitions.is_valid(i)) continue;

		g.transitions[i].action.apply(netMap);
		g.transitions[i].guard.apply(netMap);
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
			if (not is_valid(i)) continue;

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
			if (not is_valid(i)) continue;

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
			if (not is_valid(i)) continue;

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
	if (debug) { cout << "¿Yµ wWµøT?" << endl; }

	if (!this->split_groups_ready) {
		this->compute_split_groups();
	}

	// Place index of split -> transition indices
	//TODO: replace indices w/ iterators
	std::map<int, std::set<int>> split_places;

	// TODO: extract this directly from graph, which was already traversed
	for (size_t transition_idx = 0; transition_idx < this->transitions.size(); transition_idx++) {
		if (not this->transitions.is_valid(transition_idx)) continue;
		//const petri::transition &transition = this->transitions[transition_idx];

		// Identify split groups
		petri::iterator it(petri::transition::type, (int)transition_idx);
		const vector<petri::split_group> &new_split_groups = this->split_groups_of(
				petri::composition::choice, it);

		if (!new_split_groups.empty()) {
			for (const petri::split_group &split_group : new_split_groups) {

				if (!split_group.branch.empty()) {
					split_places[split_group.split].insert(split_group.branch[0]);

				} else {
					cerr << "ERROR: a split_group with no branches! " << split_group.to_string() << endl;
				}
			}
		}
	}
	//TODO: simpler implementation: traverse all places & filter for those with next()'s > 1
	//TODO: Is my earlier "split_places" definition at least more performant? Prioritize readability

	if (split_places.empty()) {
		//TODO: what if no splits? already flattened? Brainstorm example
		// aha! e.g. see ds_adder_flat where shared transitions need to be duplicated (s & co assignment)
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
	set<petri::iterator> visited;
	visited.insert(start);
	while (start != dominator) {

		petri::iterator next = this->unzip_forwards(start);
		if (debug) { cout << next.to_string() << " "; }

		if (visited.contains(next)) { break; }  // Detect cycles
		if (visited.size() > 64) { break; }  //HACK: detect runaway bug
		visited.insert(next);
		start = next;
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

	if (debug) { cout << "¡Yµ wWµøT!" << endl; }
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

}
