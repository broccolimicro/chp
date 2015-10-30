/*
 * simulator.cpp
 *
 *  Created on: Apr 28, 2015
 *      Author: nbingham
 */

#include "simulator.h"
#include "graph.h"
#include <common/text.h>
#include <common/message.h>
#include <ucs/variable.h>


namespace chp
{

instability::instability()
{
}

instability::instability(const enabled_transition &cause) : enabled_transition(cause)
{
}

instability::~instability()
{

}

string instability::to_string(const chp::graph &g, const ucs::variable_set &v)
{
	string result;
	result = "unstable transition " + enabled_transition::to_string(g, v);

	result += " cause: {";

	for (int j = 0; j < (int)history.size(); j++)
	{
		if (j != 0)
			result += "; ";

		result += history[j].to_string(g, v);
	}
	result += "}";
	return result;
}

interference::interference()
{

}

interference::interference(const term_index &first, const term_index &second)
{
	if (first < second)
	{
		this->first = first;
		this->second = second;
	}
	else
	{
		this->first = second;
		this->second = first;
	}
}

interference::~interference()
{

}

string interference::to_string(const chp::graph &g, const ucs::variable_set &v)
{
	return "interfering assignments " + first.to_string(g, v) + " and " + second.to_string(g, v);
}

mutex::mutex()
{

}

mutex::mutex(const enabled_transition &first, const enabled_transition &second)
{
	if (first < second)
	{
		this->first = first;
		this->second = second;
	}
	else
	{
		this->first = second;
		this->second = first;
	}
}

mutex::~mutex()
{

}

string mutex::to_string(const chp::graph &g, const ucs::variable_set &v)
{
	return "vacuous firings break mutual exclusion for assignments " + first.to_string(g, v) + " and " + second.to_string(g, v);
}

deadlock::deadlock()
{

}

deadlock::deadlock(const state &s) : state(s)
{

}

deadlock::~deadlock()
{

}

string deadlock::to_string(const ucs::variable_set &v)
{
	return "deadlock detected at state " + state::to_string(v);
}

simulator::simulator()
{
	base = NULL;
	variables = NULL;
}

simulator::simulator(const graph *base, const ucs::variable_set *variables, state initial, int index, bool environment)
{
	//cout << "Reset" << endl;
	this->base = base;
	this->variables = variables;
	if (base != NULL)
	{
		encoding = initial.encodings[index];
		global = initial.encodings[index];
		for (int k = 0; k < (int)initial.tokens.size(); k++)
			local.tokens.push_back(initial.tokens[k]);
	}
}

simulator::~simulator()
{

}

/** enabled()
 *
 * Returns a vector of indices representing the transitions
 * that this marking enabled and the term of each transition
 * that's enabled.
 */
int simulator::enabled(bool sorted)
{
	if (base == NULL)
	{
		internal("", "NULL pointer to simulator::base", __FILE__, __LINE__);
		return 0;
	}

	/*cout << "local:{";
	for (int i = 0; i < (int)local.tokens.size(); i++)
	{
		if (i != 0)
			cout << " ";
		cout << local.tokens[i].index;
	}
	cout << "} head:{";
	for (int i = 0; i < (int)remote.head.size(); i++)
	{
		if (i != 0)
			cout << " ";
		cout << remote.head[i].index;
	}
	cout << "} tail:{";
	for (int i = 0; i < (int)remote.tail.size(); i++)
	{
		if (i != 0)
			cout << " ";
		cout << remote.tail[i];
	}
	cout << "} body:{";
	for (int i = 0; i < (int)remote.body.size(); i++)
	{
		if (i != 0)
			cout << " ";
		cout << remote.body[i].to_string(*base, *variables);
	}
	cout << "} " << export_assignment(encoding, *variables).to_string() << "\t" << export_assignment(global, *variables).to_string() << endl;*/


	if (local.tokens.size() == 0)
		return 0;

	bool done = false;
	do
	{

	// Get the list of transitions have have a sufficient number of local at the input places
	vector<enabled_transition> loaded = base->enabled<local_token, enabled_transition>(local.tokens, sorted);



	} while (!done);

	// Now we need to check the guards of all of the loaded transitions against the state
	vector<enabled_transition> potential;
	potential.reserve(loaded.size()*2);
	for (int i = 0; i < (int)loaded.size(); i++)
	{
		// If this transition is an assignment, then we need to check to make sure all of the
		// previous guards pass before we can execute this assignment.
		for (int j = 0; j < (int)loaded[i].tokens.size(); j++)
			loaded[i].guard = (loaded[i].guard && local.tokens[loaded[i].tokens[j]].guard);

		for (int j = 0; j < (int)base->transitions[loaded[i].index].actions.size(); j++)
			for (int k = 0; k < (int)base->transitions[loaded[i].index].actions[j].size(); k++)
		if (base->transitions[loaded[i].index].behavior == transition::passive)
			loaded[i].guard &= base->transitions[loaded[i].index].local_action;

		// Now we check to see if the current state passes the guard
		/*cout << encoding << endl;
		cout << global << endl;
		cout << loaded[i].guard << endl;*/
		int pass = boolean::passes_guard(encoding, global, loaded[i].guard, &loaded[i].guard_action);
		//cout << pass << endl;

		if (base->transitions[loaded[i].index].behavior == transition::active)
			for (loaded[i].term = 0; loaded[i].term < base->transitions[loaded[i].index].local_action.size(); loaded[i].term++)
			{
				loaded[i].local_action = base->transitions[loaded[i].index].local_action[loaded[i].term];
				loaded[i].remote_action = base->transitions[loaded[i].index].remote_action[loaded[i].term];

				// If this transition is vacuous then its enabled even if its incoming guards are not firing
				// This is global and not local for the following reason: if there are two equivalent assignments
				// on the same net: x'0+ and x'1+ such that x'0+ happens first and x'1+ happens before it can observe
				// the value on x'0, is x'1+ vacuous? I assume yes because the two assignments are not differentiable in
				// the event rule system.
				loaded[i].vacuous = base->transitions[loaded[i].index].behavior == transition::active &&
									vacuous_assign(global, loaded[i].remote_action, loaded[i].stable);

				if (pass >= 0 || loaded[i].vacuous)
				{
					// If it does, then this transition may fire.
					loaded[i].stable = true;
					if (!loaded[i].vacuous)
						loaded[i].stable = (bool)pass;

					potential.push_back(loaded[i]);
				}
			}
		else if (pass >= 0)
		{
			loaded[i].stable = (bool)pass;
			potential.push_back(loaded[i]);
		}
	}

	// Check for unstable transitions
	int i = 0, j = 0;
	while (i < (int)local.ready.size())
	{
		if (j >= (int)potential.size() || (term_index)local.ready[i] < (term_index)potential[j])
		{
			local.ready[i].stable = false;
			potential.insert(potential.begin() + j, local.ready[i]);
			i++;
			j++;
		}
		else if ((term_index)potential[j] < (term_index)local.ready[i])
			j++;
		else
		{
			potential[j].history = local.ready[i].history;
			i++;
			j++;
		}
	}

	if (last.index >= 0)
		for (int i = 0; i < (int)potential.size(); i++)
			potential[i].history.push_back(last);

	local.ready = potential;

	for (int i = 0; i < (int)local.ready.size(); i++)
		for (int j = i+1; j < (int)local.ready.size(); j++)
			if (local.ready[i].vacuous && local.ready[j].vacuous && (local.ready[i].index == local.ready[j].index || vector_intersection_size(local.ready[i].tokens, local.ready[j].tokens) > 0))
			{
				mutex err(local.ready[i], local.ready[j]);
				vector<mutex>::iterator loc = lower_bound(mutex_errors.begin(), mutex_errors.end(), err);
				if (loc == mutex_errors.end() || *loc != err)
				{
					mutex_errors.insert(loc, err);
					warning("", err.to_string(*base, *variables), __FILE__, __LINE__);
				}
			}

	// Now in the production rule simulator, here is where I would automatically execute all of the
	// vacuous transitions, but that leads to some really strange (incorrect?) behavior in an HSE.

	// For example, we might take both paths of a conditional. Then I would have to add in a bunch of extra stuff
	// to handle conditional merges. Like if two tokens happen to be on the same place, then they are merged
	// into one. Luckily, since most conditional splits are guarded, this is unlikely to happen.

	// Unless... I would have to check the next assignment, skipping past any guards. since the assumption is the
	// execution of the assignment stores all of the information about any previous guards. (this is the weakest
	// assumption I could make about state encoding. If I made no assumption, about what is needed to encode
	// a state, then I would not be able to simulate instability). The thing is that most HSE will be resistant
	// to this because they don't use the previous assignment to entirely encode the next state.

	// So for now, I'm going to just mark them vacuous and let whoever is managing the simulator handle them.
	// That might be to execute all of the vacuous firings first in a random order (this would prevent taking
	// two branches of a conditional simultaneously). Or it could be to try and handle it correctly.

	// TODO I don't know how I would treat the case when the execution enters a state that is not part of the HSE.
	// In this case, all of the transitions in the entire HSE are vacuous (possibly unstable) and the tokens
	// could just zoom around the HSE making no changes to the state information.

	return local.ready.size();
}

enabled_transition simulator::fire(int index)
{
	if (base == NULL)
	{
		internal("", "NULL pointer to simulator::base", __FILE__, __LINE__);
		return enabled_transition();
	}

	enabled_transition t = local.ready[index];

	//cout << "Fire " << t.to_string(*base, *variables) << " " << t.vacuous << " " << t.stable << endl;

	// disable any transitions that were dependent on at least one of the same local tokens
	// This is only necessary to check for instability_errors transitions in the enabled() function
	for (int i = (int)local.ready.size()-1; i >= 0; i--)
		if (vector_intersection_size(local.ready[i].tokens, t.tokens) > 0)
			local.ready.erase(local.ready.begin() + i);

	// Check to see if this transition is unstable
	if (!t.stable)
	{
		instability err = instability(t);
		vector<instability>::iterator loc = lower_bound(instability_errors.begin(), instability_errors.end(), err);
		if (loc == instability_errors.end() || *loc != err)
		{
			instability_errors.insert(loc, err);
			error("", err.to_string(*base, *variables), __FILE__, __LINE__);
		}
	}

	// Update the local.tokens
	vector<int> next = base->next(petri::transition::type, t.index);
	vector<enabled_transition> prev;
	sort(t.tokens.rbegin(), t.tokens.rend());
	for (int i = 0; i < (int)t.tokens.size(); i++)
	{
		// Keep the other enabled transition tokens up to date
		for (int j = 0; j < (int)local.ready.size(); j++)
			for (int k = 0; k < (int)local.ready[j].tokens.size(); k++)
				if (local.ready[j].tokens[k] > t.tokens[i])
					local.ready[j].tokens[k]--;

		prev.insert(prev.end(), local.tokens[t.tokens[i]].prev.begin(), local.tokens[t.tokens[i]].prev.end());
		local.tokens.erase(local.tokens.begin() + t.tokens[i]);
	}

	// Check for interfering transitions. Interfering transitions are the active transitions that have fired since this
	// active transition was enabled.
	if (base->transitions[t.index].behavior == transition::active)
		for (int j = 0; j < (int)t.history.size(); j++)
			if (base->transitions[t.history[j].index].behavior == transition::active)
			{
				if (boolean::are_mutex(base->transitions[t.index].remote_action[t.term], base->transitions[t.history[j].index].local_action[t.history[j].term]))
				{
					interference err(t, t.history[j]);
					vector<interference>::iterator loc = lower_bound(interference_errors.begin(), interference_errors.end(), err);
					if (loc == interference_errors.end() || *loc != err)
					{
						interference_errors.insert(loc, err);
						error("", err.to_string(*base, *variables), __FILE__, __LINE__);
					}
				}

				t.local_action = boolean::interfere(t.local_action, base->transitions[t.history[j].index].remote_action[t.history[j].term]);
				t.remote_action = boolean::interfere(t.remote_action, base->transitions[t.history[j].index].remote_action[t.history[j].term]);
			}

	// Update the state
	arithmetic::expression guard = t.guard;
	if (base->transitions[t.index].behavior == transition::active)
	{
		/*cout << "Before" << endl;
		cout << "Global " << global << endl;
		cout << "Encoding " << encoding << endl;
		cout << "Guard " << t.guard_action << endl;
		cout << "Local " << t.local_action << endl;
		cout << "Remote " << t.remote_action << endl;
		cout << "Stable " << t.stable << endl;
		cout << "Vacuous " << t.vacuous << endl;*/
		if (t.stable && !t.vacuous)
		{
			global &= t.guard_action;
			encoding &= t.guard_action;
		}

		local_assign(global, t.remote_action, t.stable);
		local_assign(encoding, t.local_action, t.stable);
		remote_assign(encoding, global, true);

		/*cout << "After" << endl;
		cout << "Global " << global << endl;
		cout << "Encoding " << encoding << endl;*/

		guard = base->transitions[t.index].local_action;

		prev.clear();
		prev.push_back(t);
	}
	else if (base->transitions[t.index].behavior == transition::passive)
	{
		if (t.stable)
		{
			global &= t.guard_action;
			encoding &= t.guard_action;
		}

		encoding = remote_assign(encoding, global, true);
	}

	for (int i = 0; i < (int)next.size(); i++)
		local.tokens.push_back(local_token(next[i], guard, prev));

	last = t;
	return t;
}

void simulator::merge_errors(const simulator &sim)
{
	if (instability_errors.size() > 0)
	{
		vector<instability> old_instability_errors;
		swap(instability_errors, old_instability_errors);
		instability_errors.resize(old_instability_errors.size() + sim.instability_errors.size());
		merge(sim.instability_errors.begin(), sim.instability_errors.end(), old_instability_errors.begin(), old_instability_errors.end(), instability_errors.begin());
		instability_errors.resize(unique(instability_errors.begin(), instability_errors.end()) - instability_errors.begin());
	}
	else
		instability_errors = sim.instability_errors;

	if (interference_errors.size() > 0)
	{
		vector<interference> old_interference_errors;
		swap(interference_errors, old_interference_errors);
		interference_errors.resize(sim.interference_errors.size() + old_interference_errors.size());
		merge(sim.interference_errors.begin(), sim.interference_errors.end(), old_interference_errors.begin(), old_interference_errors.end(), interference_errors.begin());
		interference_errors.resize(unique(interference_errors.begin(), interference_errors.end()) - interference_errors.begin());
	}
	else
		interference_errors = sim.interference_errors;

	if (mutex_errors.size() > 0)
	{
		vector<mutex> old_mutex_errors;
		swap(mutex_errors, old_mutex_errors);
		mutex_errors.resize(sim.mutex_errors.size() + old_mutex_errors.size());
		merge(sim.mutex_errors.begin(), sim.mutex_errors.end(), old_mutex_errors.begin(), old_mutex_errors.end(), mutex_errors.begin());
		mutex_errors.resize(unique(mutex_errors.begin(), mutex_errors.end()) - mutex_errors.begin());
	}
	else
		mutex_errors = sim.mutex_errors;
}

state simulator::get_state()
{
	return state(local.tokens, encoding);
}

vector<pair<int, int> > simulator::get_vacuous_choices()
{
	vector<pair<int, int> > vacuous_choices;
	for (int i = 0; i < (int)local.ready.size(); i++)
		if (local.ready[i].vacuous)
			for (int j = i+1; j < (int)local.ready.size(); j++)
				if (local.ready[j].vacuous && vector_intersection_size(local.ready[i].tokens, local.ready[j].tokens) > 0)
					vacuous_choices.push_back(pair<int, int>(i, j));

	return vacuous_choices;
}



}
