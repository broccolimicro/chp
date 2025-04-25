#pragma once

#include <common/standard.h>
#include <petri/state.h>
#include <petri/simulator.h>
#include "graph.h"
#include "state.h"

namespace chp
{

// An instability occurs when a transition is enabled, then disabled before
// firing. This creates a glitch on the output node that could kick the state
// machine out of a valid state.
// the enabled_transition represents the enabled transition that was then disabled.
struct instability : enabled_transition
{
	instability();
	instability(const enabled_transition &cause);
	~instability();

	string to_string(const chp::graph &g);
};

// An instability occurs when the pull up and the pull down network of a gate
// are both enabled at the same time. This creates a short across the gate that
// could lead to circuit damage.
// the pair<term_index, term_index> represents the two interfering transitions.
struct interference : pair<term_index, term_index>
{
	interference();
	interference(const term_index &first, const term_index &second);
	~interference();

	string to_string(const chp::graph &g);
};

// A mutex error occurs when multiple output transitions of a non-arbiter place
// become enabled at the same time. In this case, the user did not intend for
// the circuit to make a non-deterministic choice, but we find ourselves stuck
// with one because of an error in the rest of the design.
// the pair<enabled_transition, enabled_transition> represents the two enabled
// transitions that violate this mutex constraint.
struct mutex : pair<enabled_transition, enabled_transition>
{
	mutex();
	mutex(const enabled_transition &first, const enabled_transition &second);
	~mutex();

	string to_string(const chp::graph &g);
};

// A deadlock occurs when we reach a state with no enabled transitions. This
// means that the simulator has no way of making forward progress. Given a
// latency of picoseconds on most of these transitions, a circuit specification
// that terminates wouldn't be particularly useful.
// the state is the state we found with no enabled transitions.
struct deadlock : state
{
	deadlock();
	deadlock(const state &s);
	deadlock(vector<token> tokens, arithmetic::State encodings);
	~deadlock();

	string to_string(const chp::graph &g);
};

// This keeps track of a single simulation of a set of HSE and makes it easy to
// control that simulation either through an interactive interface or
// programmatically.
struct simulator
{
	// This is used by chp::graph to roll up the reset transitions.
	typedef petri::simulator<chp::place, chp::transition, petri::token, chp::state> super;

	simulator();
	simulator(graph *base, state initial);
	~simulator();

	// This simulator is also used for elaboration, so a lot of the errors we
	// encounter may be encountered multiple times as the elaborator visits
	// different versions of the same state. This record errors of different
	// types to be deduplicated and displayed at the end of elaboration.
	vector<instability> instability_errors;
	vector<interference> interference_errors;
	vector<mutex> mutex_errors;

	// This records the set of transitions in the order they were fired.
	// Currently it is used to help with debugging (an instability happened and
	// here is the list of transitions leading up to it).

	// However, there is also a question about the definition of instability.
	// Technically, instability is defined with respect to the production rules,
	// which we do not have yet. We only have partial production rules that make
	// up the transitions in the HSE graph. Previous version have used the firing
	// history to try to guess at further terms that might be found in the
	// production rule to identify unstable variables sooner in the flow. This
	// functionality has currently been commented out as a result of a refactor
	// and rework of the underlying engine. It is unclear whether this
	// functionality needs to be explored further or whether doing so would be
	// incorrect.

	// See todo in simulator.cpp
	list<firing> history;

	// Remember these pointers so that we do not have to include them as inputs
	// to every simulation step. graph is the HSE we are simulating, and
	// variables keeps a mapping from variable name to index in a minterm.
	graph *base;

	// The encoding is a minterm that records our knowledge about the current
	// value of each variable in the circuit.

	// Each variable can be one of four possible values:
	// '0' - GND
	// '1' - VDD
	// '-' - unknown (either 0 or 1)
	// 'X' - unstable or interfering (neither 0 nor 1) 

	// Unfortunately, a single variable might exist in multiple isochronic
	// domains. This means that the wire represented by the variable is long
	// enough that a transition on one end of the wire takes a significant amount
	// of time to propagate to the other end of the wire. Each end of that wire
	// is considered to be an isochronic domain, a section in which the
	// transition is assumed to propogate across atomically relative to
	// other events.

	// So when a transition happens on one end of the wire, we don't actually
	// know when it will arrive at the other end. This means that we lose
	// knowledge about the value at the other end of the wire, it becomes '-' in the encoding.

	// Therefore, the global encoding records not just our knowledge of the
	// current value of each wire, but records what all isochronic domains of
	// each variable will be given enough time. This means that the global
	// encoding does not lose knowledge about the value of any variable. Instead
	// the transition is assumed to happen atomically across all isochronic
	// domains.

	// This is used to determine if there is a transition in one isochronic
	// domain that interferes with a transition in another.
	
	// See haystack/lib/arithmetic/arithmetic/{state.h, state.cpp} for more
	// details about the minterm representation.
	arithmetic::State encoding;
	arithmetic::State global;

	// These are the tokens that mark the current state. Effectively, these are
	// the program counters. While Petri Nets technically allow more than one
	// token on a single place, our constrained adaptation of Petri Nets as a
	// representation of HSE does not.
	// See haystack/lib/chp/chp/state.h for the definition of token.
	vector<token> tokens;

	// These transitions could be selected to fire next. An enabled transition is
	// one in which all of the gates on the pull up or pull down network are
	// satisfied, and there is a connection from the source to the output through
	// the network of transistors. Because it has not yet fired, the output
	// hasn't passed the threshold voltage yet, signalling its complete
	// transition to the new value.
	vector<enabled_transition> loaded;

	// The ready array makes it easy to tell the elaborator and the user exactly
	// how many enabled transitions there are and makes it easy to select them
	// for firing.
	// ready[i].first indexes into simulator::loaded[]
	// ready[i].second indexes into base->transitions[loaded[ready[i].first].index].local_action.cubes[] which is the cube in the action of that transition
	vector<pair<int, int> > ready;

	int enabled(bool sorted = false);
	enabled_transition fire(int index);

	void merge_errors(const simulator &sim);
	state get_state();
	state get_key();
	vector<pair<int, int> > get_choices();
};


// TODO CHP Simulator
//
// Our goal is to build a reliable simulator for Communicating Hardware
// Processes. We currently have a basic parser set up in haystack/lib/parse_chp
// that fills in the graph in haystack/lib/chp/chp/graph.h using functions
// defined in haystack/lib/interpret_chp. The state assignments is somewhat
// filled out in haystack/lib/arithmetic with it's own parser
// haystack/parse_arithmetic and interpreter haystack/interpret_arithmetic.
//
// This simulator should look and feel a lot like the HSE simulator in
// haystack/lib/chp/chp/simulator.cpp. So use that has an example for how to
// approach some of the problems you'll encounter. However, as you get into the
// problem, you'll encounter a whole list of new ones.
//
// The following steps are guidelines and not hard rules. If you think you
// found a better way to approach the problem, then feel free to chase that
// down. If you need supporting infrastructure anywhere else in the project,
// feel free to add that in. If you need to modify this function definition,
// go for it.
//
// 1. Fill out chp::graph as needed
//   a. Work with team members to identify shared and unique requirements for the
//      CHP graph class
//   b. Create the data structures necessary to implement those requirements
//
// 2. Learn some of the underlying infrastructure
//   a. Create tests for the expression and assignment evaluation engine
//   b. Fill in gaps for operator evaluation
//   c. Implement channel action evaluation
//
// 3. Create a skeleton for the simulator, copying some from
//    haystack/lib/chp/chp/simulator.cpp
//   a. create a function that identifies enabled transitions
//   b. Call upon arithmetic::local_action to update the current state with a
//      selected enabled transition’s action
//
// 4. Clean up haystack/bin/chpsim to build an interactive simulation
//    environment that works with the CHP simulator
//
// === Successful completion of project ===
// 
// Your time is yours, what do you want to tackle next?
// Some ideas:
// 1. Looking at haystack/lib/chp/chp/elaborator.cpp, implement a space
//    exploration algorithm to work with the CHP simulator for verification
// 2. Add support for more complex types than just integer. Create a type
//    system and update the parser to support it.
// 3. Add support for more than just one neutral state.
// 4. Add support for user defined channel protocols.
//
// Final cleaup:
// 1. Clean up any bugs
// 2. Prepare demo
// 3. Document as needed

}

