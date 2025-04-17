#pragma once

#include <common/standard.h>
#include <arithmetic/action.h>
#include <arithmetic/expression.h>
#include <petri/state.h>
#include <petri/graph.h>
#include <ucs/variable.h>

namespace chp
{

struct graph;
struct firing;

// This points to the cube 'term' in the action of transition 'index' in a graph.
struct term_index
{
	term_index();
	term_index(int index);
	term_index(int index, int term);
	~term_index();

	// graph::transitions[index].local_action.cubes[term]
	int index;
	int term;

	void hash(hasher &hash) const;

	string to_string(const graph &g, const ucs::variable_set &v);
};

bool operator<(term_index i, term_index j);
bool operator>(term_index i, term_index j);
bool operator<=(term_index i, term_index j);
bool operator>=(term_index i, term_index j);
bool operator==(term_index i, term_index j);
bool operator!=(term_index i, term_index j);

// This stores all the information necessary to fire an enabled transition: the local
// and remote tokens that enable it, and the total state of those tokens.
struct enabled_transition : petri::enabled_transition
{
	enabled_transition();
	enabled_transition(int index);
	enabled_transition(int index, int term);
	~enabled_transition();

	// inherited from petri::enabled_transition
	// int index;
	// vector<int> tokens;
	
	// An enabled transition creates tokens on its output before we ever fire it.
	// If the transition is vacuous, these tokens can then be used to enable the
	// next non-vacuous transition. This vector keeps track of the index of each
	// of those output tokens.
	vector<int> output_marking;

	// An enabled transition's history keeps track of every transition that fired
	// between when this transition was enabled and when it fires. This allows us
	// to determine whether this transition was stable and non-interfering when
	// we finally decide to let the event fire.
	vector<term_index> history;
	
	// The intersection of all of the terms of the guard of this transition which
	// the current state passed. This is ANDed into the current state to fill in
	// missing information.
	arithmetic::State guard_action;
	arithmetic::Region local_action;
	arithmetic::Region remote_action;

	// The effective guard of this enabled transition.
	arithmetic::Expression guard;

	// The collection of all the guards through vacuous transitions leading to
	// this transition.
	arithmetic::Expression depend;

	// An enabled transition is vacuous if the assignment would leave the current
	// state encoding unaffected.
	bool vacuous;

	// An enabled transition becomes unstable when the current state no longer
	// passes the guard.
	bool stable;

	string to_string(const graph &g, const ucs::variable_set &v);
};

bool operator<(enabled_transition i, enabled_transition j);
bool operator>(enabled_transition i, enabled_transition j);
bool operator<=(enabled_transition i, enabled_transition j);
bool operator>=(enabled_transition i, enabled_transition j);
bool operator==(enabled_transition i, enabled_transition j);
bool operator!=(enabled_transition i, enabled_transition j);

struct firing {
	firing(const enabled_transition &t, int i);
	~firing();

	arithmetic::State guard_action;
	arithmetic::State local_action;
	arithmetic::State remote_action;
	term_index index;
};

// Tokens are like program counters, marking the position of the current state
// of execution. A token may exist at any place in the HSE as long as it is not
// possible for more than one token to exist in that place.
struct token : petri::token
{
	token();
	token(petri::token t);
	token(int index, arithmetic::Expression guard, int cause=-1);
	~token();

	// The current place this token resides at.
	// inherited from petri::token
	// int index

	arithmetic::Expression guard;

	int cause;

	string to_string();
};

// A state is a collection of tokens marking where we are executing in the HSE,
// and a set of variable assignments encoded as a minterm. The simulator
// provides the infrastracture to walk through the state space one transition
// at a time using this structure.
struct state : petri::state<petri::token>
{
	state();
	state(vector<petri::token> tokens, arithmetic::State encodings);
	state(vector<chp::token> tokens, arithmetic::State encodings);
	~state();

	// The tokens marking our location in the HSE
	// inherited from petri::state
	// vector<token> tokens;

	// The current value assiged to each variable.
	arithmetic::State encodings;

	void hash(hasher &hash) const;

	static state merge(const state &s0, const state &s1);
	static state collapse(int index, const state &s);
	state convert(map<petri::iterator, vector<petri::iterator> > translate) const;
	bool is_subset_of(const state &s);
	string to_string(const ucs::variable_set &variables);
};

ostream &operator<<(ostream &os, state s);

bool operator<(state s1, state s2);
bool operator>(state s1, state s2);
bool operator<=(state s1, state s2);
bool operator>=(state s1, state s2);
bool operator==(state s1, state s2);
bool operator!=(state s1, state s2);

}

