#pragma once

#include <common/standard.h>
#include <arithmetic/action.h>
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

ostream &operator<<(ostream &os, const place &p);

struct transition : petri::transition
{
	transition();
	transition(arithmetic::Expression guard, arithmetic::Action assign);
	transition(arithmetic::Expression guard, arithmetic::Parallel assign);
	transition(arithmetic::Expression guard, arithmetic::Choice assign);
	~transition();

	arithmetic::Expression guard;
	arithmetic::Choice action;

	static transition merge(int composition, const transition &t0, const transition &t1);
	static bool mergeable(int composition, const transition &t0, const transition &t1);

	bool is_infeasible();
	bool is_vacuous();
};

ostream &operator<<(ostream &os, const transition &t);

struct variable {
	variable();
	variable(string name, int region=0);
	~variable();

	string name;
	int region;

	vector<int> remote;
};

struct graph : petri::graph<chp::place, chp::transition, petri::token, chp::state>
{
	typedef petri::graph<chp::place, chp::transition, petri::token, chp::state> super;

	graph();
	~graph();

	string name;

	vector<variable> vars;

	int netIndex(string name, int region=0) const;
	int netIndex(string name, int region=0, bool define=false);
	pair<string, int> netAt(int uid) const;
	int netCount() const;

	using super::create;
	int create(variable n = variable());

	void connect_remote(int from, int to);
	vector<vector<int> > remote_groups();

	chp::transition &at(term_index idx);
	arithmetic::Parallel &term(term_index idx);

	using super::merge;
	map<petri::iterator, vector<petri::iterator> > merge(int composition, graph g);

	void post_process(bool proper_nesting = false, bool aggressive = false);
	void decompose();
	void expand();
	arithmetic::Expression exclusion(int index) const;
};

}

