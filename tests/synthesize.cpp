#include <algorithm>
#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include <chp/graph.h>
#include <chp/synthesize.h>
#include <common/standard.h>
#include <flow/func.h>
#include <verilog/module.h>
#include <flow/synthesize.h>

#include <interpret_chp/export_dot.h>
#include <interpret_chp/import_chp.h>
#include <interpret_chp/import_cog.h>
#include <interpret_verilog/export_verilog.h>
#include <interpret_flow/export_dot.h>
#include <interpret_flow/export_cog.h>

#include <parse/default/block_comment.h>
#include <parse/default/line_comment.h>
#include <parse/tokenizer.h>
#include <parse_chp/composition.h>
#include <parse_chp/factory.h>
#include <parse_cog/composition.h>
#include <parse_cog/control.h>
#include <parse_cog/factory.h>

#include "dot.h"

using namespace std;  //TODO: use only what you need
using namespace flow; //TODO: be explicit outside of flow submodule
namespace fs = std::filesystem;

const fs::path SRC_DIR = fs::absolute(fs::current_path() / "tests" / "src");
const fs::path BUILD_DIR = fs::absolute(fs::current_path() / "tests" / "build");
const int WIDTH = 8;

chp::graph importCHPFromCogString(const string &cog, bool debug=false) {
	tokenizer tokens;
	tokens.register_token<parse::block_comment>(false);
	tokens.register_token<parse::line_comment>(false);
	parse_cog::factory.register_syntax(tokens);

	tokens.insert("string_input", cog, nullptr);
	chp::graph g;

	tokens.increment(false);
	tokens.expect<parse_cog::composition>();
	if (tokens.decrement(__FILE__, __LINE__)) {
		parse_cog::composition syntax(tokens);
		
		// Blame parser or interpreter?
		if (debug) { cout << syntax.to_string() << endl; }
		chp::import_chp(g, syntax, &tokens, true);
	}
	return g;
}

string readStringFromFile(const string &absolute_filename, bool strip_newlines=false) {
	ifstream in(absolute_filename, ios::in | ios::binary);
	EXPECT_NO_THROW({
		if (!in) throw std::runtime_error("Failed to open file: " + absolute_filename);
	});

	ostringstream contents;
	contents << in.rdbuf();
	string result = contents.str();

	// Replace all newlines with spaces
	if (strip_newlines) {
		 replace(result.begin(), result.end(), '\n', ' ');
		 replace(result.begin(), result.end(), '\r', ' '); // for Windows files
	}

	return result;
}


template <typename T>
auto to_unordered_multiset = [](const std::vector<T>& v) {
    return std::multiset<T>(v.begin(), v.end());
};


bool areEquivalent(flow::Func &real, flow::Func &expected) {
	//TODO: insensitive to ids and condition order

	EXPECT_EQ(real.nets.size(), expected.nets.size());
	//std::sort(real.nets.begin(), real.nets.end());
	//std::sort(expected.nets.begin(), expected.nets.end());
	auto real_nets = to_unordered_multiset<flow::Net>(real.nets);
	auto expected_nets = to_unordered_multiset<flow::Net>(expected.nets);
	EXPECT_EQ(real_nets, expected_nets); 

	EXPECT_EQ(real.conds.size(), expected.conds.size());
	//std::sort(real.conds.begin(), real.conds.end());
	//std::sort(expected.conds.begin(), expected.conds.end());
	auto real_conds = to_unordered_multiset<flow::Condition>(real.conds);
	auto expected_conds = to_unordered_multiset<flow::Condition>(expected.conds);
	EXPECT_EQ(real_conds, expected_conds);

	return true;
}

void testFuncSynthesisFromCog(flow::Func &expected, bool render=true) {
	string cogRaw = readStringFromFile(SRC_DIR / (expected.name + ".cog"), false);
	chp::graph g = importCHPFromCogString(cogRaw);
	g.post_process(true, false);  //TODO: ... true, true)
	g.name = expected.name;

	fs::create_directories(BUILD_DIR);

	g.flatten();
	if (render) {
		string chpGraphvizRaw = chp::export_graph(g, true).to_string();
		gvdot::render(BUILD_DIR / (expected.name + ".png"), chpGraphvizRaw);
	}

	flow::Func real = chp::synthesizeFuncFromCHP(g);
	if (render) {
		string rflowGraphvizRaw = flow::export_func(real, true).to_string();
		gvdot::render(BUILD_DIR / (expected.name + "_flow_real.png"), rflowGraphvizRaw);
	
		string eflowGraphvizRaw = flow::export_func(expected, true).to_string();
		gvdot::render(BUILD_DIR / (expected.name + "_flow_expected.png"), eflowGraphvizRaw);
	}

	cout << parse_cog::export_func(real).to_string("") << endl;

	EXPECT_EQ(real.name, expected.name);
	EXPECT_TRUE(areEquivalent(real, expected));

	/* TODO: ideal API?
	parse_cog::composition cog = importCogFromFile(cog_filename);
	chp::graph g = synthesizeCHPFromCog(cog);
	flow::Func func_real = chp::syntheiszeFuncFromCHP(g);
	*/
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
	flow::Func func;
	func.name = "counter";
	Operand n = func.pushNet("n", flow::Type(flow::Type::FIXED, WIDTH), flow::Net::IN);
	Operand i = func.pushNet("i", flow::Type(flow::Type::FIXED, WIDTH), flow::Net::REG);
	Expression expr_i(i);

	int branch0 = func.pushCond(arithmetic::Expression::boolOf(true));
	func.conds[branch0].mem(i, expr_i + 1);

	testFuncSynthesisFromCog(func);
}


TEST(CogToFlow, Receiver) {
	flow::Func func;
	func.name = "receiver";
	Operand in = func.pushNet("in", flow::Type(flow::Type::FIXED, WIDTH), flow::Net::IN);
	Operand msg = func.pushNet("msg", flow::Type(flow::Type::FIXED, WIDTH), flow::Net::REG);
	Operand logger = func.pushNet("logger", flow::Type(flow::Type::FIXED, WIDTH), flow::Net::OUT);
	Expression expr_in(in);
	Expression expr_msg(msg);
	Expression expr_logger(logger);

	int branch0 = func.pushCond(expr_msg != "end");
	func.conds[branch0].req(logger, expr_msg);
	func.conds[branch0].mem(msg, expr_in);
	func.conds[branch0].ack({in});

	int branch1 = func.pushCond(expr_msg == "end");
	func.conds[branch1].mem(msg, expr_in);
	func.conds[branch1].ack({in});

	testFuncSynthesisFromCog(func);
}


TEST(CogToFlow, TrafficLight) {
	flow::Func func;
	func.name = "traffic_light";
	Operand s = func.pushNet("state", flow::Type(flow::Type::TypeName::FIXED, WIDTH), flow::Net::REG);
	Expression expr_s(s);
	Expression expr_green(Expression::stringOf("green"));
	Expression expr_yellow(Expression::stringOf("yellow"));
	Expression expr_red(Expression::stringOf("red"));

	int branch0 = func.pushCond(expr_s == expr_red);
	func.conds[branch0].mem(s, expr_green);

	int branch1 = func.pushCond(expr_s == expr_green);
	func.conds[branch1].mem(s, expr_yellow);

	int branch2 = func.pushCond(expr_s == expr_yellow);
	func.conds[branch2].mem(s, expr_red);

	testFuncSynthesisFromCog(func);
}


TEST(CogToFlow, Buffer) {
	flow::Func func;
	func.name = "buffer";
	Operand L = func.pushNet("L", flow::Type(flow::Type::TypeName::FIXED, WIDTH), flow::Net::IN);
	Operand R = func.pushNet("R", flow::Type(flow::Type::TypeName::FIXED, WIDTH), flow::Net::OUT);
	Expression expr_L(L);

	int branch0 = func.pushCond(Expression::boolOf(true));
	func.conds[branch0].req(R, expr_L);
	func.conds[branch0].ack(L);

	testFuncSynthesisFromCog(func);
}


TEST(CogToFlow, Copy) {
	flow::Func func;
	func.name = "copy";
	Operand L = func.pushNet("L", flow::Type(flow::Type::TypeName::FIXED, WIDTH), flow::Net::IN);
	Operand R0 = func.pushNet("R0", flow::Type(flow::Type::TypeName::FIXED, WIDTH), flow::Net::OUT);
	Operand R1 = func.pushNet("R1", flow::Type(flow::Type::TypeName::FIXED, WIDTH), flow::Net::OUT);
	Expression expr_L(L);

	int branch0 = func.pushCond(Expression::boolOf(true));
	func.conds[branch0].req(R0, expr_L);
	func.conds[branch0].req(R1, expr_L);
	func.conds[branch0].ack(L);

	testFuncSynthesisFromCog(func);
}


TEST(CogToFlow, IsEven) {
	flow::Func func;
	func.name = "is_even";
	Operand L = func.pushNet("L", flow::Type(flow::Type::TypeName::FIXED, WIDTH), flow::Net::IN);
	Operand R = func.pushNet("R", flow::Type(flow::Type::TypeName::FIXED, 1), flow::Net::OUT);
	Expression expr_L(L);

	int branch0 = func.pushCond(Expression::boolOf(true));
	func.conds[branch0].req(R, expr_L % 2 == 0);
	func.conds[branch0].ack({L});

	testFuncSynthesisFromCog(func);
}


TEST(CogToFlow, Func) {
	flow::Func func;
	func.name = "func";
	Operand L0 = func.pushNet("L0", flow::Type(flow::Type::TypeName::FIXED, WIDTH), flow::Net::IN);
	Operand L1 = func.pushNet("L1", flow::Type(flow::Type::TypeName::FIXED, WIDTH), flow::Net::IN);
	Operand R = func.pushNet("R", flow::Type(flow::Type::TypeName::FIXED, WIDTH), flow::Net::OUT);
	Expression expr_L0(L0);
	Expression expr_L1(L1);

	int branch0 = func.pushCond(Expression::boolOf(true));
	func.conds[branch0].req(R, expr_L0 || expr_L1);
	//func.conds[branch0].mem(m_or, expr_L0 || expr_L1);
	func.conds[branch0].ack({L0, L1});

	testFuncSynthesisFromCog(func);
}


TEST(CogToFlow, Merge) {
	flow::Func func;
	func.name = "merge";
	Operand L0 = func.pushNet("L0", flow::Type(flow::Type::TypeName::FIXED, WIDTH), flow::Net::IN);
	Operand L1 = func.pushNet("L1", flow::Type(flow::Type::TypeName::FIXED, WIDTH), flow::Net::IN);
	Operand C = func.pushNet("C", flow::Type(flow::Type::TypeName::FIXED, 1), flow::Net::IN);
	Operand R = func.pushNet("R", flow::Type(flow::Type::TypeName::FIXED, WIDTH), flow::Net::OUT);
	Expression exprL0(L0);
	Expression exprL1(L1);
	Expression exprC(C);

	int branch0 = func.pushCond(exprC == Expression::intOf(0));
	func.conds[branch0].req(R, exprL0);
	func.conds[branch0].ack({C, L0});

	int branch1 = func.pushCond(exprC == Expression::intOf(1));
	func.conds[branch1].req(R, exprL1);
	func.conds[branch1].ack({C, L1});

	testFuncSynthesisFromCog(func);
}


TEST(CogToFlow, Split) {
	flow::Func func;
	func.name = "split";
	Operand L = func.pushNet("L", flow::Type(flow::Type::TypeName::FIXED, WIDTH), flow::Net::IN);
	Operand C = func.pushNet("C", flow::Type(flow::Type::TypeName::FIXED, 1), flow::Net::IN);
	Operand R0 = func.pushNet("R0", flow::Type(flow::Type::TypeName::FIXED, WIDTH), flow::Net::OUT);
	Operand R1 = func.pushNet("R1", flow::Type(flow::Type::TypeName::FIXED, WIDTH), flow::Net::OUT);
	Expression exprL(L);
	Expression exprC(C);

	int branch0 = func.pushCond(exprC == Expression::intOf(0));
	func.conds[branch0].req(R0, exprL);
	func.conds[branch0].ack({C, L});

	int branch1 = func.pushCond(exprC == Expression::intOf(1));
	func.conds[branch1].req(R1, exprL);
	func.conds[branch1].ack({C, L});

	testFuncSynthesisFromCog(func);
}

TEST(CogToFlow, DSAdder) {
	flow::Func func;
	func.name = "ds_adder";
	Operand Ad = func.pushNet("Ad", flow::Type(flow::Type::FIXED, WIDTH), flow::Net::IN);
	Operand Ac = func.pushNet("Ac", flow::Type(flow::Type::FIXED, 1), flow::Net::IN);
	Operand Bd = func.pushNet("Bd", flow::Type(flow::Type::FIXED, WIDTH), flow::Net::IN);
	Operand Bc = func.pushNet("Bc", flow::Type(flow::Type::FIXED, 1), flow::Net::IN);
	Operand Sd = func.pushNet("Sd", flow::Type(flow::Type::FIXED, WIDTH), flow::Net::OUT);
	Operand Sc = func.pushNet("Sc", flow::Type(flow::Type::FIXED, 1), flow::Net::OUT);
	Operand ci = func.pushNet("ci", flow::Type(flow::Type::BITS,  1), flow::Net::REG);
	Expression exprAc(Ac);
	Expression exprAd(Ad);
	Expression exprBc(Bc);
	Expression exprBd(Bd);
	Expression exprci(ci);

	Expression s((exprAd + exprBd + exprci) % pow(2, WIDTH));
	Expression co((exprAd + exprBd + exprci) / pow(2, WIDTH));

	int branch0 = func.pushCond(~exprAc & ~exprBc);
	func.conds[branch0].req(Sd, s);
	func.conds[branch0].req(Sc, Operand::intOf(0));
	func.conds[branch0].mem(ci, co);
	func.conds[branch0].ack({Ac, Ad, Bc, Bd});

	int branch1 = func.pushCond(exprAc & ~exprBc);
	func.conds[branch1].req(Sd, s);
	func.conds[branch1].req(Sc, Operand::intOf(0));
	func.conds[branch1].mem(ci, co);
	func.conds[branch1].ack({Bc, Bd});

	int branch2 = func.pushCond(~exprAc & exprBc);
	func.conds[branch2].req(Sd, s);
	func.conds[branch2].req(Sc, Operand::intOf(0));
	func.conds[branch2].mem(ci, co);
	func.conds[branch2].ack({Ac, Ad});

	int branch3 = func.pushCond(exprAc & exprBc & (co != exprci));
	func.conds[branch3].req(Sd, s);
	func.conds[branch3].req(Sc, Operand::intOf(0));
	func.conds[branch3].mem(ci, co);

	int branch4 = func.pushCond(exprAc & exprBc & (co == exprci));
	func.conds[branch4].req(Sd, s);
	func.conds[branch4].req(Sc, Operand::intOf(1));
	func.conds[branch4].mem(ci, Operand::intOf(0));
	func.conds[branch4].ack({Ac, Ad, Bc, Bd});

	testFuncSynthesisFromCog(func);
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
