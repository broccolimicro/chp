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

using petri::token;

/* This points to the cube 'term' in the action of transition 'index' in a graph.
 * i.e. g.transitions[index].action.cubes[term]
 */
struct term_index : petri::term_index
{
	term_index();
	term_index(int index);
	term_index(int index, int term);
	~term_index();

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
	local_token(token t);
	local_token(int index, arithmetic::expression guard, vector<enabled_transition> prev);
	~local_token();

	arithmetic::expression guard;
	vector<enabled_transition> prev;

	string to_string();
};

struct state : petri::state<token>
{
	state();
	state(vector<token> tokens, vector<arithmetic::value> encodings);
	state(vector<local_token> tokens, vector<arithmetic::value> encoding);
	~state();

	vector<arithmetic::value> encodings;

	void hash(hasher &hash) const;

	static state merge(const state &s0, const state &s1);
	static state collapse(int index, const state &s);
	state convert(map<petri::iterator, petri::iterator> translate) const;
	string to_string(const ucs::variable_set &variables);
};

}

#endif
