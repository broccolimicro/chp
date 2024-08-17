#pragma once

#include <common/standard.h>
#include <arithmetic/action.h>
#include <ucs/variable.h>
#include <petri/graph.h>

#include "state.h"

namespace chp
{

using petri::iterator;
using petri::parallel;
using petri::choice;
using petri::sequence;

struct place : petri::place
{
	place();
	~place();

	// inherited from petri::place
	// vector<split_group> groups;

	// if true, more than one output transition from this place can be enabled
	// simultaneously. This means that the hardware needs to make a
	// non-deterministic decision about which one to fire. This is generally done
	// with an arbiter.
	bool arbiter;

	static place merge(int composition, const place &p0, const place &p1);
};

struct transition : petri::transition
{
	transition();
	transition(arithmetic::expression guard, arithmetic::action assign);
	transition(arithmetic::expression guard, arithmetic::parallel assign);
	transition(arithmetic::expression guard, arithmetic::choice assign);
	~transition();

	arithmetic::expression guard;
	arithmetic::choice action;

	static transition merge(int composition, const transition &t0, const transition &t1);
	static bool mergeable(int composition, const transition &t0, const transition &t1);

	bool is_infeasible();
	bool is_vacuous();
};

struct graph : petri::graph<chp::place, chp::transition, petri::token, chp::state>
{
	typedef petri::graph<chp::place, chp::transition, petri::token, chp::state> super;

	graph();
	~graph();

	chp::transition &at(term_index idx);
	arithmetic::parallel &term(term_index idx);

	void post_process(const ucs::variable_set &variables, bool proper_nesting = false, bool aggressive = false);
	void decompose(const ucs::variable_set &variables);
	void expand(const ucs::variable_set &variables);
	arithmetic::expression exclusion(int index) const;
};

}

