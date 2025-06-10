#pragma once

#include <common/standard.h>
#include <common/net.h>
#include <arithmetic/action.h>
#include <petri/graph.h>

#include "state.h"

namespace chp
{

// TODO(edward.bingham) by default, a guard in CHP should be non-atomic
// while a guard in HSE is atomic by default. This comes from the different
// levels of abstraction. An HSE is assumed to be written such that state
// variable insertion has already happened for the majority of the
// handshake and all expressions and transitions are boolean. This means
// that and by default there won't be added transitions between a guard and
// a transition. However, in CHP expressions and transitions are arithmetic
// and it is assumed that hanshake reshuffling and state variable insertion
// hasn't happened yet. This means that by default it is more likely than
// not that a state transition will be inserted between a guard and another
// statement. So, in CHP we need an extra syntax for the user to say either
// "using the atomic complex gate assumption, assume that this expression
// or transition is atomic" or "flag an error if it is not possible to
// implement this expression or transition in an atomic way". This should
// look something like the following:
//
// atomic await condition {
//   action
// } or atomic await condition {
//   action
// }
//
// assume atomic await condition {
//   action
// } or assume atomic await condition {
//   action
// }
//
// As a result if the user specifies the atomic flag, the default is to
// enforce atomicity in the synthesis, and only if they add "assume" do
// they get to use the atomic complex gate assumption.
//
// This means that arithmetic expressions need to be broken apart by
// default in the CHP, so we need to merge the arithmetic expression
// structure into the CHP graph, along with expression simplification, etc.
// This matches fairly well to existing compiler infrastructure, except for
// the direct acknowledgement of concurrency/sequencing independent of data
// dependency. Most compiler backends just have a flat ordered list of
// operators. We need to maintain a parallel/choice/sequence graph of such
// operators, and still be able to apply arithmetic simplification
// operations on top of that.
//
// The arithmetic library is still useful for things that don't fit well
// into that graph structure. In fact, I wonder if it would be possible to
// somehow interpret the simplification rules in the arithmetic library
// into an expression simplification system in the graph datastructure.
//
// Also, this means that I can reasonably put off the quantifier
// elimination algorithm and the base syntax will behave correctly given
// the above definitions. And once I add the quantifier elimination
// algorithm, it will insert nicely with the above new syntax.

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

	int netIndex(string name, bool define=false);
	int netIndex(string name) const;
	string netAt(int uid) const;
	int netCount() const;

	using super::create;
	int create(variable n = variable());

	void connect_remote(int from, int to);
	vector<vector<int> > remote_groups();

	chp::transition &at(term_index idx);
	arithmetic::Parallel &term(term_index idx);

	using super::merge;
	petri::mapping merge(graph g);

	void post_process(bool proper_nesting = false, bool aggressive = false);
	void decompose();
	void expand();
	arithmetic::Expression exclusion(int index) const;
};

}

