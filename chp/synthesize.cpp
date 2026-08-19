#include <algorithm>
#include <iostream>
#include <map>
#include <queue>
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

const size_t DATA_CHANNEL_WIDTH = 8;  //TODO: synthesize appropriate widths in SynthesisContext

namespace chp {

struct SynthesisContext {
	const chp::graph& g;
	flow::Func &func;
	bool debug;
};

void setPurpose(flow::Net &net, flow::Net::Purpose purpose) {
	if (net.purpose == flow::Net::NONE) {
		net.purpose = purpose;
	} else if (net.purpose != purpose) {
		error("", "conflicting usage of channel '" + net.name + "'", __FILE__, __LINE__);
	}
}

// Crawl sub-expression for vars that represent Channel names, then categorize them for context.func
void synthesizeChannelsInExpression(arithmetic::Expression &e, size_t condition_idx, SynthesisContext &context) {
	//auto operand_is_var = [](const arithmetic::Operand& op) -> bool { return op.isVar(); };
	//auto operand_to_net = [&g](const arithmetic::Operand& op) -> std::string { return context.g.netAt(op.index); };

	// First resolve all channel receives and probes into identity and add recvs to the ack list
	for (arithmetic::Operand &operand : e.exprIndex()) {
		arithmetic::Operation operation = *e.getExpr(operand.index);
		if (operation.func != arithmetic::Operation::CALL
			and operation.func != arithmetic::Operation::MEMBER_CALL) { continue; }  //TODO: other operations of interest?

		std::string func_name = operation.operands[0].cnst.sval;
		//TODO: optimize perf (don't do string comparison)
		if (func_name == "recv") {
			size_t channel_idx = lvalueBase(e, operation.operands[1]);
			if (channel_idx == std::numeric_limits<size_t>::max()) { continue; }

			string channel_name = context.g.vars[channel_idx].name;
			setPurpose(context.func.nets[channel_idx], flow::Net::IN);
			context.func.conds[condition_idx].ack(Operand::varOf(channel_idx));

			operation.operands.erase(operation.operands.begin());
			operation.func = arithmetic::Operation::IDENTITY;
			e.setExpr(operation);
			if (context.debug) { cout << "* cond #" << condition_idx << " ack'd " << channel_name << endl; }
		} else if (func_name == "probe") {
			if (context.debug) { cout << "<><> PROBE op <><> " << operation << endl; }
			size_t channel_idx = lvalueBase(e, operation.operands[1]);
			if (channel_idx == std::numeric_limits<size_t>::max()) { continue; }

			setPurpose(context.func.nets[channel_idx], flow::Net::IN);

			operation.operands.erase(operation.operands.begin());
			operation.func = arithmetic::Operation::IDENTITY;
			e.setExpr(operation);
		}
	}

	// Then handle all channel sends and internal memory
	for (const arithmetic::Operand &operand : e.exprIndex()) {
		const arithmetic::Operation &operation = *e.getExpr(operand.index);
		if (operation.func != arithmetic::Operation::CALL
			and operation.func != arithmetic::Operation::MEMBER_CALL) { continue; }  //TODO: other operations of interest?

		std::string func_name = operation.operands[0].cnst.sval;
		//TODO: optimize perf (don't do string comparison)
		if (func_name == "send") {
			size_t channel_idx = lvalueBase(e, operation.operands[1]);
			if (channel_idx == std::numeric_limits<size_t>::max()) { continue; }

			const string &channel_name = context.g.vars[channel_idx].name;
			setPurpose(context.func.nets[channel_idx], flow::Net::OUT);
			if (context.debug) { cout << "* send on " << channel_name << "(" << channel_idx << ")" << endl; }

			////TODO: no magic numbers (e.g. "2" representing assumption of the first 2 parameters fixed
			const Operand &send_operand = operation.operands[2];
			//const arithmetic::Operation &send_operation = *e.getExpr(operation.operands[2].index);
			arithmetic::Expression send_expr = arithmetic::subExpr(e, send_operand);

			context.func.conds[condition_idx].req(Operand::varOf(channel_idx), send_expr);

			if (context.debug) {
				cout << "* cond #" << condition_idx << " req'd " << channel_name << endl
					<< "w/ expr: " << send_expr << endl;
			}

		} else { // built-in functions (.e.g. "valid")

			//TODO: preserve multiple operands, not just one
			if (operation.operands.size() < 2) { continue; }  // only a func_name w/ no params? no subexpr to synthesize
			arithmetic::Expression call_expr = arithmetic::subExpr(e, operation.operands[1]);
			if (context.debug) { cout << "* calling \"" << func_name << "\"(" << call_expr << ")" << endl; }
		}
	}
}


size_t synthesizeConditionFromTransitions(
		arithmetic::Expression predicate,
		const std::set<size_t> &transitions,
		SynthesisContext &context) {

	if (transitions.empty()) { return -1; } // No transitions to process

	// Properly synthesize condition predicate before assigning it to the condition
	size_t condition_idx = context.func.pushCond(Expression::undef());
	synthesizeChannelsInExpression(predicate, condition_idx, context);

	predicate.minimize();
	context.func.conds[condition_idx].valid = predicate;

	for (size_t transition_idx : transitions) {
		if (transition_idx < 0 || transition_idx >= context.g.transitions.size()) {
			continue;  // skip invalid transitions
		}

		if (context.debug) { cout << endl << "T" << transition_idx << endl; }
		const chp::transition &transition = context.g.transitions[transition_idx];
		flow::Condition &cond = context.func.conds[condition_idx];

		// Crawl into every action
		const arithmetic::Choice &action = transition.action;
		for (const arithmetic::Parallel &term : action.terms) {
			for (const arithmetic::Action &action : term.actions) {
				arithmetic::Expression expr(action.rvalue);
				synthesizeChannelsInExpression(expr, condition_idx, context);

				// Are we assigning to a local variable?
				if (action.lvalue.isUndef()) { continue; }
				size_t chp_var_idx = arithmetic::lvalueBase(action.lvalue, action.lvalue.top);
				if (chp_var_idx == std::numeric_limits<size_t>::max()) { continue; }

				std::string chp_var_name = context.g.netAt(chp_var_idx);
				setPurpose(context.func.nets[chp_var_idx], flow::Net::REG);
				cond.mem(Operand::varOf(chp_var_idx), expr);

				if (context.debug) {
					cout << "* cond #" << condition_idx << " mem'd " << chp_var_name << endl
						<< "in expr: " << expr.to_string() << endl;
				}
				//break; ??
			}
		}
	}

	return condition_idx;
}


std::set<size_t> get_branch_transitions(const graph &g, const petri::iterator &dominator, const petri::iterator &branch_head, const SynthesisContext &context) {
	std::set<size_t> branch_transition_idxs = { static_cast<size_t>(branch_head.index) };

	// Breadth-first crawl every path of this branch to dominator
	std::set<petri::iterator> visited;
	std::queue<petri::iterator> q;
	petri::iterator curr;
	q.push(branch_head);

	while (not q.empty()) {
		curr = q.front();
		q.pop();
		visited.insert(curr);
		if (context.debug) { cout << endl << "[" << curr.to_string() << "] -> "; }

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

	if (context.debug) {
		cout << endl << "_=-+_=-+_=-+_=-> BRANCH: ";
		std::copy(branch_transition_idxs.begin(), branch_transition_idxs.end(), ostream_iterator<size_t>(cout, " "));
		cout << endl;
	}

	return branch_transition_idxs;
}


flow::Func synthesizeFuncFromCHP(const graph &g, bool debug) {
	flow::Func func;
	SynthesisContext context(g, func, debug);
	context.func.name = g.name;
	for (const auto &var : g.vars) {
		// DESIGN(edward.bingham) fill in purpose as we walk the graph

		// TODO(edward.bingham) do type lookup
		size_t width = (!var.name.empty() and var.name.back() == 'c') ? 1 : DATA_CHANNEL_WIDTH;  // Hack for short-term testing
		context.func.pushNet(var.name, flow::Type(flow::Type::FIXED, width));
	}

	if (context.debug) { cout << endl << "?? FLAT ENOUGH FOR SYNTHESIS? " << std::boolalpha << g.isFlat() << endl << endl; }

	// Confirm chp::graph has been normalized to flattened form & identify split-place dominator
	petri::iterator dominator;

	for (size_t place_idx = 0; place_idx < g.places.size(); place_idx++) {
		if (not g.places.is_valid(place_idx)) continue;

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
	if (context.debug) { cout << endl << "SYNTH DOM> " << dominator.to_string() << endl; }

	// Capture split-less/branch-less groups too
	if (dominator == -1) {
		//string msg = "ERROR: Dominator not found. chp::graph isn't ready for FlowSynthesis, because it's not `flat`. chp::graph::flatten() _should_ get it ready.";
		//cerr << msg << endl;
		//throw std::runtime_error(msg);

		std::set<size_t> all_transition_idxs;
		for (size_t i = 0; i < g.transitions.size(); i++) {
			if (g.transitions.is_valid(i)) {
				all_transition_idxs.insert(i);
			}
		}

		//TODO: Can there be a non-always predicate in a guard-less default branch?
		synthesizeConditionFromTransitions(Expression::boolOf(true), all_transition_idxs, context);

	} else {
		// Crawl each branch in flattened chp::graph
		for (const petri::iterator &branch_head : g.super::next(dominator)) {
			std::set<size_t> branch_transition_idxs = get_branch_transitions(g, dominator, branch_head, context);

			// Identify condition's predicate/condition, if there is one
			arithmetic::Expression guard = g.transitions[branch_head.index].guard;

			//guard = guard.isValid() ? Expression::boolOf(true) : guard;
			synthesizeConditionFromTransitions(guard, branch_transition_idxs, context);

			//std::list<size_t> branch_transition_idxs;
			////auto iterator_to_transition = [&g](const petri::iterator &it) { return g.transitions[it.index]; };
			//std::transform(branch_transitions.begin(), branch_transitions.end(), branch_transition_idxs.begin(),
			//		[](const petri::iterator &it) { return it.index; });
		}
	}

	// Apply all mappings post-analysis
	/*Expression x = Expression::varOf(0);
	arithmetic::RuleSet substitutions({
		//(a[b:c]) > (),
		(arithmetic::call("recv", {x})) > (x),
		(arithmetic::call("true", {x})) > (x),
		(arithmetic::isTrue(x)) > (x),
		(arithmetic::isValid(x)) > (x),
		(1 && x) > (x),
		(0 || x) > (x),
	});
	for (auto condIt = func.conds.begin(); condIt != func.conds.end(); condIt++) {
		condIt->valid.minimize(substitutions);
		condIt->valid.minimize();

		for (auto condRegIt = condIt->regs.begin(); condRegIt != condIt->regs.end(); condRegIt++) {
			condRegIt->second.minimize(substitutions);
			condRegIt->second.minimize();
		}

		for (auto condOutIt = condIt->outs.begin(); condOutIt != condIt->outs.end(); condOutIt++) {
			condOutIt->second.minimize(substitutions);
			condOutIt->second.minimize();
		}
	}*/

	return context.func;
}
}
