#pragma once

#include <set>

#include <common/mapping.h>
#include <flow/func.h>

#include "graph.h"

namespace chp {

	//arithmetic::Operand synthesizeOperandFromCHPVar(const string &chp_var_name, const int &chp_var_idx, const flow::Net::Purpose &purpose, flow::Func &func, mapping &chp_to_flow_nets);
	//int synthesizeConditionFromTransitions(const graph& g, const std::set<int>& transitions, arithmetic::Expression predicate, flow::Func& func, mapping& chp_to_flow_nets);

	//TODO: flow::Condition synthesizeConditionFromCHP(const graph &g);
	flow::Func synthesizeFuncFromCHP(const graph &g);

}
