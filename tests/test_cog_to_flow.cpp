#include <gtest/gtest.h>

#include "helpers.h"
#include "dot.h"

#include <chp/synthesize.h>
#include <interpret_chp/export_dot.h>
#include <interpret_flow/export_dot.h>

const int WIDTH = 8;

void testFuncSynthesisFromCog(const flow::Func &expected, bool debug=false) {
	std::string cogRaw = readStringFromFile(SRC_DIR / (expected.name + ".cog"), false);
	chp::graph g = importCHPFromCogString(expected.name, cogRaw);

	expectProperNesting(g, debug);

	g.flatten();
	if (debug) {
		string chpGraphvizRaw = chp::export_graph(g, true).to_string();
		gvdot::render(BUILD_DIR / (expected.name + "_flattened.png"), chpGraphvizRaw);
	}

	flow::Func real = chp::synthesizeFuncFromCHP(g);
	if (debug) {
		string rflowGraphvizRaw = flow::export_func(real, true).to_string();
		gvdot::render(BUILD_DIR / (expected.name + "_flow_real.png"), rflowGraphvizRaw);
	
		string eflowGraphvizRaw = flow::export_func(expected, true).to_string();
		gvdot::render(BUILD_DIR / (expected.name + "_flow_expected.png"), eflowGraphvizRaw);
	}

	expectEq(real, expected);
}

/*
TEST(CogToFlow, ExpressionNets) {
	flow::Func func;
	Operand A = func.pushNet("A", flow::Type(flow::Type::FIXED, WIDTH), flow::Net::IN);
	Operand B = func.pushNet("B", flow::Type(flow::Type::FIXED, WIDTH), flow::Net::OUT);
	Expression expr_A(A);
	Expression expr_B(B);
	Expression recvA(arithmetic::call(arithmetic::Operand::Type::EXPR, {Expression::stringOf("recv"), expr_A}));
	Expression recvB(arithmetic::call(arithmetic::Operand::Type::EXPR, {Expression::stringOf("recv"), expr_B}));
	Expression sendA(arithmetic::call(arithmetic::Operand::Type::EXPR, {Expression::stringOf("send"), expr_A, Expression::intOf(1)}));
	Expression sendB(arithmetic::call(arithmetic::Operand::Type::EXPR, {Expression::stringOf("send"), expr_B, Expression::intOf(1)}));
	Expression expr_1(sendA || recvB);
	Expression expr_2(recvA || sendB);

	//TODO: pushCond for branch & grab .nets directly from func
	std::set<flow::Net> expectation = {
		flow::Net("branch_0", flow::Type(flow::Type::BITS, 1), flow::Net::COND),
		flow::Net("A", flow::Type(flow::Type::FIXED, WIDTH), flow::Net::IN),
		flow::Net("B", flow::Type(flow::Type::FIXED, WIDTH), flow::Net::OUT),
	};

	//std::set<flow::Net> reality =
	chp::synthesizeOperandsFromExpression(expr_1);

	chp::synthesizeOperandsFromExpression(expr_2);
	EXPECT_EQ(to_unordered_multiset(reality), to_unordered_multiset(expectation));
}
*/


TEST(CogToFlow, Counter) {
	flow::Func expected;
	expected.name = "counter";
	Operand i = expected.pushNet("i", flow::Type(flow::Type::FIXED, WIDTH), flow::Net::REG);
	Operand n = expected.pushNet("n", flow::Type(flow::Type::FIXED, WIDTH), flow::Net::WIRE);
	Expression expr_i(i);

	int branch0 = expected.pushCond(arithmetic::Expression::boolOf(true));
	expected.conds[branch0].mem(i, expr_i + 1);

	testFuncSynthesisFromCog(expected);
}


TEST(CogToFlow, Receiver) {
	flow::Func expected;
	expected.name = "receiver";
	Operand in = expected.pushNet("in", flow::Type(flow::Type::FIXED, WIDTH), flow::Net::IN);
	Operand msg = expected.pushNet("msg", flow::Type(flow::Type::FIXED, WIDTH), flow::Net::REG);
	Operand logger = expected.pushNet("logger", flow::Type(flow::Type::FIXED, WIDTH), flow::Net::OUT);
	Expression expr_in(in);
	Expression expr_msg(msg);
	Expression expr_logger(logger);

	int branch0 = expected.pushCond(expr_msg != "end");
	expected.conds[branch0].req(logger, expr_msg);
	expected.conds[branch0].mem(msg, expr_in);
	expected.conds[branch0].ack({in});

	int branch1 = expected.pushCond(expr_msg == "end");
	expected.conds[branch1].mem(msg, expr_in);
	expected.conds[branch1].ack({in});

	testFuncSynthesisFromCog(expected);
}


TEST(CogToFlow, TrafficLight) {
	flow::Func expected;
	expected.name = "traffic_light";
	Operand s = expected.pushNet("state", flow::Type(flow::Type::TypeName::FIXED, WIDTH), flow::Net::REG);
	Expression expr_s(s);
	Expression expr_green(Expression::stringOf("green"));
	Expression expr_yellow(Expression::stringOf("yellow"));
	Expression expr_red(Expression::stringOf("red"));

	int branch0 = expected.pushCond(expr_s == expr_red);
	expected.conds[branch0].mem(s, expr_green);

	int branch1 = expected.pushCond(expr_s == expr_green);
	expected.conds[branch1].mem(s, expr_yellow);

	int branch2 = expected.pushCond(expr_s == expr_yellow);
	expected.conds[branch2].mem(s, expr_red);

	testFuncSynthesisFromCog(expected);
}


TEST(CogToFlow, Buffer) {
	flow::Func expected;
	expected.name = "buffer";
	Operand L = expected.pushNet("L", flow::Type(flow::Type::TypeName::FIXED, WIDTH), flow::Net::IN);
	Operand R = expected.pushNet("R", flow::Type(flow::Type::TypeName::FIXED, WIDTH), flow::Net::OUT);
	Expression expr_L(L);

	int branch0 = expected.pushCond(Expression::boolOf(true));
	expected.conds[branch0].req(R, expr_L);
	expected.conds[branch0].ack(L);

	testFuncSynthesisFromCog(expected);
}


TEST(CogToFlow, Copy) {
	flow::Func expected;
	expected.name = "copy";
	Operand L = expected.pushNet("L", flow::Type(flow::Type::TypeName::FIXED, WIDTH), flow::Net::IN);
	Operand R0 = expected.pushNet("R0", flow::Type(flow::Type::TypeName::FIXED, WIDTH), flow::Net::OUT);
	Operand R1 = expected.pushNet("R1", flow::Type(flow::Type::TypeName::FIXED, WIDTH), flow::Net::OUT);
	Expression expr_L(L);

	int branch0 = expected.pushCond(Expression::boolOf(true));
	expected.conds[branch0].req(R0, expr_L);
	expected.conds[branch0].req(R1, expr_L);
	expected.conds[branch0].ack(L);

	testFuncSynthesisFromCog(expected);
}


TEST(CogToFlow, IsEven) {
	flow::Func expected;
	expected.name = "is_even";
	Operand L = expected.pushNet("L", flow::Type(flow::Type::TypeName::FIXED, WIDTH), flow::Net::IN);
	Operand R = expected.pushNet("R", flow::Type(flow::Type::TypeName::FIXED, 1), flow::Net::OUT);
	Expression expr_L(L);

	int branch0 = expected.pushCond(Expression::boolOf(true));
	expected.conds[branch0].req(R, expr_L % 2 == 0);
	expected.conds[branch0].ack({L});

	testFuncSynthesisFromCog(expected);
}


TEST(CogToFlow, Func) {
	flow::Func expected;
	expected.name = "expected";
	Operand L0 = expected.pushNet("L0", flow::Type(flow::Type::TypeName::FIXED, WIDTH), flow::Net::IN);
	Operand L1 = expected.pushNet("L1", flow::Type(flow::Type::TypeName::FIXED, WIDTH), flow::Net::IN);
	Operand R = expected.pushNet("R", flow::Type(flow::Type::TypeName::FIXED, WIDTH), flow::Net::OUT);
	Expression expr_L0(L0);
	Expression expr_L1(L1);

	int branch0 = expected.pushCond(Expression::boolOf(true));
	expected.conds[branch0].req(R, expr_L0 || expr_L1);
	//expected.conds[branch0].mem(m_or, expr_L0 || expr_L1);
	expected.conds[branch0].ack({L0, L1});

	testFuncSynthesisFromCog(expected);
}


TEST(CogToFlow, Merge) {
	flow::Func expected;
	expected.name = "merge";
	Operand L0 = expected.pushNet("L0", flow::Type(flow::Type::TypeName::FIXED, WIDTH), flow::Net::IN);
	Operand L1 = expected.pushNet("L1", flow::Type(flow::Type::TypeName::FIXED, WIDTH), flow::Net::IN);
	Operand C = expected.pushNet("C", flow::Type(flow::Type::TypeName::FIXED, 1), flow::Net::IN);
	Operand R = expected.pushNet("R", flow::Type(flow::Type::TypeName::FIXED, WIDTH), flow::Net::OUT);
	Expression exprL0(L0);
	Expression exprL1(L1);
	Expression exprC(C);

	int branch0 = expected.pushCond(exprC == Expression::intOf(0));
	expected.conds[branch0].req(R, exprL0);
	expected.conds[branch0].ack({C, L0});

	int branch1 = expected.pushCond(exprC == Expression::intOf(1));
	expected.conds[branch1].req(R, exprL1);
	expected.conds[branch1].ack({C, L1});

	testFuncSynthesisFromCog(expected);
}


TEST(CogToFlow, Split) {
	flow::Func expected;
	expected.name = "split";
	Operand L = expected.pushNet("L", flow::Type(flow::Type::TypeName::FIXED, WIDTH), flow::Net::IN);
	Operand C = expected.pushNet("C", flow::Type(flow::Type::TypeName::FIXED, 1), flow::Net::IN);
	Operand R0 = expected.pushNet("R0", flow::Type(flow::Type::TypeName::FIXED, WIDTH), flow::Net::OUT);
	Operand R1 = expected.pushNet("R1", flow::Type(flow::Type::TypeName::FIXED, WIDTH), flow::Net::OUT);
	Expression exprL(L);
	Expression exprC(C);

	int branch0 = expected.pushCond(exprC == Expression::intOf(0));
	expected.conds[branch0].req(R0, exprL);
	expected.conds[branch0].ack({C, L});

	int branch1 = expected.pushCond(exprC == Expression::intOf(1));
	expected.conds[branch1].req(R1, exprL);
	expected.conds[branch1].ack({C, L});

	testFuncSynthesisFromCog(expected);
}

TEST(CogToFlow, SerialAdder) {
	flow::Func expected;
	expected.name = "serial_adder";
	Operand Ad = expected.pushNet("Ad", flow::Type(flow::Type::FIXED, WIDTH), flow::Net::IN);
	Operand Ac = expected.pushNet("Ac", flow::Type(flow::Type::FIXED, 1), flow::Net::IN);
	Operand Bd = expected.pushNet("Bd", flow::Type(flow::Type::FIXED, WIDTH), flow::Net::IN);
	Operand Bc = expected.pushNet("Bc", flow::Type(flow::Type::FIXED, 1), flow::Net::IN);
	Operand Sd = expected.pushNet("Sd", flow::Type(flow::Type::FIXED, WIDTH), flow::Net::OUT);
	Operand Sc = expected.pushNet("Sc", flow::Type(flow::Type::FIXED, 1), flow::Net::OUT);
	Operand ci = expected.pushNet("ci", flow::Type(flow::Type::BITS,  1), flow::Net::REG);
	Expression exprAc(Ac);
	Expression exprAd(Ad);
	Expression exprBc(Bc);
	Expression exprBd(Bd);
	Expression exprci(ci);

	Expression s((exprAd + exprBd + exprci)(0, WIDTH-1));
	Expression co((exprAd + exprBd + exprci)(WIDTH-1));

	int branch0 = expected.pushCond(!exprAc && !exprBc);
	expected.conds[branch0].req(Sd, s);
	expected.conds[branch0].req(Sc, Operand::intOf(0));
	expected.conds[branch0].mem(ci, co);
	expected.conds[branch0].ack({Ac, Ad, Bc, Bd});

	int branch1 = expected.pushCond(exprAc && !exprBc);
	expected.conds[branch1].req(Sd, s);
	expected.conds[branch1].req(Sc, Operand::intOf(0));
	expected.conds[branch1].mem(ci, co);
	expected.conds[branch1].ack({Bc, Bd});

	int branch2 = expected.pushCond(!exprAc && exprBc);
	expected.conds[branch2].req(Sd, s);
	expected.conds[branch2].req(Sc, Operand::intOf(0));
	expected.conds[branch2].mem(ci, co);
	expected.conds[branch2].ack({Ac, Ad});

	int branch3 = expected.pushCond(exprAc && exprBc && (co != exprci));
	expected.conds[branch3].req(Sd, s);
	expected.conds[branch3].req(Sc, Operand::intOf(0));
	expected.conds[branch3].mem(ci, co);

	int branch4 = expected.pushCond(exprAc && exprBc && (co == exprci));
	expected.conds[branch4].req(Sd, s);
	expected.conds[branch4].req(Sc, Operand::intOf(1));
	expected.conds[branch4].mem(ci, Operand::intOf(0));
	expected.conds[branch4].ack({Ac, Ad, Bc, Bd});

	testFuncSynthesisFromCog(expected);
}

TEST(CogToFlow, SerialAdderFlat) {
	flow::Func expected;
	expected.name = "serial_adder_flat";
	Operand Ad = expected.pushNet("Ad", flow::Type(flow::Type::FIXED, WIDTH), flow::Net::IN);
	Operand Ac = expected.pushNet("Ac", flow::Type(flow::Type::FIXED, 1), flow::Net::IN);
	Operand Bd = expected.pushNet("Bd", flow::Type(flow::Type::FIXED, WIDTH), flow::Net::IN);
	Operand Bc = expected.pushNet("Bc", flow::Type(flow::Type::FIXED, 1), flow::Net::IN);
	Operand Sd = expected.pushNet("Sd", flow::Type(flow::Type::FIXED, WIDTH), flow::Net::OUT);
	Operand Sc = expected.pushNet("Sc", flow::Type(flow::Type::FIXED, 1), flow::Net::OUT);
	Operand ci = expected.pushNet("ci", flow::Type(flow::Type::BITS,  1), flow::Net::REG);
	Expression exprAc(Ac);
	Expression exprAd(Ad);
	Expression exprBc(Bc);
	Expression exprBd(Bd);
	Expression exprci(ci);

	Expression s((exprAd + exprBd + exprci)(0, WIDTH-1));
	Expression co((exprAd + exprBd + exprci)(WIDTH-1));

	int branch0 = expected.pushCond(!exprAc && !exprBc);
	expected.conds[branch0].req(Sd, s);
	expected.conds[branch0].req(Sc, Operand::intOf(0));
	expected.conds[branch0].mem(ci, co);
	expected.conds[branch0].ack({Ac, Ad, Bc, Bd});

	int branch1 = expected.pushCond(exprAc && !exprBc);
	expected.conds[branch1].req(Sd, s);
	expected.conds[branch1].req(Sc, Operand::intOf(0));
	expected.conds[branch1].mem(ci, co);
	expected.conds[branch1].ack({Bc, Bd});

	int branch2 = expected.pushCond(!exprAc && exprBc);
	expected.conds[branch2].req(Sd, s);
	expected.conds[branch2].req(Sc, Operand::intOf(0));
	expected.conds[branch2].mem(ci, co);
	expected.conds[branch2].ack({Ac, Ad});

	int branch3 = expected.pushCond(exprAc && exprBc && (co != exprci));
	expected.conds[branch3].req(Sd, s);
	expected.conds[branch3].req(Sc, Operand::intOf(0));
	expected.conds[branch3].mem(ci, co);

	int branch4 = expected.pushCond(exprAc && exprBc && (co == exprci));
	expected.conds[branch4].req(Sd, s);
	expected.conds[branch4].req(Sc, Operand::intOf(1));
	expected.conds[branch4].mem(ci, Operand::intOf(0));
	expected.conds[branch4].ack({Ac, Ad, Bc, Bd});

	testFuncSynthesisFromCog(expected);
}

TEST(CogToFlow, Instance) {
	flow::Func expected;
	expected.name = "instance";
	Operand L0 = expected.pushNet("L0", flow::Type(flow::Type::TypeName::FIXED, WIDTH), flow::Net::IN);
	Operand L1 = expected.pushNet("L1", flow::Type(flow::Type::TypeName::FIXED, WIDTH), flow::Net::IN);
	Operand R = expected.pushNet("R", flow::Type(flow::Type::TypeName::FIXED, WIDTH), flow::Net::OUT);
	Operand M = expected.pushNet("M", flow::Type(flow::Type::TypeName::FIXED, WIDTH), flow::Net::WIRE);
	Expression expr_L0(L0);
	Expression expr_L1(L1);
	Expression expr_M(M);

	int branch0 = expected.pushCond(Expression::boolOf(true));
	expected.conds[branch0].req(R, arithmetic::call("myFunc", {expr_M}));
	expected.conds[branch0].ack({M});

	expected.inst.push_back(flow::Instance("myProcess", {expr_L0, expr_L1, expr_M}));

	testFuncSynthesisFromCog(expected);
}

/*
TEST(CogToFlow, WCHB1B) {
	string test_name = "wchb1b";
	flow::Func func_real = synthesizeFuncFromCogTestFile(test_name);
}


TEST(CogToFlow, WCHBStream) {
	string test_name = "wchb_stream";
	flow::Func func_real = synthesizeFuncFromCogTestFile(test_name);
}


TEST(CogToFlow, WCHBFullAdder) {
	string test_name = "wchb_full_adder";
	flow::Func func_real = synthesizeFuncFromCogTestFile(test_name);
}
*/
