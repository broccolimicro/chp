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

	vector<term_index> history;
	
	arithmetic::state guard_action;

	// The effective guard of this enabled transition.
	arithmetic::expression guard;

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

// Tokens are like program counters, marking the position of the current state
// of execution. A token may exist at any place in the HSE as long as it is not
// possible for more than one token to exist in that place.
struct token : petri::token
{
	token();
	token(petri::token t);
	token(int index);
	~token();

	// The current place this token resides at.
	// inherited from petri::token
	// int index

	string to_string();
};

// A state is a collection of tokens marking where we are executing in the HSE,
// and a set of variable assignments encoded as a minterm. The simulator
// provides the infrastracture to walk through the state space one transition
// at a time using this structure.
struct state : petri::state<petri::token>
{
	state();
	state(vector<petri::token> tokens, arithmetic::state encodings);
	state(vector<chp::token> tokens, arithmetic::state encodings);
	~state();

	// The tokens marking our location in the HSE
	// inherited from petri::state
	// vector<token> tokens;

	// The current value assiged to each variable.
	arithmetic::state encodings;

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

