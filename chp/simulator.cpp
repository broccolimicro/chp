#include "simulator.h"
#include "graph.h"
#include <common/text.h>
#include <common/message.h>
#include <interpret_arithmetic/export.h>

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

string instability::to_string(const chp::graph &g)
{
	string result;
	result = "unstable rule " + enabled_transition::to_string(g);

	result += " cause: {";

	for (int j = 0; j < (int)history.size(); j++)
	{
		if (j != 0)
			result += "; ";

		result += history[j].to_string(g);
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

string interference::to_string(const chp::graph &g)
{
	return "interfering assignments " + first.to_string(g) + " and " + second.to_string(g);
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

string mutex::to_string(const chp::graph &g)
{
	return "non-exclusive guards in deterministic selection for assignments " + first.to_string(g) + " and " + second.to_string(g);
}

deadlock::deadlock()
{

}

deadlock::deadlock(const state &s) : state(s)
{

}

deadlock::deadlock(vector<token> tokens, arithmetic::State encodings) : state(tokens, encodings)
{

}

deadlock::~deadlock()
{

}

string deadlock::to_string(const graph &g)
{
	return "deadlock detected at state " + state::to_string(g);
}

simulator::simulator()
{
	base = NULL;
}

simulator::simulator(graph *base, state initial) {
	this->base = base;
	if (base != NULL) {
		encoding = initial.encodings;
		global = initial.encodings;
		for (int k = 0; k < (int)initial.tokens.size(); k++) {
			tokens.push_back(chp::token(initial.tokens[k].index));
		}
	}
}

simulator::~simulator() {
}

// Returns a vector of indices representing the transitions
// that this marking enabled and the term of each transition
// that's enabled.
int simulator::enabled(bool sorted) {
	if (base == NULL) {
		internal("", "NULL pointer to simulator::base", __FILE__, __LINE__);
		return 0;
	}

	// Do a pre-screen
	for (int i = (int)tokens.size()-1; i >= 0; i--)
		if (tokens[i].cause >= 0)
			tokens.erase(tokens.begin() + i);

	if (tokens.size() == 0)
		return 0;

	if (!sorted)
		sort(tokens.begin(), tokens.end());

	// Get the list of transitions that have a sufficient number of tokens at the input places
	vector<enabled_transition> preload;
	vector<enabled_transition> potential;
	vector<int> global_disabled;
	vector<int> disabled;
	disabled.reserve(base->transitions.size());
	
	int preload_size = 0;
	do {
		disabled = global_disabled;
		preload_size = preload.size();
		for (auto a = base->arcs[place::type].begin(); a != base->arcs[place::type].end(); a++) {
			// A transition will only be in disabled if we've already determined that it can't be enabled.
			auto d = lower_bound(disabled.begin(), disabled.end(), a->to.index);
			if (d == disabled.end() or *d != a->to.index) {
				// Find the index of this transition (if any) in the loaded pool
				bool loaded_found = false;
				for (int i = (int)preload.size()-1; i >= 0; i--) {
					if (preload[i].index == a->to.index) {
						loaded_found = true;

						// Check to make sure that this enabled transition isn't from a previous iteration.
						if (i >= preload_size) {
							// Check to see if there is any token at the input place of this arc and make sure that
							// this token has not already been consumed by this particular transition
							vector<int> matching_tokens;
							for (int j = 0; j < (int)tokens.size(); j++) {
								if (a->from.index == tokens[j].index) {
									// We have to implement a recursive...ish algorithm here
									// to check to see if this token has already been used by
									// any of the transitions in the chain
									bool used = false;
									vector<int> test = preload[i].tokens;
									for (int k = 0; k < (int)test.size() and not used; k++) {
										used = (test[k] == j);
										if (tokens[test[k]].cause >= 0) {
											test.insert(test.end(), preload[tokens[test[k]].cause].tokens.begin(), preload[tokens[test[k]].cause].tokens.end());
										}
									}

									if (not used) {
										matching_tokens.push_back(j);
									}
								}
							}

							// there might be more than one matching token
							for (int j = (int)matching_tokens.size()-1; j >= 1; j--) {
								preload.push_back(preload[i]);
								preload.back().tokens.push_back(matching_tokens[j]);
							}

							if (not matching_tokens.empty()) {
								preload[i].tokens.push_back(matching_tokens[0]);
							} else {
								// If we didn't find a token at the input place, then we know that this transition can't
								// be enabled. So lets remove this from the list of possibly enabled transitions
								disabled.insert(d, a->to.index);
								preload.erase(preload.begin() + i);
							}
						}
					}
				}

				// If we didn't find this transition in the loaded list, then this is our
				// first encounter with this transition being possibly enabled for all of the
				// previous iterations. We need to add it to the loaded list.
				if (!loaded_found) {
					bool token_found = false;
					for (int j = 0; j < (int)tokens.size(); j++)
						if (a->from.index == tokens[j].index)
						{
							token_found = true;
							preload.push_back(enabled_transition(a->to.index));
							preload.back().tokens.push_back(j);
						}

					if (!token_found)
						disabled.insert(d, a->to.index);
				}
			}
		}

		// Now we need to check the guards of all of the loaded transitions against the state
		for (int i = (int)preload.size()-1; i >= preload_size; i--) {
			// To ensure that a transition is actually enabled, we need to check its
			// guard. We also need to implement an instability check. Unfortunately,
			// instability is defined in the context of complete production rules, but
			// not really in the context of an HSE where we only have partial
			// production rules. Therefore, there are a couple of possible ways to
			// define instability depending on what phase of the circuit synthesis you
			// are in. Further, the chosen definition of instability affects how we
			// define the guard of the transition that we need to check.

			// 1. If you assume that you can still insert state variables, then
			// inserting a state variable can break apart a guard. This means that if a
			// guard leading to an assignment passes, and then later doesn't, that
			// doesn't necessarily imply that the transition is unstable. This is
			// because the state variable insertion algorithm could insert a state
			// transition that separates that piece of the guard that doesn't pass from
			// the transition in question. Therefore, conservatively, you'd never want
			// to treat an unstable guard as unstable.  Instead the guard would hold
			// state. Once it passes the first time, it's considered passed. The
			// problem with this approach is that it assumes state variables in a lot
			// of different places that the designer is not likely to have intended. It
			// then proceeds to ignore instabilities in the HSE, leading to an
			// incorrect sythesis when the state variable insertion algorithm
			// invariably doesn't catch it.

			// 2. If you assume that you can not still insert state variables, then we
			// have a separate problem. Each assignment in the HSE is a partial
			// production rule. Given a sequence of transition a+; b-; c+, in general
			// the term "a" will appear in the production rule for "b-" as "a -> b-",
			// and the term "~b" will appear in the production rule for "c+" as "~b ->
			// c+". This implements the event sequencing. Meanwhile, in the HSE "b-"
			// and "c+" don't have guards. It is possible after executing "a+" but
			// before executing "b-" for a parallel assignment to lower "a". This
			// SHOULD make "b-" unstable. To implement this correctly, we'd have to add
			// those terms into the guard that we check while running the simulation.

			// However, if the previous transition is vacuous (the variable being
			// assigned doesn't actually change its value as a result of the
			// assignment), then it is not enough to just add the term for that
			// assignment. Instead, we have to include that assignment's guard, and
			// that assignment's previous assignments and so on until we enounter
			// non-vacuous transitions.

			// It gets worse though. Following the above strategy will create false
			// positive identifications of unstable transitions. In the case of a
			// handshake [R.e]; R.r+; [~R.e]; R.r-, the production rule for R.r- does
			// not need to contain R.r+ (Also, it can't contain R.r+ because that would
			// create a self-invalidating production rule). So we need to look back
			// through the history of transitions and see which of the previous
			// assigments have been acknowledged through another variable.

			// At this point we would pretty much be deriving the complete production
			// rule for every transition we execute in the simulator just in time for
			// execution by running guard strengthening.


			// For now, we've implemented a compromise that seams generally reasonable.
			// Syntactic ordering constraints are assumed to be stable. This means that
			// we don't look backwards at the previous assignments. We only check what
			// is written in the guard in the HSE.

			// As a result of graph::post_process(), all of the guards should have been
			// merged into the closest assignment.
			preload[i].depend = true;
			for (int j = 0; j < (int)preload[i].tokens.size(); j++) {
				preload[i].depend = preload[i].depend & tokens[preload[i].tokens[j]].guard;
			}
			preload[i].guard = base->transitions[preload[i].index].guard & base->transitions[preload[i].index].action.guard();

			//preload[i].depend.hide(base->transitions[preload[i].index].local_action.vars());
		
			// Vacuous transitions may pass through a selection statement with
			// multiple branches. However, that shouldn't pre-empt the selection
			// statement and force a choice. Instead, the guard from the selection
			// should also propagate through to whatever the next non-vacuous
			// transition is. This means that we'll have to propagate all of the
			// guards from the vacuous transitions through. However, this will create
			// a false-positive for instabilities. So we need to remove the terms in
			// the propagated guard that have already been acknowledged by other
			// transitions acknowledged by the base guard and the sequencing.
			arithmetic::Expression guard = preload[i].depend & preload[i].guard;

			// Check for unstable transitions
			bool previously_enabled = false;
			for (int j = 0; j < (int)loaded.size() and not previously_enabled; j++) {
				if (loaded[j].index == preload[i].index and not loaded[j].vacuous) {
					preload[i].history = loaded[j].history;
					previously_enabled = true;
				}
			}

			// Now we check to see if the current state passes the guard
			int isReady = arithmetic::passesGuard(encoding, global, guard, &preload[i].guard_action);

			if (isReady < 0 and previously_enabled) {
				isReady = 0;
			}

			preload[i].local_action = base->transitions[preload[i].index].action.evaluate(encoding & preload[i].guard_action);
			preload[i].remote_action = preload[i].local_action.remote(base->remote_groups());

			preload[i].stable = (isReady > 0);
			preload[i].vacuous = arithmetic::vacuousAssign(global, preload[i].remote_action, preload[i].stable);
			preload[i].stable = preload[i].stable or preload[i].vacuous;

			// if the transition is vacuous, then we've already passed the guard even
			// if the guard is not satisfied by the current state
			if (preload[i].vacuous) {
				vector<int> output = base->next(transition::type, preload[i].index);
				bool loop = true;
				for (int j = 0; j < (int)output.size() and loop; j++) {
					loop = false;
					for (int k = 0; k < (int)tokens.size() and not loop; k++) {
						loop = tokens[k].index == output[j];
					}
				}

				if (loop) {
					for (int j = 0; j < (int)tokens.size(); j++) {
						if (tokens[j].cause > i) {
							tokens[j].cause--;
						}
					}

					auto d = lower_bound(global_disabled.begin(), global_disabled.end(), preload[i].index);
					if (d == global_disabled.end() or *d != preload[i].index) {
						global_disabled.insert(d, preload[i].index);
					}
					preload.erase(preload.begin() + i);
				} else {
					arithmetic::Expression guard = preload[i].depend;
					if (preload[i].local_action.isTautology()) {
						guard = guard & preload[i].guard;
					} else {
						// the guard should be the most minimal possible guard necessary to
						// guard any multi-branch selection statement (unless the
						// transition is a skip, in which case any guard should be passed
						// on to the next transition). If there isn't a multi-term
						// selection statement, then the guard should be ignored.
						arithmetic::Expression exclude = base->exclusion(preload[i].index);
						arithmetic::Expression weak = arithmetic::weakestGuard(preload[i].guard, exclude);
						guard = guard & weak;
						//cout << "setting token guard:" << export_expression(base->transitions[preload[i].index].guard, *variables).to_string() << " exclude:" << export_expression(exclude, *variables).to_string() << " weak:" << export_expression(weak, *variables).to_string() << " result:" << export_expression(guard, *variables).to_string() << endl;
					}

					for (int j = 0; j < (int)output.size(); j++)
					{
						preload[i].output_marking.push_back((int)tokens.size());
						tokens.push_back(token(output[j], guard, i));
					}
				}
			} else {
				if (isReady >= 0) {
					potential.push_back(preload[i]);
				}

				for (int j = 0; j < (int)tokens.size(); j++) {
					if (tokens[j].cause > i) {
						tokens[j].cause--;
					}
				}

				auto d = lower_bound(global_disabled.begin(), global_disabled.end(), preload[i].index);
				if (d == global_disabled.end() or *d != preload[i].index) {
					global_disabled.insert(d, preload[i].index);
				}
				preload.erase(preload.begin() + i);
			}
		}
	} while ((int)preload.size() != preload_size);

	for (int i = 0; i < (int)potential.size(); i++) {
		vector<int> output = base->next(transition::type, potential[i].index);
		for (int j = 0; j < (int)output.size(); j++) {
			potential[i].output_marking.push_back((int)tokens.size());
			tokens.push_back(token(output[j], arithmetic::Operand(true), preload.size()));
		}

		preload.push_back(potential[i]);
	}

	loaded = preload;
	ready.clear();

	for (int i = 0; i < (int)loaded.size(); i++) {
		if (not loaded[i].vacuous) {
			for (int j = 0; j < (int)loaded[i].local_action.states.size(); j++) {
				ready.push_back(pair<int, int>(i, j));
			}
		}
	}

	return ready.size();
}

enabled_transition simulator::fire(int index)
{
	if (base == NULL)
	{
		internal("", "NULL pointer to simulator::base", __FILE__, __LINE__);
		return enabled_transition();
	}

	enabled_transition t = loaded[ready[index].first];
	int term = ready[index].second;

	// We need to go through the potential list of transitions and flatten their input and output tokens.
	// Since we know that no transition can use the same token twice, we can simply mush them all
	// into one big list and then remove duplicates.
	// assumes that a transition in the loaded array only depends upon transitions before it in the array
	vector<int> visited(1, ready[index].first);
	for (int i = 0; i < (int)t.tokens.size(); i++) {
		if (tokens[t.tokens[i]].cause >= 0 and tokens[t.tokens[i]].cause < ready[index].first)
		{
			t.tokens.insert(t.tokens.end(), loaded[tokens[t.tokens[i]].cause].tokens.begin(), loaded[tokens[t.tokens[i]].cause].tokens.end());
			t.output_marking.insert(t.output_marking.end(), loaded[tokens[t.tokens[i]].cause].output_marking.begin(), loaded[tokens[t.tokens[i]].cause].output_marking.end());
			visited.push_back(tokens[t.tokens[i]].cause);
		}
	}

	sort(t.tokens.begin(), t.tokens.end());
	t.tokens.resize(unique(t.tokens.begin(), t.tokens.end()) - t.tokens.begin());
	sort(t.output_marking.begin(), t.output_marking.end());
	t.output_marking.resize(unique(t.output_marking.begin(), t.output_marking.end()) - t.output_marking.begin());
	sort(visited.begin(), visited.end());

	for (int i = 0; i < (int)loaded.size(); i++) {
		if (find(visited.begin(), visited.end(), i) == visited.end()) {
			for (int j = 0; j < (int)loaded[i].tokens.size(); j++) {
				if (tokens[loaded[i].tokens[j]].cause >= 0
				  and tokens[loaded[i].tokens[j]].cause < i
				  and find(visited.begin(), visited.end(), tokens[loaded[i].tokens[j]].cause) == visited.end()) {
					loaded[i].tokens.insert(loaded[i].tokens.end(), loaded[tokens[loaded[i].tokens[j]].cause].tokens.begin(), loaded[tokens[loaded[i].tokens[j]].cause].tokens.end());
				}
			}

			sort(loaded[i].tokens.begin(), loaded[i].tokens.end());
			loaded[i].tokens.resize(unique(loaded[i].tokens.begin(), loaded[i].tokens.end()) - loaded[i].tokens.begin());
		}
	}

	// disable any transitions that were dependent on at least one of the same tokens
	// This is only necessary to check for unstable transitions in the enabled() function
	for (int i = (int)loaded.size()-1, j = ready.size()-1; i >= 0; i--)
	{
		vector<int> intersect = vector_intersection(loaded[i].tokens, t.tokens);
		if (intersect.size() > 0)
		{
			// ASSUME ready array is sorted in ascending order
			bool is_effective = false;
			for (; j >= 0 and not is_effective; j--)
				is_effective = (ready[j].first == i);

			bool is_deterministic = true;
			for (int k = 0; k < (int)intersect.size() and is_deterministic; k++)
				is_deterministic = not base->places[tokens[intersect[k]].index].arbiter;

			if (is_effective and is_deterministic and loaded[i].index != t.index)
			{
				cout << "Intersect: (";
				for (int l = 0; l < (int)intersect.size(); l++)
					cout << tokens[intersect[l]].index << " ";
				cout << ")";
				cout << "Arbiters: (";
				for (int l = 0; l < (int)intersect.size(); l++) {
					if (base->places[tokens[intersect[l]].index].arbiter) {
						cout << tokens[intersect[l]].index << " ";
					}
				}
				cout << ")";
				mutex err = mutex(t, loaded[i]);
				vector<mutex>::iterator loc = lower_bound(mutex_errors.begin(), mutex_errors.end(), err);
				if (loc == mutex_errors.end() or *loc != err)
				{
					mutex_errors.insert(loc, err);
					error("", err.to_string(*base), __FILE__, __LINE__);
				}
			}

			loaded.erase(loaded.begin() + i);
		}
	}

	ready.clear();

	// take the set symmetric difference, but leave the two sets separate.
	for (int j = 0, k = 0; j < (int)t.tokens.size() and k < (int)t.output_marking.size(); ) {
		if (t.tokens[j] == t.output_marking[k]) {
			t.tokens.erase(t.tokens.begin() + j);
			t.output_marking.erase(t.output_marking.begin() + k);
		} else if (t.tokens[j] > t.output_marking[k]) {
			k++;
		} else if (t.tokens[j] < t.output_marking[k]) {
			j++;
		}
	}

	// Check to see if this transition is unstable
	if (not t.stable and not t.vacuous) {
		instability err = instability(t);
		vector<instability>::iterator loc = lower_bound(instability_errors.begin(), instability_errors.end(), err);
		if (loc == instability_errors.end() or *loc != err) {
			instability_errors.insert(loc, err);
			error("", err.to_string(*base), __FILE__, __LINE__);
		}
	}

	// Update the tokens
	for (int i = 0; i < (int)t.output_marking.size(); i++) {
		tokens[t.output_marking[i]].cause = -1;
	}

	sort(t.tokens.begin(), t.tokens.end());
	for (int i = t.tokens.size()-1; i >= 0; i--) {
		tokens.erase(tokens.begin() + t.tokens[i]);
	}

	for (int i = tokens.size()-1; i >= 0; i--) {
		if (tokens[i].cause >= 0) {
			tokens.erase(tokens.begin() + i);
		}
	}

	// Restrict the state with the guard
	if (t.stable and not t.vacuous) {
		global &= t.guard_action;
		encoding &= t.guard_action;
	}

	// Check for interfering transitions. Interfering transitions are the active
	// transitions that have fired since this active transition was enabled.
	for (int j = 0; j < (int)t.history.size(); j++) {
		arithmetic::State historical_action = base->term(t.history[j]).evaluate(encoding).remote(base->remote_groups());
		if (arithmetic::areInterfering(t.remote_action[term], historical_action))
		{
			interference err(term_index(t.index, term), t.history[j]);
			vector<interference>::iterator loc = lower_bound(interference_errors.begin(), interference_errors.end(), err);
			if (loc == interference_errors.end() or *loc != err)
			{
				interference_errors.insert(loc, err);
				error("", err.to_string(*base), __FILE__, __LINE__);
			}
		}

		t.local_action[term] = arithmetic::interfere(t.local_action[term], historical_action);
		t.remote_action[term] = arithmetic::interfere(t.remote_action[term], historical_action);
	}

	// Update the state
	global = localAssign(global, t.remote_action[term], t.stable);
	encoding = remoteAssign(localAssign(encoding, t.local_action[term], t.stable), global, true);

	// Update the history. The first thing we need to do is remove any assignments that no longer
	// have any effect on the global state. So we remove history items where all of the terms
	// in their assignments are conflicting with terms in more recent assignments.
	arithmetic::State m = t.local_action[term].mask();
	for (list<firing>::reverse_iterator i = history.rbegin(); i != history.rend();) {
		if (i->local_action.mask(m).isTautology()) {
			i++;
			i = list<firing>::reverse_iterator(history.erase(i.base()));
		} else {
			m = m.combineMask(i->local_action.mask());
			i++;
		}
	}

	// Add the latest firing to the history.
	history.push_back(firing(t, term));
	
	for (int i = 0; i < (int)loaded.size(); i++) {
		loaded[i].history.push_back(term_index(t.index, term));
	}

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
	state result;
	result.encodings = encoding;
	for (int i = 0; i < (int)tokens.size(); i++) {
		if (tokens[i].cause < 0) {
			result.tokens.push_back(tokens[i]);
		}
	}

	sort(result.tokens.begin(), result.tokens.end());
	return result;
}

state simulator::get_key()
{
	state result;
	result.encodings = encoding;
	for (int i = 0; i < (int)ready.size(); i++)
		result.tokens.push_back(petri::token(loaded[ready[i].first].index));
	sort(result.tokens.begin(), result.tokens.end());
	result.tokens.resize(unique(result.tokens.begin(), result.tokens.end()) - result.tokens.begin());
	return result;
}

vector<pair<int, int> > simulator::get_choices()
{
	vector<pair<int, int> > result;
	vector<vector<int> > tree;
	vector<vector<int> > visited;

	for (int i = 0; i < (int)loaded.size(); i++)
	{
		tree.push_back(loaded[i].tokens);
		visited.push_back(vector<int>(1, i));
	}

	for (int i = 0; i < (int)tree.size(); i++)
	{
		int tsize = (int)tree[i].size();
		for (int j = 0; j < tsize; j++) {
			if (tokens[tree[i][j]].cause >= 0 and tokens[tree[i][j]].cause < i) {
				tree[i].insert(tree[i].end(), tree[tokens[tree[i][j]].cause].begin(), tree[tokens[tree[i][j]].cause].end());
				visited[i].insert(visited[i].end(), visited[tokens[tree[i][j]].cause].begin(), visited[tokens[tree[i][j]].cause].end());
			}
		}

		sort(tree[i].begin(), tree[i].end());
		tree[i].resize(unique(tree[i].begin(), tree[i].end()) - tree[i].begin());
		sort(visited[i].begin(), visited[i].end());
		visited[i].resize(unique(visited[i].begin(), visited[i].end()) - visited[i].begin());
	}

	vector<int> lindices;
	for (int i = 0; i < (int)ready.size(); i++)
		lindices.push_back(ready[i].first);
	sort(lindices.begin(), lindices.end());
	lindices.resize(unique(lindices.begin(), lindices.end()) - lindices.begin());

	for (int i = 0; i < (int)lindices.size(); i++)
		for (int j = i+1; j < (int)lindices.size(); j++)
		{
			vector<int> temp = loaded[lindices[j]].tokens;
			for (int k = 0; k < (int)temp.size(); k++) {
				if (tokens[temp[k]].cause >= 0
				  and tokens[temp[k]].cause < lindices[j]
				  and find(visited[i].begin(), visited[i].end(), tokens[temp[k]].cause) == visited[i].end()) {
					temp.insert(temp.end(), loaded[tokens[temp[k]].cause].tokens.begin(), loaded[tokens[temp[k]].cause].tokens.end());
				}
			}

			sort(temp.begin(), temp.end());
			temp.resize(unique(temp.begin(), temp.end()) - temp.begin());

			if (vector_intersects(tree[lindices[i]], temp))
				result.push_back(pair<int, int>(lindices[i], lindices[j]));
		}

	return result;
}

}
