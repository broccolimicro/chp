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
	local_action.cubes.push_back(arithmetic::cube());
}

transition::transition(arithmetic::expression e)
{
	local_action = arithmetic::cover(e);
}

transition::transition(arithmetic::action a)
{
	local_action.cubes.push_back(arithmetic::cube());
	local_action.cubes.back().actions.push_back(a);
}

transition::transition(arithmetic::cube a)
{
	local_action.cubes.push_back(a);
}

transition::transition(arithmetic::cover a)
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
		for (int i = 0; i < (int)t0.local_action.cubes.size(); i++)
			for (int j = 0; j < (int)t1.local_action.cubes.size(); j++)
			{
				result.local_action.cubes.push_back(arithmetic::cube());
				result.local_action.cubes.back().actions.insert(result.local_action.cubes.back().actions.end(), t0.local_action.cubes[i].actions.begin(), t0.local_action.cubes[i].actions.end());
				result.local_action.cubes.back().actions.insert(result.local_action.cubes.back().actions.end(), t1.local_action.cubes[j].actions.begin(), t1.local_action.cubes[j].actions.end());
			}
	}
	else if (composition == petri::choice)
	{
		result.local_action.cubes.insert(result.local_action.cubes.end(), t0.local_action.cubes.begin(), t0.local_action.cubes.end());
		result.local_action.cubes.insert(result.local_action.cubes.end(), t1.local_action.cubes.begin(), t1.local_action.cubes.end());
	}
	return result;
}

bool transition::mergeable(int composition, const transition &t0, const transition &t1)
{
	return true;
}

bool transition::is_infeasible()
{
	if (local_action.cubes.size() == 0)
		return true;

	for (int i = 0; i < (int)local_action.cubes.size(); i++)
	{
		bool feasible = true;
		for (int j = 0; j < (int)local_action.cubes[i].actions.size() && feasible; j++)
			if (local_action.cubes[i].actions[j].expr.is_constant())
			{
				arithmetic::value eval = local_action.cubes[i].actions[j].expr.evaluate(vector<arithmetic::value>());
				feasible = (eval.data != arithmetic::value::invalid);
			}

		if (feasible)
			return false;
	}

	return true;
}

bool transition::is_vacuous()
{
	if (local_action.cubes.size() == 0)
		return false;

	for (int i = 0; i < (int)local_action.cubes.size(); i++)
	{
		bool vacuous = true;
		for (int j = 0; j < (int)local_action.cubes[i].actions.size() && vacuous; j++)
		{
			if (local_action.cubes[i].actions[j].variable < 0 && local_action.cubes[i].actions[j].channel < 0 && local_action.cubes[i].actions[j].expr.is_constant())
			{
				arithmetic::value eval = local_action.cubes[i].actions[j].expr.evaluate(vector<arithmetic::value>());
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

void graph::post_process(const ucs::variable_set &variables, bool proper_nesting)
{
	for (int i = 0; i < (int)transitions.size(); i++)
		transitions[i].remote_action = transitions[i].local_action.remote(variables.get_groups());

	// Handle Reset Behavior
	bool change = true;
	while (change)
	{
		super::reduce(proper_nesting);

		change = false;
		for (int i = 0; i < (int)source.size(); i++)
		{
			vector<petri::enabled_transition<term_index> > enabled = this->enabled<petri::token, petri::enabled_transition<term_index> >(source[i].tokens, false);

			change = false;
			for (int j = 0; j < (int)enabled.size() && !change; j++)
			{
				bool firable = transitions[enabled[j].index].local_action.cubes.size() <= 1;
				for (int k = 0; k < (int)enabled[j].tokens.size() && firable; k++)
				{
					for (int l = 0; l < (int)arcs[petri::transition::type].size() && firable; l++)
						if (arcs[petri::transition::type][l].to.index == source[i].tokens[enabled[j].tokens[k]].index)
							firable = false;
					for (int l = 0; l < (int)arcs[petri::place::type].size() && firable; l++)
						if (arcs[petri::place::type][l].from.index == source[i].tokens[enabled[j].tokens[k]].index && arcs[petri::place::type][l].to.index != enabled[j].index)
							firable = false;
				}

				if (firable)
				{
					sort(enabled[j].tokens.begin(), enabled[j].tokens.end());
					for (int k = (int)enabled[j].tokens.size()-1; k >= 0; k--)
						source[i].tokens.erase(source[i].tokens.begin() + enabled[j].tokens[k]);

					vector<int> n = next(transition::type, enabled[j].index);
					for (int k = 0; k < (int)n.size(); k++)
						source[i].tokens.push_back(petri::token(n[k]));

					for (int k = (int)transitions[enabled[j].index].local_action.cubes.size()-1; k >= 0; k--)
					{
						if (k > 0)
						{
							source.push_back(source[i]);
							if (transitions[enabled[j].index].behavior == transition::active)
							{
								local_assign(source.back().encodings, transitions[enabled[j].index].local_action.cubes[k], true);
								remote_assign(source.back().encodings, transitions[enabled[j].index].remote_action.cubes[k], true);
							}
							else
								source.back().encodings &= transitions[enabled[j].index].local_action.cubes[k];
						}
						else
						{
							if (transitions[enabled[j].index].behavior == transition::active)
							{
								local_assign(source[i].encodings, transitions[enabled[j].index].local_action.cubes[0], true);
								remote_assign(source[i].encodings, transitions[enabled[j].index].remote_action.cubes[0], true);
							}
							else
								source[i].encodings &= transitions[enabled[j].index].local_action.cubes[0];
						}
					}

					change = true;
				}
			}
		}

		for (int i = 0; i < (int)reset.size(); i++)
		{
			vector<petri::enabled_transition<term_index> > enabled = this->enabled<petri::token, petri::enabled_transition<term_index> >(reset[i].tokens, false);

			change = false;
			for (int j = 0; j < (int)enabled.size() && !change; j++)
			{
				bool firable = transitions[enabled[j].index].local_action.cubes.size() <= 1;
				for (int k = 0; k < (int)enabled[j].tokens.size() && firable; k++)
				{
					for (int l = 0; l < (int)arcs[petri::transition::type].size() && firable; l++)
						if (arcs[petri::transition::type][l].to.index == reset[i].tokens[enabled[j].tokens[k]].index)
							firable = false;
					for (int l = 0; l < (int)arcs[petri::place::type].size() && firable; l++)
						if (arcs[petri::place::type][l].from.index == reset[i].tokens[enabled[j].tokens[k]].index && arcs[petri::place::type][l].to.index != enabled[j].index)
							firable = false;
				}

				if (firable)
				{
					sort(enabled[j].tokens.begin(), enabled[j].tokens.end());
					for (int k = (int)enabled[j].tokens.size()-1; k >= 0; k--)
						reset[i].tokens.erase(reset[i].tokens.begin() + enabled[j].tokens[k]);

					vector<int> n = next(transition::type, enabled[j].index);
					for (int k = 0; k < (int)n.size(); k++)
						reset[i].tokens.push_back(petri::token(n[k]));

					for (int k = (int)transitions[enabled[j].index].local_action.cubes.size()-1; k >= 0; k--)
					{
						if (k > 0)
						{
							reset.push_back(reset[i]);
							if (transitions[enabled[j].index].behavior == transition::active)
							{
								local_assign(reset.back().encodings, transitions[enabled[j].index].local_action.cubes[k], true);
								remote_assign(reset.back().encodings, transitions[enabled[j].index].remote_action.cubes[k], true);
							}
							else
								reset.back().encodings &= transitions[enabled[j].index].local_action.cubes[k];
						}
						else
						{
							if (transitions[enabled[j].index].behavior == transition::active)
							{
								local_assign(reset[i].encodings, transitions[enabled[j].index].local_action.cubes[0], true);
								remote_assign(reset[i].encodings, transitions[enabled[j].index].remote_action.cubes[0], true);
							}
							else
								reset[i].encodings &= transitions[enabled[j].index].local_action.cubes[0];
						}
					}

					change = true;
				}
			}
		}
	}

	if (reset.size() == 0)
		reset = source;
}

}
