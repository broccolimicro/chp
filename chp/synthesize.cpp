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
	mapping &channels;  // Mapping from CHP variable indices to flow variable indices
	bool debug;
};

std::ostream& operator<<(std::ostream &os, const SynthesisContext &c) {
	os << "SynthesisContext::channels => " << c.channels;
	return os;
}


arithmetic::Operand synthesizeChannelFromCHPVar(const string &chp_var_name, const int &chp_var_idx, const flow::Net::Purpose &purpose, SynthesisContext &context) {

	// Get or set flow operand for this channel
	Operand flow_operand;
	if (context.channels.has(chp_var_idx)) {
		int flow_var_idx = context.channels.map(chp_var_idx);
		flow_operand = Operand::varOf(flow_var_idx);  //TODO: preserve other Operand props?

		if (context.debug) {
			cout << "! HIT(" << chp_var_name << " @ CHP[" << chp_var_idx
				<< "]) => Flow[" << flow_var_idx << "]" << endl;
		}

	} else {
		//TODO: pipe appropriate width annotations via SynthesisContext
		size_t channel_width = (!chp_var_name.empty() && chp_var_name.back() == 'c') ? 1 : DATA_CHANNEL_WIDTH;  // Hack for short-term testing
		flow_operand = context.func.pushNet(chp_var_name, flow::Type(flow::Type::FIXED, channel_width), purpose);
		context.channels.set(chp_var_idx, static_cast<int>(flow_operand.index));

		if (context.debug) {
			cout << "? MISS(" << chp_var_name << " @ CHP[" << chp_var_idx
				<< "]) => Flow[" << static_cast<int>(flow_operand.index) << "]" << endl;
			cout << context;
		}
	}

	return flow_operand;
}


// Crawl sub-expression for vars that represent Channel names, then categorize them for context.func
void synthesizeChannelsInExpression(arithmetic::Expression &e, int condition_idx, SynthesisContext &context) {
	//auto operand_is_var = [](const arithmetic::Operand& op) -> bool { return op.isVar(); };
	//auto operand_to_net = [&g](const arithmetic::Operand& op) -> std::string { return context.g.netAt(op.index); };

	for (const arithmetic::Operand &operand : e.exprIndex()) {
		const arithmetic::Operation &operation = *e.getExpr(operand.index);
		if (operation.func != arithmetic::Operation::OpType::CALL) { continue; }  //TODO: other operations of interest?

		std::string func_name = operation.operands[0].cnst.sval;

		//TODO: optimize perf (don't do string comparison)
		if (func_name == "recv") {
			size_t channel_idx = e.getExpr(operation.operands[1].index)->operands[0].index;
			string channel_name = context.g.vars[channel_idx].name;
			Operand flow_operand = synthesizeChannelFromCHPVar(channel_name, channel_idx, flow::Net::IN, context);

			context.func.conds[condition_idx].ack(flow_operand);
			if (context.debug) { cout << "* cond #" << condition_idx << " ack'd " << channel_name << endl; }

		} else if (func_name == "send") {
			size_t channel_idx = e.getExpr(operation.operands[1].index)->operands[0].index;
			string channel_name = context.g.vars[channel_idx].name;
			Operand flow_operand = synthesizeChannelFromCHPVar(channel_name, channel_idx, flow::Net::OUT, context);
			if (context.debug) { cout << "* send on " << channel_name << "(" << channel_idx << ")" << endl; }

			////TODO: no magic numbers (e.g. "2" representing assumption of the first 2 parameters fixed
			arithmetic::Expression send_expr = arithmetic::subExpr(e, operation.operands[2]);
			//send_expr.minimize();
			synthesizeChannelsInExpression(send_expr, condition_idx, context);
			//if (!isSubexpression) { send_expr.apply(context.channels); }
			context.func.conds[condition_idx].req(flow_operand, send_expr);

			if (context.debug) {
				cout << "* cond #" << condition_idx << " req'd " << channel_name << endl
					<< "w/ expr: " << send_expr << endl;
			}

		} else if (func_name == "probe") {
			if (context.debug) { cout << "<><> PROBE op <><> " << operation << endl; }
			size_t expr_idx = operation.operands[1].index;

			const arithmetic::Operation &new_probe_operation = *e.getExpr(expr_idx);
			arithmetic::Operand probe_var = new_probe_operation.operands[0];
			if (context.debug) { cout << "<><> PROBE unwrap <><>" << probe_var << endl; }

			const size_t &channel_idx = probe_var.index;
			const string &channel_name = context.g.vars[channel_idx].name;
			Operand flow_operand = synthesizeChannelFromCHPVar(channel_name, channel_idx, flow::Net::IN, context);
			//probe_var.apply(context.channels);

			//e.sub.elems.eraseExpr() // DO NOT modify while iterating over
			//TODO: emplace_at new_probe_operation into SimpleOperationSet (or just the Operand into elems)

		} //TODO: else {}  for built-in functions?
	}
}


int synthesizeConditionFromTransitions(
		arithmetic::Expression predicate,
		const std::set<int> &transitions,
		SynthesisContext &context) {

	if (transitions.empty()) {
		return -1; // No transitions to process
	}

	// Properly synthesize condition predicate before assigning it to the condition
	int condition_idx = context.func.pushCond(Expression::undef());
	synthesizeChannelsInExpression(predicate, condition_idx, context);

	predicate.minimize();
	//predicate.apply(context.channels);
	context.func.conds[condition_idx].valid = predicate;

	for (int transition_idx : transitions) {
		if (transition_idx < 0 || transition_idx >= (int)context.g.transitions.size()) {
			continue;  // skip invalid transitions
		}

		if (context.debug) { cout << endl << "T" << transition_idx << endl; }
		const chp::transition &transition = context.g.transitions[transition_idx];
		flow::Condition &cond = context.func.conds[condition_idx];

		// Crawl into every action
		const arithmetic::Choice &action = transition.action;
		for (const arithmetic::Parallel &term : action.terms) {
			for (const arithmetic::Action &action : term.actions) {
				arithmetic::Expression expr(action.expr);
				synthesizeChannelsInExpression(expr, condition_idx, context);

				// Are we assigning to a local variable?
				if (action.variable != -1) {
					std::string chp_var_name = context.g.netAt(action.variable);
					int chp_var_idx = action.variable;
					Operand flow_operand = synthesizeChannelFromCHPVar(chp_var_name, chp_var_idx, flow::Net::REG, context);

					//TODO: Ignore local-only temporary variables
					expr.minimize();
					//expr.apply(context.channels);
					cond.mem(flow_operand, expr);

					if (context.debug) {
						cout << "* cond #" << condition_idx << " mem'd " << chp_var_name << endl
						<< "in expr: " << expr.to_string() << endl;
					}
				}
			}
		}
	}

	return condition_idx;
}


std::set<int> get_branch_transitions(const graph &g, const petri::iterator &dominator, const petri::iterator &branch_head, const SynthesisContext &context) {
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
		std::copy(branch_transition_idxs.begin(), branch_transition_idxs.end(), ostream_iterator<int>(cout, " "));
		cout << endl;
	}

	return branch_transition_idxs;
}


flow::Func synthesizeFuncFromCHP(const graph &g, bool debug) {
	flow::Func func;
	mapping channels;
	SynthesisContext context(g, func, channels, debug);
	context.func.name = g.name;

	if (context.debug) { cout << endl << "?? FLAT ENOUGH FOR SYNTHESIS? " << std::boolalpha << g.isFlat() << endl << endl; }

	// Confirm chp::graph has been normalized to flattened form & identify split-place dominator
	petri::iterator dominator;

	for (int place_idx = 0; place_idx < g.places.size(); place_idx++) {
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

		std::set<int> all_transition_idxs;
		for (int i = 0; i < g.transitions.size(); i++) {
			if (g.transitions.is_valid(i)) {
				all_transition_idxs.insert(i);
			}
		}

		//TODO: Can there be a non-always predicate in a guard-less default branch?
		synthesizeConditionFromTransitions(Expression::boolOf(true), all_transition_idxs, context);
		return context.func;
	}

	// Crawl each branch in flattened chp::graph
	for (const petri::iterator &branch_head : g.super::next(dominator)) {
		std::set<int> branch_transition_idxs = get_branch_transitions(g, dominator, branch_head, context);

		// Identify condition's predicate/condition, if there is one
		arithmetic::Expression guard = g.transitions[branch_head.index].guard;

		//guard = guard.isValid() ? Expression::boolOf(true) : guard;
		synthesizeConditionFromTransitions(guard, branch_transition_idxs, context);

		//std::list<int> branch_transition_idxs;
		////auto iterator_to_transition = [&g](const petri::iterator &it) { return g.transitions[it.index]; };
		//std::transform(branch_transitions.begin(), branch_transitions.end(), branch_transition_idxs.begin(),
		//		[](const petri::iterator &it) { return it.index; });
	}

	// Apply all mappings post-analysis
	for (auto condIt = func.conds.begin(); condIt != func.conds.end(); condIt++) {
		condIt->valid.minimize();
		condIt->valid.apply(context.channels);

		for (auto condRegIt = condIt->regs.begin(); condRegIt != condIt->regs.end(); condRegIt++) {
			condRegIt->second.minimize();
			condRegIt->second.apply(context.channels);
		}

		for (auto condOutIt = condIt->outs.begin(); condOutIt != condIt->outs.end(); condOutIt++) {
			condOutIt->second.minimize();
			condOutIt->second.apply(context.channels);
		}
	}

	return context.func;
}
}
