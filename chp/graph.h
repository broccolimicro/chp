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

typedef petri::place place;

struct transition : petri::transition
{
	transition();
	transition(arithmetic::expression e);
	transition(arithmetic::action a);
	transition(arithmetic::parallel a);
	transition(arithmetic::choice a);
	~transition();

	arithmetic::expression guard;
	arithmetic::choice local_action;
	arithmetic::choice remote_action;

	static transition merge(int composition, const transition &t0, const transition &t1);
	static bool mergeable(int composition, const transition &t0, const transition &t1);

	bool is_infeasible();
	bool is_vacuous();
};

struct graph : petri::graph<petri::place, chp::transition, petri::token, chp::state>
{
	typedef petri::graph<petri::place, chp::transition, petri::token, chp::state> super;

	graph();
	~graph();

	void post_process(const ucs::variable_set &variables, bool proper_nesting = false);
	void decompose(const use::variable_set &variables);
	void expand(const ucs::variable_set &variables);
};

}

