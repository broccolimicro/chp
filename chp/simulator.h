/*
 * simulator.h
 *
 *  Created on: Apr 28, 2015
 *      Author: nbingham
 */

#include <common/standard.h>
#include <ucs/variable.h>
#include "state.h"

#ifndef chp_simulator_h
#define chp_simulator_h

namespace chp
{

struct graph;

struct instability : enabled_transition
{
	instability();
	instability(const enabled_transition &cause);
	~instability();

	string to_string(const chp::graph &g, const ucs::variable_set &v);
};

struct interference : pair<term_index, term_index>
{
	interference();
	interference(const term_index &first, const term_index &second);
	~interference();

	string to_string(const chp::graph &g, const ucs::variable_set &v);
};

struct mutex : pair<enabled_transition, enabled_transition>
{
	mutex();
	mutex(const enabled_transition &first, const enabled_transition &second);
	~mutex();

	string to_string(const chp::graph &g, const ucs::variable_set &v);
};

struct deadlock : state
{
	deadlock();
	deadlock(const state &s);
	~deadlock();

	string to_string(const ucs::variable_set &v);
};

struct simulator
{
	simulator();
	simulator(const graph *base, const ucs::variable_set *variables, state initial, int index, bool environment);
	~simulator();

	vector<instability> instability_errors;
	vector<interference> interference_errors;
	vector<mutex> mutex_errors;
	vector<term_index> unacknowledged;
	term_index last;

	const graph *base;
	const ucs::variable_set *variables;

	vector<arithmetic::value> encoding;
	vector<arithmetic::value> global;

	struct
	{
		vector<local_token> tokens;
		vector<enabled_transition> ready;
	} local;

	int enabled(bool sorted = false);
	enabled_transition fire(int index);

	void merge_errors(const simulator &sim);
	state get_state();

	vector<pair<int, int> > get_vacuous_choices();
};

}

#endif
