#pragma once

#include <set>

#include <common/mapping.h>
#include <flow/func.h>

#include "graph.h"

namespace chp {

	//TODO: flow::Condition synthesizeConditionFromCHP(const graph &g);
	//TODO: flow::Condition synthesizeConditionFromTransitions(const std::set<int> &transitions);
	int synthesizeConditionFromTransitions(const graph& g, const std::set<int>& transitions, arithmetic::Expression predicate, flow::Func& func, mapping& chp_to_flow_nets);
	//int synthesizeConditionFromCHP(flow::Func& func, const std::set<int>& transitions, mapping& chp_to_flow_nets, const graph& g);
	flow::Func synthesizeFuncFromCHP(const graph &g);

}
