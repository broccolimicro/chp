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
	actions.push_back(vector<arithmetic::assignment>());
}

transition::transition(arithmetic::expression e)
{
	arithmetic::assignment a;
	a.expr = e;
	a.behavior = arithmetic::assignment::guard;
	this->actions.push_back(vector<arithmetic::assignment>(1, a));
}

transition::transition(arithmetic::assignment a)
{
	this->actions.push_back(vector<arithmetic::assignment>(1, a));
}

transition::transition(vector<arithmetic::assignment> a)
{
	this->actions.push_back(a);
}

transition::transition(vector<vector<arithmetic::assignment> > a)
{
	this->actions = a;
}

transition::~transition()
{

}

transition transition::merge(int composition, const transition &t0, const transition &t1)
{
	transition result;
	if (composition == petri::parallel || composition == petri::sequence)
	{
		for (int i = 0; i < (int)t0.actions.size(); i++)
			for (int j = 0; j < (int)t1.actions.size(); j++)
			{
				result.actions.push_back(vector<arithmetic::assignment>());
				result.actions.back().insert(result.actions.back().end(), t0.actions[i].begin(), t0.actions[i].end());
				result.actions.back().insert(result.actions.back().end(), t1.actions[j].begin(), t1.actions[j].end());
			}
	}
	else if (composition == petri::choice)
	{
		result.actions.insert(result.actions.end(), t0.actions.begin(), t0.actions.end());
		result.actions.insert(result.actions.end(), t1.actions.begin(), t1.actions.end());
	}
	return result;
}

bool transition::mergeable(int composition, const transition &t0, const transition &t1)
{
	return true;
}

bool transition::is_infeasible()
{
	if (actions.size() == 0)
		return true;

	for (int i = 0; i < (int)actions.size(); i++)
	{
		bool feasible = true;
		for (int j = 0; j < (int)actions[i].size() && feasible; j++)
			if (actions[i][j].expr.is_constant())
			{
				arithmetic::value eval = actions[i][j].expr.evaluate(vector<arithmetic::value>());
				feasible = (eval.data != arithmetic::value::invalid);
			}

		if (feasible)
			return false;
	}

	return true;
}

bool transition::is_vacuous()
{
	if (actions.size() == 0)
		return false;

	for (int i = 0; i < (int)actions.size(); i++)
	{
		bool vacuous = true;
		for (int j = 0; j < (int)actions[i].size() && vacuous; j++)
		{
			if (actions[i][j].behavior == arithmetic::assignment::guard && actions[i][j].expr.is_constant())
			{
				arithmetic::value eval = actions[i][j].expr.evaluate(vector<arithmetic::value>());
				vacuous = (eval.data != arithmetic::value::invalid);
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

}
