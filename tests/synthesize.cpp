#include <algorithm>
#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include <chp/graph.h>
#include <chp/synthesize.h>
#include <common/standard.h>
#include <interpret_chp/export_dot.h>
#include <interpret_chp/import_chp.h>
#include <flow/func.h>
#include <flow/module.h>
#include <flow/synthesize.h>
#include <interpret_flow/export_verilog.h>
#include <parse/default/block_comment.h>
#include <parse/default/line_comment.h>
#include <parse/tokenizer.h>
#include <parse_chp/composition.h>
#include <parse_chp/factory.h>

#include "dot.h"

using namespace std;  //TODO: use only what you need
using std::filesystem::absolute;
using std::filesystem::current_path;
using namespace flow; //TODO: be explicit outside of flow submodule

const std::filesystem::path TEST_DIR = absolute(current_path() / "tests");
const int WIDTH = 8;


chp::graph importCHPFromString(const string &chp_string, bool debug=false) {
	tokenizer tokens;
	tokens.register_token<parse::block_comment>(false);
	tokens.register_token<parse::line_comment>(false);
	parse_chp::register_syntax(tokens);

	tokens.insert("string_input", chp_string, nullptr);
	chp::graph g;

	tokens.increment(false);
	tokens.expect<parse_chp::composition>();
	if (tokens.decrement(__FILE__, __LINE__)) {
		parse_chp::composition syntax(tokens);

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


void testFuncSynthesisFromCHP(flow::Func expected, bool render=true) {
	string filenameWithoutExtension = (TEST_DIR / expected.name).string();
	string chpFilename = filenameWithoutExtension + ".chp";
	string chpRaw = readStringFromFile(chpFilename, true);
	chp::graph g = importCHPFromString(chpRaw);
	g.post_process(true, false);
	g.name = expected.name;

	if (render) {
		string graphvizRaw = chp::export_graph(g, true).to_string();
		gvdot::render(filenameWithoutExtension, graphvizRaw);
	}

	flow::Func real = chp::synthesizeFuncFromCHP(g);
	EXPECT_EQ(real.name, expected.name);
	EXPECT_TRUE(areEquivalent(real, expected));

	/*
	cout << "CHP Graph.vars {" << endl;
	for (auto &var : g.vars) {
		cout << "* var(name: " << var.name << ", region: " << var.region << ")" << endl;

		bool exists = std::any_of(func.nets.begin(), func.nets.end(),
				[&var](const flow::Net &net) { return net.name == var.name; });
		EXPECT_EQ(exists, true);
	}
	cout << "}" << endl << endl;
	*/
}


TEST(ChpToFlow, Counter) {
	flow::Func func;
	func.name = "counter";
	Operand i = func.pushNet("i", Type(Type::FIXED, WIDTH), flow::Net::REG);
	Expression expri(i);
	//Expression increment_i(i + 1);

	int condition_idx = func.pushCond(expri < 8);
	func.conds[condition_idx].mem(i, expri + 1);

	testFuncSynthesisFromCHP(func);
}


TEST(ChpToFlow, Countdown) {
	flow::Func func;
	func.name = "countdown";
	Operand log = func.pushNet("log", Type(Type::FIXED, WIDTH), flow::Net::OUT);
	Operand n = func.pushNet("n", Type(Type::FIXED, WIDTH), flow::Net::REG);
	Expression exprn(n);
	//Expression decrement_n(n - 1);

	int condition_idx = func.pushCond(exprn > 0);
	flow::Condition cond = func.conds[condition_idx];
	cond.req(log, exprn);
	cond.mem(n, n - 1);

	//TODO: one-off trailing send: log!1337

	testFuncSynthesisFromCHP(func);
}


TEST(ChpToFlow, Buffer) {
	flow::Func func;
	func.name = "buffer";
	Operand L = func.pushNet("L", Type(Type::FIXED, WIDTH), flow::Net::IN);
	Operand R = func.pushNet("R", Type(Type::FIXED, WIDTH), flow::Net::OUT);
	Expression exprL(L);

	int branch0 = func.pushCond(Expression::boolOf(true));
	func.conds[branch0].req(R, exprL);
	func.conds[branch0].ack(L);

	testFuncSynthesisFromCHP(func);
}


TEST(ChpToFlow, Merge) {
	flow::Func func;
	func.name = "merge";
	Operand L0 = func.pushNet("L0", Type(Type::TypeName::FIXED, WIDTH), flow::Net::IN);
	Operand L1 = func.pushNet("L1", Type(Type::TypeName::FIXED, WIDTH), flow::Net::IN);
	Operand C = func.pushNet("C", Type(Type::TypeName::FIXED, 1), flow::Net::IN);
	Operand R = func.pushNet("R", Type(Type::TypeName::FIXED, WIDTH), flow::Net::OUT);
	Expression exprL0(L0);
	Expression exprL1(L1);
	Expression exprC(C);

	int branch0 = func.pushCond(exprC == Expression::intOf(0));
	func.conds[branch0].req(R, exprL0);
	func.conds[branch0].ack({C, L0});

	int branch1 = func.pushCond(exprC == Expression::intOf(1));
	func.conds[branch1].req(R, exprL1);
	func.conds[branch1].ack({C, L1});

	testFuncSynthesisFromCHP(func);
}


TEST(ChpToFlow, Split) {
	flow::Func func;
	func.name = "split";
	Operand L = func.pushNet("L", Type(Type::TypeName::FIXED, WIDTH), flow::Net::IN);
	Operand C = func.pushNet("C", Type(Type::TypeName::FIXED, 1), flow::Net::IN);
	Operand R0 = func.pushNet("R0", Type(Type::TypeName::FIXED, WIDTH), flow::Net::OUT);
	Operand R1 = func.pushNet("R1", Type(Type::TypeName::FIXED, WIDTH), flow::Net::OUT);
	Expression exprL(L);
	Expression exprC(C);

	int branch0 = func.pushCond(exprC == Expression::intOf(0));
	func.conds[branch0].req(R0, exprL);
	func.conds[branch0].ack({C, L});

	int branch1 = func.pushCond(exprC == Expression::intOf(1));
	func.conds[branch1].req(R1, exprL);
	func.conds[branch1].ack({C, L});

	testFuncSynthesisFromCHP(func);
}


TEST(ChpToFlow, IsEven) {
	flow::Func func;
	func.name = "is_even";
	testFuncSynthesisFromCHP(func);
}


TEST(ChpToFlow, Primes) {
	flow::Func func;
	func.name = "primes";
	testFuncSynthesisFromCHP(func);
}


TEST(ChpToFlow, TrafficLight) {
	flow::Func func;
	func.name = "traffic_light";
	testFuncSynthesisFromCHP(func);
}


TEST(ChpToFlow, DSAdderFlat) {
	flow::Func func;
	func.name = "ds_adder_flat";
	Operand Ad = func.pushNet("Ad", Type(Type::FIXED, WIDTH), flow::Net::IN);
	Operand Ac = func.pushNet("Ac", Type(Type::FIXED, 1), flow::Net::IN);
	Operand Bd = func.pushNet("Bd", Type(Type::FIXED, WIDTH), flow::Net::IN);
	Operand Bc = func.pushNet("Bc", Type(Type::FIXED, 1), flow::Net::IN);
	Operand Sd = func.pushNet("Sd", Type(Type::FIXED, WIDTH), flow::Net::OUT);
	Operand Sc = func.pushNet("Sc", Type(Type::FIXED, 1), flow::Net::OUT);
	Operand ci = func.pushNet("ci", Type(Type::FIXED, 1), flow::Net::REG);
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

	testFuncSynthesisFromCHP(func);
}

TEST(ChpToFlow, DSAdder) {
	Func func;
	func.name = "ds_adder";

	Operand Ad = func.pushNet("Ad", Type(Type::FIXED, WIDTH), flow::Net::IN);
	Operand Ac = func.pushNet("Ac", Type(Type::FIXED, 1), flow::Net::IN);
	Operand Bd = func.pushNet("Bd", Type(Type::FIXED, WIDTH), flow::Net::IN);
	Operand Bc = func.pushNet("Bc", Type(Type::FIXED, 1), flow::Net::IN);
	Operand Sd = func.pushNet("Sd", Type(Type::FIXED, WIDTH), flow::Net::OUT);
	Operand Sc = func.pushNet("Sc", Type(Type::FIXED, 1), flow::Net::OUT);
	Operand ci = func.pushNet("ci", Type(Type::FIXED, 1), flow::Net::REG);
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

	testFuncSynthesisFromCHP(func);
}
