#pragma once

#include <common/standard.h>
#include <ucs/variable.h>
#include <petri/state.h>
#include <petri/simulator.h>
#include "graph.h"
#include "state.h"

namespace chp
{

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
// haystack/lib/hse/hse/simulator.cpp. So use that has an example for how to
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
//    haystack/lib/hse/hse/simulator.cpp
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
// 1. Looking at haystack/lib/hse/hse/elaborator.cpp, implement a space
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

