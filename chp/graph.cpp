/*
 * graph.cpp
 *
 *  Created on: Feb 2, 2015
 *      Author: nbingham
 */

#include "graph.h"
#include <common/message.h>
#include <common/text.h>

namespace chp
{

transition::transition()
{
	local_action.terms.push_back(arithmetic::parallel());
}

transition::transition(arithmetic::expression e)
{
	local_action = arithmetic::choice(e);
}

transition::transition(arithmetic::action a)
{
	local_action.terms.push_back(arithmetic::parallel());
	local_action.terms.back().actions.push_back(a);
}

transition::transition(arithmetic::parallel a)
{
	local_action.terms.push_back(a);
}

transition::transition(arithmetic::choice a)
{
	local_action = a;
}

transition::~transition()
{

}

transition transition::merge(int composition, const transition &t0, const transition &t1)
{
	transition result;
	if (composition == petri::parallel || composition == petri::sequence)
	{
		for (int i = 0; i < (int)t0.local_action.terms.size(); i++)
			for (int j = 0; j < (int)t1.local_action.terms.size(); j++)
			{
				result.local_action.terms.push_back(arithmetic::parallel());
				result.local_action.terms.back().actions.insert(result.local_action.terms.back().actions.end(), t0.local_action.terms[i].actions.begin(), t0.local_action.terms[i].actions.end());
				result.local_action.terms.back().actions.insert(result.local_action.terms.back().actions.end(), t1.local_action.terms[j].actions.begin(), t1.local_action.terms[j].actions.end());
			}
	}
	else if (composition == petri::choice)
	{
		result.local_action.terms.insert(result.local_action.terms.end(), t0.local_action.terms.begin(), t0.local_action.terms.end());
		result.local_action.terms.insert(result.local_action.terms.end(), t1.local_action.terms.begin(), t1.local_action.terms.end());
	}
	return result;
}

bool transition::mergeable(int composition, const transition &t0, const transition &t1)
{
	return true;
}

bool transition::is_infeasible()
{
	if (local_action.terms.size() == 0)
		return true;

	for (int i = 0; i < (int)local_action.terms.size(); i++)
	{
		bool feasible = true;
		for (int j = 0; j < (int)local_action.terms[i].actions.size() && feasible; j++)
			if (local_action.terms[i].actions[j].expr.is_constant())
			{
				arithmetic::value eval = local_action.terms[i].actions[j].expr.evaluate(arithmetic::state());
				feasible = (eval.data != arithmetic::value::neutral);
			}

		if (feasible)
			return false;
	}

	return true;
}

bool transition::is_vacuous()
{
	if (local_action.terms.size() == 0)
		return false;

	for (int i = 0; i < (int)local_action.terms.size(); i++)
	{
		bool vacuous = true;
		for (int j = 0; j < (int)local_action.terms[i].actions.size() && vacuous; j++)
		{
			if (local_action.terms[i].actions[j].variable < 0 && local_action.terms[i].actions[j].channel < 0 && local_action.terms[i].actions[j].expr.is_constant())
			{
				arithmetic::value eval = local_action.terms[i].actions[j].expr.evaluate(arithmetic::state());
				vacuous = (eval.data != arithmetic::value::neutral);
			}
			else
				vacuous = false;
		}

		if (vacuous)
			return true;
	}

	return false;
}

graph::graph()
{
}

graph::~graph()
{

}

void graph::post_process(const ucs::variable_set &variables, bool proper_nesting) {
	for (int i = 0; i < (int)transitions.size(); i++) {
		transitions[i].remote_action = transitions[i].local_action.remote(variables.get_groups());
	}
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

}
