/*
 * state.h
 *
 *  Created on: Jun 23, 2015
 *      Author: nbingham
 */

#include <common/standard.h>
#include <petri/state.h>
#include <petri/graph.h>
#include <ucs/variable.h>

#include <arithmetic/expression.h>

#ifndef chp_state_h
#define chp_state_h

namespace chp
{

struct graph;

using petri::term_index;
using petri::token;

/* This stores all the information necessary to fire an enabled transition: the local
 * and remote tokens that enable it, and the total state of those tokens.
 */
struct enabled_transition : petri::enabled_transition<term_index>
{
	enabled_transition();
	enabled_transition(int index);
	~enabled_transition();

	vector<term_index> history;
	arithmetic::expression guard;
	bool vacuous;
	bool stable;
};

bool operator<(enabled_transition i, enabled_transition j);
bool operator>(enabled_transition i, enabled_transition j);
bool operator<=(enabled_transition i, enabled_transition j);
bool operator>=(enabled_transition i, enabled_transition j);
bool operator==(enabled_transition i, enabled_transition j);
bool operator!=(enabled_transition i, enabled_transition j);

/* A local token maintains its own local state information and cannot access global state
 * except through transformations applied to its local state.
 */
struct local_token : token
{
	local_token();
	local_token(int index);
	local_token(int index, arithmetic::expression, vector<enabled_transition> prev);
	~local_token();

	arithmetic::expression guard;
	vector<enabled_transition> prev;

	string to_string();
};

struct state : petri::state<token>
{
	state();
	state(vector<token> tokens, vector<vector<int> > encodings);
	~state();

	vector<vector<int> > encodings;

	void hash(hasher &hash) const;

	static state merge(int composition, const state &s0, const state &s1);
	static state collapse(int composition, int index, const state &s);
	state convert(map<petri::iterator, petri::iterator> translate) const;
	string to_string(const ucs::variable_set &variables);
};

}

#endif
