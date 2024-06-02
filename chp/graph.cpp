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

void graph::post_process(const ucs::variable_set &variables, bool proper_nesting)
{
	for (int i = 0; i < (int)transitions.size(); i++)
		transitions[i].remote_action = transitions[i].local_action.remote(variables.get_groups());

	
}

}
