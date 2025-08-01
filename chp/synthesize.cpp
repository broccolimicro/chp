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

const int BUS_WIDTH = 8;

namespace chp {

  //TODO: RETVRN HERE
  //TODO: missing nets from predicate, request, (even mem?) expressions ...and ack's?
  //TODO: try just grabbing everything from chp_to_flow_nets
  //TODO: also either ignore or match Condition uids: how is it getting auto-incremented beforehand?

  int synthesizeConditionFromTransitions(
      const graph& g,
      const std::set<int>& transitions,
      arithmetic::Expression predicate,
      flow::Func& func,
      mapping& chp_to_flow_nets) {

    if (transitions.empty()) {
      return -1; // No transitions to process
    }

    int condition_idx = func.pushCond(predicate);

    // Process each transition in the conditional
    for (int transition_idx : transitions) {
      if (transition_idx < 0 || transition_idx >= (int)g.transitions.size()) {
        continue; // Skip invalid transition indices
      }

      const chp::transition &transition = g.transitions[transition_idx];
      //const arithmetic::Choice &action = transition.action;
      ////cout << endl << "T" << transition_idx << endl;

      // Process action terms
      flow::Condition &cond = func.conds[condition_idx];
      for (const arithmetic::Parallel &term : transition.action.terms) {
        for (const arithmetic::Action &action : term.actions) {
          /*
             cout << " > T.action(arith::Choice).terms(arith::Parallel)" << endl << "        "; //<< term << ") ";
             if (term.isInfeasible()) { cout << " Infeasible "; }
             if (term.isVacuous()) { cout << " Vacuous "; }
             if (term.isPassive()) { cout << " Passive "; }
             cout << endl;

             cout << "term.ACTIONS:" << endl;
           */

          // Handle variable assignments (registers)
          if (action.variable != -1) {
            //cout << "* action(var: " << action.variable << ", net: " << g.netAt(action.variable) << ") " << endl;//action.expr.to_string() << endl;
            std::string net_name = g.netAt(action.variable);
            int chp_var_idx = action.variable;

            // Get or create flow variable
            Operand flow_operand;
            if (chp_to_flow_nets.has(chp_var_idx)) {
              int flow_var_idx = chp_to_flow_nets.map(chp_var_idx);
              flow_operand = Operand::varOf(flow_var_idx);
              //cout << "! HIT(" << net_name << " @ CHP[" << chp_var_idx
              //  << "]) => Flow[" << flow_var_idx << "]" << endl;

            } else {
              flow_operand = func.pushNet(net_name, flow::Type(flow::Type::FIXED, BUS_WIDTH), flow::Net::REG);
              chp_to_flow_nets.set(chp_var_idx, static_cast<int>(flow_operand.index));
              //cout << "? MISS(" << net_name << " @ CHP[" << chp_var_idx
              //  << "]) => Flow[" << static_cast<int>(flow_operand.index) << "]" << endl;
            }

            // Add the assignment to the condition
            cond.mem(flow_operand, action.expr);
            //cout << "* cond #" << condition_idx << " mem'd " << net_name << endl;
          }

          //TODO: um, are these mutually exclusive, reg vs in/out? No, we can have an expresion/call wihtin an assign

          // TODO: REACH INTO THE ACTION'S EXPRESSION FOR OPERATION(29) & when it's "recv/send", grab the first argument
          // TODO: OH OH DON'T FORGET THESE EXPRESSIONS CAN ALSO BE WITHIN TRANSITION GUARDS!

          // Process any expressions in the action
          if (!action.expr.top.isUndef()) {
            for (const arithmetic::Operand& op : action.expr.exprIndex()) {
              const arithmetic::Operation &operation = *action.expr.getExpr(op.index);

              //if (operation) {}
              if (operation.func == arithmetic::Operation::CALL) {
                auto operand_to_net = [&g](const arithmetic::Operand& op) -> std::string {
                  return g.netAt(op.index);
                };
                auto operand_is_var = [](const arithmetic::Operand& op) -> bool {
                  return op.isVar();
                };

                std::string func_name = operation.operands[0].cnst.sval;
                //cout << "* " << func_name << "():";
                //for (const auto &operand : var_operands) {
                //  cout << " " << operand_to_net(operand);
                //}
                //cout << endl;

                //if (func_name == "recv" || func_name == "send") {
                //  for (const arithmetic::Operand &operand : operation.operands) {
                //    if (!operand.isVar()) continue;

                //    std::string net_name = operand_to_net(operand);
                //    int chp_var_idx = operand.index;

                //    // Get or create flow variable for the channel
                //    if (!chp_to_flow_nets.has(chp_var_idx)) {
                //      flow::Net::Purpose purpose = (func_name == "recv") 
                //        ? flow::Net::IN 
                //        : flow::Net::OUT;

                //      Operand flow_operand = func.pushNet(
                //          net_name,
                //          flow::Type(flow::Type::FIXED, BUS_WIDTH),
                //          purpose
                //          );
                //      chp_to_flow_nets.set(action.variable, static_cast<int>(flow_operand.index));

                //      // Add to the condition's inputs/outputs
                //      if (purpose == flow::Net::IN) {
                //        cond.ack(flow_operand);

                //      } else {
                //        //TODO: no magic numbers (e.g. "2" representing assumption of the first 2 parameters fixed
                //        // For example, CALL(func="send", channel_name="channel_to_send_on", *vector of params)"
                //        vector<Operand> send_operands(operation.operands.begin() + 2, operation.operands.end());

                //        //auto operand_to_expr = [](const Operand &operand) -> Expression { return Expression(operand); };
                //        vector<Expression> send_expressions;
                //        for (const auto &o : send_operands) {
                //          send_expressions.push_back(arithmetic::subExpr(action.expr, o));
                //        }

                //        Expression send_expression_array = arithmetic::array(send_expressions);
                //        cond.req(flow_operand, send_expression_array);
                //      }
                //    }
                //  }
                //}

                //TODO: optimize perf (don't do string comparison)
                if (func_name == "recv") {
                  auto var_operands = operation.operands | std::views::filter(operand_is_var);
                  for (const auto &operand : var_operands) {
                    string net_name = operand_to_net(operand);
                    int chp_var_idx = operand.index; //g.netIndex(net_name);

                    // Get or set Flow var for this CHP var
                    Operand flow_operand;
                    if (chp_to_flow_nets.has(chp_var_idx)) {
                      int flow_var_idx = chp_to_flow_nets.map(chp_var_idx);
                      flow_operand = Operand::varOf(flow_var_idx);  //TODO: preserve other Operand props?
                                                                    //cout << "! HIT(" << net_name << " @ CHP[" << chp_var_idx
                                                                    //  << "]) => Flow[" << flow_var_idx << "]" << endl;

                    } else {
                      flow_operand = func.pushNet(net_name, flow::Type(flow::Type::FIXED, BUS_WIDTH), flow::Net::IN);
                      chp_to_flow_nets.set(chp_var_idx, static_cast<int>(flow_operand.index));
                      //cout << "? MISS(" << net_name << " @ CHP[" << chp_var_idx
                      //  << "]) => Flow[" << static_cast<int>(flow_operand.index) << "]" << endl;
                    }
                    func.conds[condition_idx].ack(flow_operand);
                    //cout << "* cond #" << condition_idx << " ack'd " << net_name << endl;
                  }

                } else if (func_name == "send") {
                  //TODO: don't forget to send raw constants (e.g. Operand::intOf(1)), not just vars
                  string net_name = operand_to_net(operation.operands[1]);
                  int chp_var_idx = g.netIndex(net_name);

                  // Get or set Flow var for this CHP var
                  Operand flow_operand;
                  if (chp_to_flow_nets.has(chp_var_idx)) {
                    int flow_var_idx = chp_to_flow_nets.map(chp_var_idx);
                    flow_operand = Operand::varOf(chp_to_flow_nets.map(chp_var_idx));
                    //cout << "! HIT(" << net_name << " @ CHP[" << chp_var_idx
                    //  << "]) => Flow[" << flow_var_idx << "]" << endl;

                  } else {
                    flow_operand = func.pushNet(net_name, flow::Type(flow::Type::FIXED, BUS_WIDTH), flow::Net::OUT);
                    chp_to_flow_nets.set(chp_var_idx, static_cast<int>(flow_operand.index));
                    //cout << "? MISS(" << net_name << " @ CHP[" << chp_var_idx
                    //  << "]) => Flow[" << static_cast<int>(flow_operand.index) << "]" << endl;
                  }

                  //TODO: no magic numbers (e.g. "2" representing assumption of the first 2 parameters fixed
                  // For example, CALL(func="send", channel_name="channel_to_send_on", *vector of params)"
                  vector<Operand> send_operands(operation.operands.begin() + 2, operation.operands.end());
                  //auto operand_to_expr = [](const Operand &operand) -> Expression { return Expression(operand); };
                  vector<Expression> send_expressions;
                  for (const auto &operand : send_operands) {
                    send_expressions.push_back(arithmetic::subExpr(action.expr, operand));
                  }
                  Expression send_expression_array = arithmetic::array(send_expressions);
                  func.conds[condition_idx].req(flow_operand, send_expression_array);
                  //cout << "* cond #" << condition_idx << " req'd " << net_name << endl;

                  //} else if (func_name == "probe") { //used exclusively in these guards! they need valid signals too, just not ready singla or channel for recv behavior
                }
              }
            }
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
    /*
    for (size_t place_idx = 0; place_idx < g.places.size(); place_idx++) {
      const auto& place = g.places[place_idx];
      const auto& choice_splits = place.splits[petri::choice];

      for (const auto& split_group : choice_splits) {
        if (split_group.count > 1) {
          for (const auto& transition_idx : g.next(petri::transition::type, place_idx)) {
            cout << split_group.count << "(" << place_idx << ", " << transition_idx << ")" << endl;
          }
        }
        if (split_group.branch.size() > 1) {
          splits[place_idx].insert(split_group.branch.begin(), split_group.branch.end());
        }
      }
    }
    */

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

        arithmetic::Expression guard = g.transitions[split_transition_idx].guard;
        if (!guard.top.isUndef()) {
          // Process the guard condition
          guard.apply(chp_to_flow_nets);
          synthesizeConditionFromTransitions(g, region_idxs, guard, func, chp_to_flow_nets);

        } else {
          // int condition_idx = 
          synthesizeConditionFromTransitions(g, region_idxs, Expression::boolOf(true), func, chp_to_flow_nets);
        }
      }
    }

    /*
    for (const auto &c : func.conds) {
      cout << endl << "========================" << endl
        << c.toString() << endl << endl;
    }

    // Explore vars for potential nets
    for (const chp::variable &var : g.vars) {
      //func.pushNet(var.name); //TODO: width? out, reg, or in?
      cout << "* g.var(" << g.netIndex(var.name) << " => " << var.name << "): region " << var.region << endl;
    }

    cout << endl << endl << "=======\\==============/=======\\==/===========\\==============/=======\\==/====" << endl;
    cout << chp_to_flow_nets << endl;
    cout << "MAP {" << endl;
    for (int flow_var_idx : chp_to_flow_nets.nets) {
      int chp_var_idx = chp_to_flow_nets.unmap(flow_var_idx);
      cout << "* CHP.var(" << setw(2) << setfill(' ') << g.netAt(chp_var_idx) << " @ " << chp_var_idx << ") -> Flow.var(" << setw(2) << setfill(' ') << func.netAt(flow_var_idx) << " @ " << flow_var_idx << ")" << endl;
    }
    cout << "}" << endl;
    cout << endl << endl;
    */

    return func;
  }
}
