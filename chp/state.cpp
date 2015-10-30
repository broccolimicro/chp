/*
 * state.cpp
 *
 *  Created on: Jun 23, 2015
 *      Author: nbingham
 */

#include <common/text.h>
#include "state.h"
#include "graph.h"
#include <interpret_arithmetic/export.h>

namespace chp
{

term_index::term_index()
{
	term = -1;
}

term_index::term_index(int index) : petri::term_index(index)
{
	term = -1;
}

term_index::term_index(int index, int term) : petri::term_index(index)
{
	this->term = term;
}

term_index::~term_index()
{

}

void term_index::hash(hasher &hash) const
{
	hash.put(&index);
	hash.put(&term);
}

string term_index::to_string(const graph &g, const ucs::variable_set &v)
{
	return "T" + ::to_string(index) + "." + ::to_string(term) + ":" + export_composition(g.transitions[index].local_action.cubes[term], v).to_string();
}

bool operator<(term_index i, term_index j)
{
	return (i.index < j.index) ||
		   (i.index == j.index && i.term < j.term);
}

bool operator>(term_index i, term_index j)
{
	return (i.index > j.index) ||
		   (i.index == j.index && i.term > j.term);
}

bool operator<=(term_index i, term_index j)
{
	return (i.index < j.index) ||
		   (i.index == j.index && i.term <= j.term);
}

bool operator>=(term_index i, term_index j)
{
	return (i.index > j.index) ||
		   (i.index == j.index && i.term >= j.term);
}

bool operator==(term_index i, term_index j)
{
	return (i.index == j.index && i.term == j.term);
}

bool operator!=(term_index i, term_index j)
{
	return (i.index != j.index || i.term != j.term);
}

enabled_transition::enabled_transition()
{
	index = 0;
	vacuous = false;
	stable = true;
	guard = arithmetic::operand(1);
}

enabled_transition::enabled_transition(int index)
{
	this->index = index;
	vacuous = false;
	stable = true;
	guard = arithmetic::operand(1);
}

enabled_transition::~enabled_transition()
{

}

bool operator<(enabled_transition i, enabled_transition j)
{
	return ((term_index)i < (term_index)j) ||
		   ((term_index)i == (term_index)j && i.history < j.history);
}

bool operator>(enabled_transition i, enabled_transition j)
{
	return ((term_index)i > (term_index)j) ||
		   ((term_index)i == (term_index)j && i.history > j.history);
}

bool operator<=(enabled_transition i, enabled_transition j)
{
	return ((term_index)i < (term_index)j) ||
		   ((term_index)i == (term_index)j && i.history <= j.history);
}

bool operator>=(enabled_transition i, enabled_transition j)
{
	return ((term_index)i > (term_index)j) ||
		   ((term_index)i == (term_index)j && i.history >= j.history);
}

bool operator==(enabled_transition i, enabled_transition j)
{
	return ((term_index)i == (term_index)j && i.history == j.history);
}

bool operator!=(enabled_transition i, enabled_transition j)
{
	return ((term_index)i != (term_index)j || i.history != j.history);
}

local_token::local_token()
{
	index = 0;
	guard = arithmetic::operand(1);
}

local_token::local_token(int index)
{
	this->index = index;
	guard = arithmetic::operand(1);
}

local_token::local_token(token t)
{
	this->index = t.index;
	guard = arithmetic::operand(1);
}

local_token::local_token(int index, arithmetic::expression guard, vector<enabled_transition> prev)
{
	this->index = index;
	this->guard = guard;
	this->prev = prev;
}

local_token::~local_token()
{

}

string local_token::to_string()
{
	return "P" + ::to_string(index);
}

state::state()
{
}

state::state(vector<token> tokens, vector<arithmetic::value> encodings)
{
	this->tokens = tokens;
	this->encodings = encodings;
}

state::state(vector<local_token> tokens, vector<arithmetic::value> encoding)
{
	this->tokens = tokens;
	this->encodings = encoding;
}

state::~state()
{

}

void state::hash(hasher &hash) const
{
	hash.put(&tokens);
}

state state::merge(const state &s0, const state &s1)
{
	state result;

	result.tokens.resize(s0.tokens.size() + s1.tokens.size());
	::merge(s0.tokens.begin(), s0.tokens.end(), s1.tokens.begin(), s1.tokens.end(), result.tokens.begin());
	result.tokens.resize(unique(result.tokens.begin(), result.tokens.end()) - result.tokens.begin());

	int m = min(s0.encodings.size(), s1.encodings.size());
	for (int k = 0; k < m; k++)
	{
		if (s0.encodings[k].data != s1.encodings[k])
			result.encodings.push_back(arithmetic::value::unknown);
		else
			result.encodings.push_back(s0.encodings[k]);
	}

	return result;
}

state state::collapse(int index, const state &s)
{
	state result;

	result.tokens.push_back(petri::token(index));

	result.encodings = s.encodings;

	return result;
}

state state::convert(map<petri::iterator, petri::iterator> translate) const
{
	state result;

	for (int i = 0; i < (int)tokens.size(); i++)
	{
		map<petri::iterator, petri::iterator>::iterator loc = translate.find(petri::iterator(place::type, tokens[i].index));
		if (loc != translate.end())
			result.tokens.push_back(petri::token(loc->second.index));
	}

	result.encodings = encodings;

	return result;
}

string state::to_string(const ucs::variable_set &variables)
{
	string result = "{";
	for (int i = 0; i < (int)tokens.size(); i++)
	{
		if (i != 0)
			result += " ";
		result += ::to_string(tokens[i].index);
	}
	result += "} ";

	for (int i = 0; i < (int)encodings.size(); i++)
	{
		if (i != 0)
			result += ", ";
		result += variables.nodes[i].to_string() + "=" + ::to_string(encodings[i]);
	}

	return result;
}

}
