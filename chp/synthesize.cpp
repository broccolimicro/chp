#include <algorithm>
#include <iostream>
#include <map>
#include <ranges>
#include <string>
#include <vector>
#include <unordered_set>

#include <arithmetic/algorithm.h>
#include <chp/graph.h>
#include <chp/synthesize.h>
#include <common/mapping.h>
#include <flow/func.h>

using namespace std;

using arithmetic::Expression;
//using arithmetic::Operand;

const int BUS_WIDTH = 8;
//TODO: globalize chp_to_flow_nets to synthesize.cpp?

namespace chp {

	//void synthesizeOperand(const arithmetic::Operand &chp_var, mapping &chp_to_flow_nets) {
	arithmetic::Operand synthesizeOperandFromCHPVar(const string &chp_var_name, const int &chp_var_idx, const flow::Net::Purpose &purpose, flow::Func &func, mapping &chp_to_flow_nets) {

		// Get or set Flow var for this CHP var
		Operand flow_operand;
		if (chp_to_flow_nets.has(chp_var_idx)) {
			int flow_var_idx = chp_to_flow_nets.map(chp_var_idx);
			flow_operand = Operand::varOf(flow_var_idx);	//TODO: preserve other Operand props?
			//cout << "! HIT(" << chp_var_name << " @ CHP[" << chp_var_idx
			//	<< "]) => Flow[" << flow_var_idx << "]" << endl;

		} else {
			//TODO: synthesize appropriate widths
			flow_operand = func.pushNet(chp_var_name, flow::Type(flow::Type::FIXED, BUS_WIDTH), purpose);
			chp_to_flow_nets.set(chp_var_idx, static_cast<int>(flow_operand.index));
			//cout << "? MISS(" << chp_var_name << " @ CHP[" << chp_var_idx
			//	<< "]) => Flow[" << static_cast<int>(flow_operand.index) << "]" << endl;
		}

		return flow_operand;
	}


	// Crawl every expression inside chp::transition for nets to be mapped
	//TODO: clarify whether Expression is expected to be pre-mapped (to CHP vars) or post-mapped (to Flow Operands)
	void synthesizeOperandsFromExpression(const arithmetic::Expression &e, int condition_idx, flow::Func &func, mapping &chp_to_flow_nets, const chp::graph &g) {
		//if (e.top.isUndef()) {
		//	cout << "=======[UNDEFINED]=> " << e.to_string() << endl << endl;
		//	return;
		//} else if (e.sub.size() == 0) {
		//	cout << "========[.sub.size()==0]=> " << e.to_string() << endl << endl;
		//	return;
		//}

		for (const arithmetic::Operand &operand : e.exprIndex()) {
			const arithmetic::Operation &operation = *e.getExpr(operand.index);

			auto operand_is_var = [](const arithmetic::Operand& op) -> bool { return op.isVar(); };
			auto operand_to_net = [&g](const arithmetic::Operand& op) -> std::string { return g.netAt(op.index); };

			if (operation.func == arithmetic::Operation::OpType::TYPE_CALL) {
				std::string func_name = operation.operands[0].cnst.sval;

				//TODO: optimize perf (don't do string comparison)
				if (func_name == "recv") {
					size_t channel_idx = e.getExpr(operation.operands[1].index)->operands[0].index;
					string channel_name = g.vars[channel_idx].name;
					Operand flow_operand = synthesizeOperandFromCHPVar(channel_name, channel_idx, flow::Net::IN, func, chp_to_flow_nets);

					func.conds[condition_idx].ack(flow_operand);
					//cout << "* cond #" << condition_idx << " ack'd " << channel_name << endl;

				} else if (func_name == "send") {
					size_t channel_idx = e.getExpr(operation.operands[1].index)->operands[0].index;
					string channel_name = g.vars[channel_idx].name;
					Operand flow_operand = synthesizeOperandFromCHPVar(channel_name, channel_idx, flow::Net::OUT, func, chp_to_flow_nets);
					//cout << "* send on " << channel_name << "(" << channel_idx << ")" << endl;

					////TODO: don't forget to send raw constants (e.g. Operand::intOf(1)), not just vars
					////TODO: no magic numbers (e.g. "2" representing assumption of the first 2 parameters fixed

					//TODO: safe to mutate reference instead of copy?
					arithmetic::Expression send_expr = arithmetic::subExpr(e, operation.operands[2]);
					synthesizeOperandsFromExpression(send_expr, condition_idx, func, chp_to_flow_nets, g);
					send_expr.apply(chp_to_flow_nets);

					func.conds[condition_idx].req(flow_operand, send_expr);
					//cout << "* cond #" << condition_idx << " req'd " << channel_name << endl
					//	<< "w/ expr: " << send_expr << endl;

					//TODO: do we properly pipe #probes() down into Func para handle them later correctly in ModuleSynthesis & beyond?
					//} else if (func_name == "probe") {
						// used exclusively in these guards! They need valid signals too,
						// just not ready signal or channel for recv behavior
				}
			}
		}
	}


	int synthesizeConditionFromTransitions(
			const graph &g,
			const std::set<int> &transitions,
			arithmetic::Expression predicate,
			flow::Func &func,
			mapping &chp_to_flow_nets) {

		if (transitions.empty()) {
			return -1; // No transitions to process
		}

		// Properly synthesize condition predicate before assigning it to the condition
		int condition_idx = func.pushCond(Expression::undef());
		synthesizeOperandsFromExpression(predicate, condition_idx, func, chp_to_flow_nets, g);
		predicate.apply(chp_to_flow_nets);
		func.conds[condition_idx].valid = predicate;

		for (int transition_idx : transitions) {
			if (transition_idx < 0 || transition_idx >= (int)g.transitions.size()) {
				continue;  // skip invalid transitions
			}

			//cout << endl << "T" << transition_idx << endl;
			const chp::transition &transition = g.transitions[transition_idx];
			flow::Condition &cond = func.conds[condition_idx];

			// Crawl into every action
			const arithmetic::Choice &action = transition.action;
			for (const arithmetic::Parallel &term : action.terms) {
				for (const arithmetic::Action &action : term.actions) {

					//TODO: safe to mutate reference instead of copy?
					arithmetic::Expression expr(action.expr);
					synthesizeOperandsFromExpression(expr, condition_idx, func, chp_to_flow_nets, g);
					//expr.apply(chp_to_flow_nets);

					// Are we assigning to a local variable?
					if (action.variable != -1) {
						std::string chp_var_name = g.netAt(action.variable);
						int chp_var_idx = action.variable;
						Operand flow_operand = synthesizeOperandFromCHPVar(chp_var_name, chp_var_idx, flow::Net::REG, func, chp_to_flow_nets);

						expr.apply(chp_to_flow_nets);
						cond.mem(flow_operand, expr);
						//cout << "* cond #" << condition_idx << " mem'd " << chp_var_name << endl
						//	<< "in expr: " << mem_expr.to_string() << endl;
					}
				}
			}
		}

		return condition_idx;
	}


	flow::Func synthesizeFuncFromCHP(const graph &g) {
		flow::Func func;
		func.name = g.name;

		// Mapping from CHP variable indices to flow variable indices
		mapping chp_to_flow_nets;
		//auto set_or_get_flow_operand = [&chp_to_flow_nets](int flow_net) {};

		// Place index of split -> transition indices
		std::map<int, std::set<int>> splits;

		// Identify all split groups
		if (!g.split_groups_ready) {
				g.compute_split_groups();
		}

		// TODO: extract this directly from graph, which was already traversed
		for (size_t i = 0; i < g.transitions.size(); i++) {
			const chp::transition &transition = g.transitions[i];

			// Identify split groups
			petri::iterator it(petri::transition::type, (int)i);
			const vector<petri::split_group> &new_split_groups = g.split_groups_of(
					petri::composition::choice, it);

			if (!new_split_groups.empty()) {
				for (const petri::split_group &split_group : new_split_groups) {

					//TODO: Assumes there's one & always at least one transition here
					//TODO: Maybe include final transition id to cleanly detect cycle
					splits[split_group.split].insert(split_group.branch[0]);
				}
			}
		}

		//TODO: flatten split groups

		// Capture split-less/branch-less groups too
		if (splits.empty()) {
			set<int> all_transition_idxs;
			for (int i = 0; i < g.transitions.size(); i++) {
				all_transition_idxs.insert(i);
			}

			//TODO: search for predicate, in case it's not the first guarded transition (e.g. non-recurring start-up, Counter).
			// ...don't mistake any first guard for a predicate (e.g. buffer, copy). Op-specific?
			//int default_branch =
			synthesizeConditionFromTransitions(g, all_transition_idxs, Expression::boolOf(true), func, chp_to_flow_nets);
			return func;
		}

		// Crawl conditions in CHP graph (splits) to classify Func net vars
		for (const auto &[place_idx, transition_idxs] : splits) {
			for (const auto &split_transition_idx : transition_idxs) {
				std::set<int> region_idxs;

				// Traverse all transitions in this condition's cycle back to the split's place_idx
				vector<int> prev_place_idxs = g.prev(petri::transition::type, split_transition_idx);
				size_t transition_idx = split_transition_idx;
				do { 
					region_idxs.insert(transition_idx);

					++transition_idx;
					prev_place_idxs = g.prev(petri::transition::type, transition_idx);
					if (transition_idx == g.transitions.size()) { break; }
				} while (std::find(prev_place_idxs.begin(), prev_place_idxs.end(), place_idx) == prev_place_idxs.end());

				// Identify condition's predicate, if there is one
				arithmetic::Expression guard = g.transitions[split_transition_idx].guard;
				//if (!guard.top.isUndef()) { //TODO: ??? why is there a constant
				if (!guard.isValid()) {
					synthesizeConditionFromTransitions(g, region_idxs, guard, func, chp_to_flow_nets);

				} else {
					// int condition_idx = 
					synthesizeConditionFromTransitions(g, region_idxs, Expression::boolOf(true), func, chp_to_flow_nets);
				}
			}
		}

		return func;
	}
}
