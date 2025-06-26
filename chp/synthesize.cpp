#include <iostream>
#include <map>
#include <ranges>
#include <string>
#include <vector>
#include <unordered_set>

#include "chp/synthesize.h"
#include "chp/graph.h"
#include "common/mapping.h"
#include "flow/func.h"

using namespace std;

namespace chp {

	void synthesizeFunc(flow::Func &out, const graph &base) {
		out.name = base.name;

		// Mapping from CHP variable indices to flow variable indices
		mapping chp_to_flow_nets;

		// Place index of split -> transition indices
		std::map<int, std::set<int>> splits;

		// Identify all split groups
		// TODO: extract this directly from graph, which was already traversed
		for (size_t i = 0; i < base.transitions.size(); i++) {
			const chp::transition &transition = base.transitions[i];

			// Identify split groups
			petri::iterator it(petri::transition::type, (int)i);
			const vector<petri::split_group> &new_split_groups = base.split_groups_of(
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

		// Crawl conditions in CHP graph (splits) to classify Func net vars
		for (const auto &[place_idx, transition_idxs] : splits) {
			for (const auto &split_transition_idx : transition_idxs) {
				const arithmetic::Expression &e = base.transitions[split_transition_idx].guard;
				int condition_idx = out.pushCond(e);

				// Traverse all transitions in this condition's cycle back to the split's place_idx
				vector<int> prev_place_idxs = base.prev(petri::transition::type, split_transition_idx);
				size_t transition_idx = split_transition_idx;
				do { 
					const chp::transition &transition = base.transitions[transition_idx];
					const arithmetic::Expression &guard = transition.guard;
					const arithmetic::Choice &action = transition.action;

					for (const arithmetic::Parallel &term : transition.action.terms) {
						for (const arithmetic::Action &action : term.actions) {

							// Register detected!
							if (action.variable != -1) {
								string net_name = base.netAt(action.variable);
								int chp_var_idx = action.variable;

								// Get or set Flow var for this CHP var
								Operand flow_operand;
								if (chp_to_flow_nets.has(action.variable)) {
									int flow_var_idx = chp_to_flow_nets.map(chp_var_idx);
									flow_operand = Operand::varOf(chp_to_flow_nets.map(chp_var_idx));

								} else {
									//TODO: quantify bus width? Assuming 1 for now
									int width = 1;
									flow_operand = out.pushNet(net_name, flow::Type(flow::Type::BITS, width), flow::Net::REG);
									chp_to_flow_nets.set(action.variable, static_cast<int>(flow_operand.index));
								}
								out.conds[condition_idx].mem(flow_operand, action.expr);
							}

							//TODO: um, are these mutually exclusive, reg vs in/out? No, we can have an expresion/call wihtin an assign

							// TODO: REACH INTO THE ACTION'S EXPRESSION FOR OPERATION(29) & when it's "recv/send", grab the first argument
							// TODO: OH OH DON'T FORGET THESE EXPRESSIONS CAN ALSO BE WITHIN TRANSITION GUARDS!
							if (!action.expr.operations.empty()) {									
								for (const arithmetic::Operation &operation : action.expr.operations) {
									if (operation.func == arithmetic::Operation::CALL) {
										auto operand_to_net = [&base](const arithmetic::Operand &op) -> string { return base.netAt(op.index); };
										auto operand_is_var = [](const arithmetic::Operand &op) -> bool { return op.isVar(); };
										auto var_operands = operation.operands | std::views::filter(operand_is_var);

										//TODO: optimize perf (don't do string comparison)
										string func_name = operation.operands[0].cnst.sval;
										if (func_name == "recv") {
											for (const auto &operand : var_operands) {
												string net_name = operand_to_net(operand);
												int chp_var_idx = operand.index; //base.netIndex(net_name);

												// Get or set Flow var for this CHP var
												Operand flow_operand;
												if (chp_to_flow_nets.has(chp_var_idx)) {
													int flow_var_idx = chp_to_flow_nets.map(chp_var_idx);
													flow_operand = Operand::varOf(flow_var_idx);  //TODO: preserve other Operand props?

												} else {
													//TODO: quantify bus width? Assuming 1 for now
													int width = 1;
													flow_operand = out.pushNet(net_name, flow::Type(flow::Type::FIXED, width), flow::Net::IN);
													chp_to_flow_nets.set(chp_var_idx, static_cast<int>(flow_operand.index));
												}
												out.conds[condition_idx].ack(flow_operand);
											}

										} else if (func_name == "send") {
											//TODO: don't forget to send raw constants (e.g. Operand::intOf(1)), not just vars
											string net_name = operand_to_net(operation.operands[1]);
											int chp_var_idx = base.netIndex(net_name);

											// Get or set Flow var for this CHP var
											Operand flow_operand;
											if (chp_to_flow_nets.has(chp_var_idx)) {
												int flow_var_idx = chp_to_flow_nets.map(chp_var_idx);
												flow_operand = Operand::varOf(chp_to_flow_nets.map(chp_var_idx));

											} else {
												//TODO: quantify bus width? Assuming 1 for now
												int width = 1;
												flow_operand = out.pushNet(net_name, flow::Type(flow::Type::FIXED, width), flow::Net::OUT);
												chp_to_flow_nets.set(chp_var_idx, static_cast<int>(flow_operand.index));
											}

											//TODO: no magic numbers (e.g. "2" representing assumption of the first 2 parameters fixed
											// For example, CALL(func="send", channel_name="channel_to_send_on", *vector of params)"
											vector<Operand> send_operands(operation.operands.begin() + 2, operation.operands.end());
											auto operand_to_expr = [](const Operand &operand) -> Expression { return Expression(operand); };
											vector<Expression> send_expressions;
											for (const auto &o : send_operands) {
												send_expressions.push_back(Expression(o));
											}
											Expression send_expression_array = arithmetic::array(send_expressions);
											out.conds[condition_idx].req(flow_operand, send_expression_array);

											//} else if (func_name == "probe") {
											//} else {
										}
									}
								}
							}
						}
					}

					++transition_idx;
					prev_place_idxs = base.prev(petri::transition::type, transition_idx);
					if (transition_idx == base.transitions.size()) { break; }
				} while (std::find(prev_place_idxs.begin(), prev_place_idxs.end(), place_idx) == prev_place_idxs.end());
			}
		}
	}
}
