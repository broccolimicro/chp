#include <algorithm>
#include <iostream>
#include <map>
#include <ranges>
#include <string>
#include <vector>

#include <arithmetic/algorithm.h>
#include <chp/graph.h>
#include <chp/synthesize.h>
#include <common/mapping.h>
#include <flow/func.h>

using namespace std;

using arithmetic::Expression;
using arithmetic::Operand;

const int BUS_WIDTH = 8;  //TODO: synthesize appropriate widths in SynthesisContext

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

			//auto operand_is_var = [](const arithmetic::Operand& op) -> bool { return op.isVar(); };
			//auto operand_to_net = [&g](const arithmetic::Operand& op) -> std::string { return g.netAt(op.index); };

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



std::set<int> get_branch_transitions(const graph &g, const petri::iterator &dominator, const petri::iterator &branch_head) {
	std::set<int> branch_transition_idxs = { branch_head.index };

	// Breadth-first crawl every path of this branch to dominator
	std::set<petri::iterator> visited;
	std::queue<petri::iterator> q;
	petri::iterator curr;
	q.push(branch_head);

	while (not q.empty()) {
		curr = q.front();
		q.pop();
		visited.insert(curr);
		//cout << endl << "[" << curr.to_string() << "] -> ";

		//TODO: context.g.super::out() sufficient? just cache all branch_heads in set to compare
		for (const petri::iterator &out_place : g.super::next(curr)) {
			if (out_place == dominator) { continue; }  // Back to where we started

			for (const petri::iterator &out_transition : g.super::next(out_place)) {
				if (visited.contains(out_transition)) { continue; }  // Already been here

				branch_transition_idxs.insert(out_transition.index);
				q.push(out_transition);
			}
		}
	}

	cout << endl << "_=-+_=-+_=-+_=-> BRANCH: ";
	std::copy(branch_transition_idxs.begin(), branch_transition_idxs.end(), ostream_iterator<int>(cout, " "));
	cout << endl;

	return branch_transition_idxs;
}


flow::Func synthesizeFuncFromCHP(const graph &g) {
	flow::Func func;
	mapping channels;
	SynthesisContext context(g, func, channels);
	context.func.name = g.name;

	// Confirm chp::graph has been normalized to flattened form & identify split-place dominator
	petri::iterator dominator;

	for (int place_idx = 0; place_idx < g.places.size(); place_idx++) {
		petri::iterator place_it(place::type, place_idx);

		vector<petri::iterator> in_transitions(g.super::next(place_it));
		vector<petri::iterator> out_transitions(g.super::prev(place_it));
		size_t in_count = in_transitions.size();
		size_t out_count = out_transitions.size();

		// Is graph ready, in flat form?
		if (in_count != out_count) {
			string msg = "ERROR: split-place with unequal ins & outs detected [" \
				+ std::to_string(place_idx) + "] => (" + std::to_string(in_count) + ", " + std::to_string(out_count) \
				+ "). chp::graph isn't ready for FlowSynthesis, because it's not `flat`. chp::graph::flatten() _should_ get it ready.";
			cerr << msg << endl;
			//throw std::runtime_error(msg);
		}

		if (out_count > 1) {
			if (dominator != -1) {
				string msg = "ERROR: multiple split-places detected. chp::graph isn't ready for FlowSynthesis, because it's not `flat`. chp::graph::flatten() _should_ get it ready.";
				cerr << msg << endl;
				throw std::runtime_error(msg);
			}

			dominator = place_it; // Found our dominator!
			break;
		}
	}
	cout << endl << "SYNTH DOM> " << dominator.to_string() << endl;

	// Capture split-less/branch-less groups too
	if (dominator == -1) {
		//string msg = "ERROR: Dominator not found. chp::graph isn't ready for FlowSynthesis, because it's not `flat`. chp::graph::flatten() _should_ get it ready.";
		//cerr << msg << endl;
		//throw std::runtime_error(msg);

		std::set<int> all_transition_idxs;
		for (int i = 0; i < g.transitions.size(); i++) {
			all_transition_idxs.insert(i);
		}

		//TODO: Can there be a non-always predicate in a guard-less default branch?
		synthesizeConditionFromTransitions(Expression::boolOf(true), all_transition_idxs, context);
		return context.func;
	}

	// Crawl each branch in flattened chp::graph
	for (const petri::iterator &branch_head : g.super::next(dominator)) {
		std::set<int> branch_transition_idxs = get_branch_transitions(g, dominator, branch_head);

		// Identify condition's predicate/condition, if there is one
		arithmetic::Expression guard = g.transitions[branch_head.index].guard;
		//if (!guard.top.isUndef()) { //TODO: ??? why is there a constant
		guard = guard.isValid() ? Expression::boolOf(true) : guard;

		synthesizeConditionFromTransitions(guard, branch_transition_idxs, context);

		//std::list<int> branch_transition_idxs;
		////auto iterator_to_transition = [&g](const petri::iterator &it) { return g.transitions[it.index]; };
		//std::transform(branch_transitions.begin(), branch_transitions.end(), branch_transition_idxs.begin(),
		//		[](const petri::iterator &it) { return it.index; });
	}

	return context.func;
}
}
