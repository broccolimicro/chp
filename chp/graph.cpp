/*
 * graph.cpp
 *
 *  Created on: Feb 2, 2015
 *      Author: nbingham
 */

#include "graph.h"
#include <common/message.h>
#include <common/text.h>

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

transition::transition()
{
	guard = true;
	action.terms.push_back(arithmetic::Parallel());
}

transition::transition(arithmetic::Expression guard, arithmetic::Action assign)
{
	this->guard = guard;
	this->action.terms.push_back(arithmetic::Parallel());
	this->action.terms.back().actions.push_back(assign);
}

transition::transition(arithmetic::Expression guard, arithmetic::Parallel assign)
{
	this->guard = guard;
	this->action.terms.push_back(assign);
}

transition::transition(arithmetic::Expression guard, arithmetic::Choice assign)
{
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

graph::graph()
{
}

graph::~graph()
{

}

chp::transition &graph::at(term_index idx) {
	return transitions[idx.index];
}

arithmetic::Parallel &graph::term(term_index idx) {
	return transitions[idx.index].action.terms[idx.term];
}

void graph::post_process(const ucs::variable_set &variables, bool proper_nesting, bool aggressive) {
	// Handle Reset Behavior
	bool change = true;
	while (change)
	{
		super::reduce(proper_nesting, aggressive);
		change = false;

		for (int i = 0; i < (int)source.size(); i++)
		{
			simulator::super sim(this, source[i]);
			sim.enabled();

			change = false;
			for (int j = 0; j < (int)sim.ready.size() and not change; j++)
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
					source[i].tokens = sim.tokens;
					for (int k = (int)transitions[t.index].action.terms.size()-1; k >= 0; k--) {
						int idx = i;
						if (k > 0) {
							idx = source.size();
							source.push_back(source[i]);
						}

						arithmetic::State guard_action;
						passesGuard(source[idx].encodings, source[idx].encodings, transitions[t.index].guard, &guard_action);
						// TODO(edward.bingham) set up a global encoding and actually simulate the guards
						//source[idx].encodings &= guard_action;

						arithmetic::State local = transitions[t.index].action.terms[k].evaluate(source[idx].encodings);
						arithmetic::State remote = local.remote(variables.get_groups());

						source[idx].encodings = localAssign(source[idx].encodings, remote, true);
					}

					change = true;
				}
			}
		}

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
						arithmetic::State remote = local.remote(variables.get_groups());

						reset[idx].encodings = localAssign(reset[idx].encodings, remote, true);
					}

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
		for (petri::iterator i(transition::type, 0); i < (int)transitions.size() and !change; i++) {
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
							disconnect(petri::iterator(petri::transition::type, k));
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

	// Determine the actual starting location of the tokens given the state information
	if (reset.size() == 0)
		reset = source;
}

void graph::decompose(const ucs::variable_set &variables) {
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

void expand(const ucs::variable_set &variables) {
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
