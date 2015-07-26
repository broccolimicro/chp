/*
 * state.cpp
 *
 *  Created on: Jun 23, 2015
 *      Author: nbingham
 */

#include <common/text.h>
#include "state.h"
#include "graph.h"

namespace chp
{

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

state::state(vector<token> tokens, vector<vector<int> > encodings)
{
	this->tokens = tokens;
	this->encodings = encodings;
}

state::~state()
{

}

void state::hash(hasher &hash) const
{
	hash.put(&tokens);
}

state state::merge(int composition, const state &s0, const state &s1)
{
	state result;

	result.tokens.resize(s0.tokens.size() + s1.tokens.size());
	::merge(s0.tokens.begin(), s0.tokens.end(), s1.tokens.begin(), s1.tokens.end(), result.tokens.begin());
	result.tokens.resize(unique(result.tokens.begin(), result.tokens.end()) - result.tokens.begin());

	if (composition == petri::parallel)
	{
		for (int i = 0; i < (int)s0.encodings.size(); i++)
			for (int j = 0; j < (int)s1.encodings.size(); j++)
			{
				vector<int> add;
				int m = min(s0.encodings[i].size(), s1.encodings[j].size());
				for (int k = 0; k < m; k++)
				{
					if (s0.encodings[i][k] == 0)
						add.push_back(s1.encodings[j][k]);
					else
						add.push_back(s0.encodings[i][k]);
				}

				result.encodings.push_back(add);
			}

	}
	else if (composition == petri::choice)
	{
		result.encodings.insert(result.encodings.end(), s0.encodings.begin(), s0.encodings.end());
		result.encodings.insert(result.encodings.end(), s1.encodings.begin(), s1.encodings.end());
	}

	return result;
}

state state::collapse(int composition, int index, const state &s)
{
	state result;

	result.tokens.push_back(token(index));

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
			result.tokens.push_back(token(loc->second.index));
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
		result += "[";
		for (int j = 0; j < (int)encodings[i].size(); j++)
		{
			if (j != 0)
				result += ", ";
			result += variables.nodes[j].to_string() + "=" + ::to_string(encodings[i][j]);
		}
		result += "] ";
	}

	return result;
}

}
