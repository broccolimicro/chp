/*
 * graph.h
 *
 *  Created on: Feb 1, 2015
 *      Author: nbingham
 */

#include <common/standard.h>
#include <arithmetic/assignment.h>
#include <ucs/variable.h>
#include <petri/graph.h>

#include "state.h"

#ifndef chp_graph_h
#define chp_graph_h

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
	transition(arithmetic::assignment a);
	transition(vector<arithmetic::assignment> a);
	transition(vector<vector<arithmetic::assignment> > a);
	~transition();

	vector<vector<arithmetic::assignment> > actions;

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
};

}

#endif
