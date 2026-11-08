#include "helpers.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <algorithm>

#include <petri/tree.h>
#include <petri/synthesize.h>
#include <chp/synthesize.h>

#include <parse/default/block_comment.h>
#include <parse/default/line_comment.h>
#include <parse/tokenizer.h>
#include <parse_cog/composition.h>
#include <parse_cog/factory.h>
#include <parse_chp/composition.h>
#include <parse_chp/factory.h>

#include <interpret_chp/export_cog.h>
#include <interpret_chp/export_dot.h>
#include <interpret_chp/import_cog.h>
#include <interpret_chp/import_chp.h>

#include "dot.h"

namespace fs = std::filesystem;

const fs::path SRC_DIR = fs::absolute(fs::current_path() / "tests" / "src");
const fs::path BUILD_DIR = fs::absolute(fs::current_path() / "tests" / "build");

std::string readStringFromFile(std::string path, bool stripNewlines) {
	ifstream in(path, ios::in | ios::binary);
	EXPECT_TRUE((bool)in) << "Failed to open file: " << path;
	if (not in) {
		return "";
	}

	std::ostringstream contents;
	contents << in.rdbuf();
	std::string result = contents.str();

	// Replace all newlines with spaces
	if (stripNewlines) {
		 std::replace(result.begin(), result.end(), '\n', ' ');
		 std::replace(result.begin(), result.end(), '\r', ' '); // for Windows files
	}

	return result;
}

chp::graph importCHPFromString(std::string name, std::string source, bool debug) {
	tokenizer tokens;
	tokens.register_token<parse::block_comment>(false);
	tokens.register_token<parse::line_comment>(false);
	parse_chp::factory.register_syntax(tokens);

	tokens.insert("string_input", source, nullptr);
	chp::graph g;

	tokens.increment(false);
	tokens.expect<parse_chp::composition>();
	if (tokens.decrement(__FILE__, __LINE__)) {
		parse_chp::composition syntax(tokens);

		// Blame parser or interpreter?
		if (debug) { cout << syntax.to_string() << endl; }
		chp::import_chp(g, syntax, &tokens, true);
	}
	g.name = name;
	if (debug) {
		fs::create_directories(BUILD_DIR);
		string chpGraphvizRaw = chp::export_graph(g, true).to_string();
		gvdot::render(BUILD_DIR / (name + ".png"), chpGraphvizRaw);
	}

	g.post_process(true, false);
	if (debug) {
		std::string chpGraphvizRaw = chp::export_graph(g, true).to_string();
		gvdot::render(BUILD_DIR / (name + "_post.png"), chpGraphvizRaw);
	}
	return g;
}

chp::graph importCHPFromCogString(std::string name, std::string source, bool debug) {
	tokenizer tokens;
	tokens.register_token<parse::block_comment>(false);
	tokens.register_token<parse::line_comment>(false);
	parse_cog::factory.register_syntax(tokens);

	tokens.insert("string_input", source, nullptr);
	chp::graph g;

	tokens.increment(false);
	tokens.expect<parse_cog::composition>();
	if (tokens.decrement(__FILE__, __LINE__)) {
		parse_cog::composition syntax(tokens);
		
		// Blame parser or interpreter?
		if (debug) { cout << syntax.to_string() << endl; }
		chp::import_chp(g, syntax, &tokens, true);
	}
	g.name = name;
	if (debug) {
		fs::create_directories(BUILD_DIR);
		string chpGraphvizRaw = chp::export_graph(g, true).to_string();
		gvdot::render(BUILD_DIR / (name + ".png"), chpGraphvizRaw);
	}

	g.post_process(true, false);
	if (debug) {
		std::string chpGraphvizRaw = chp::export_graph(g, true).to_string();
		gvdot::render(BUILD_DIR / (name + "_post.png"), chpGraphvizRaw);
	}
	return g;
}

void expectProperNesting(const chp::graph &g, bool debug) {
	petri::graph<chp::place, controlflow::Tree<chp::transition, chp::place>, chp::state> t;
	EXPECT_TRUE(graph_to_tree(t, g));
	if (debug) {
		t.print();
		for (size_t i = 0; i < t.transitions.size(); i++) {
			if (not t.transitions.is_valid(i)) continue;

			printf("T%d: ", (int)i);
			t.transitions[i].print();
			printf("\n");
		}
	}
}

void expectEq(const flow::Func &real, const flow::Func &expected) {
	EXPECT_EQ(real.name, expected.name);

	EXPECT_EQ(real.nets.size(), expected.nets.size());
	auto realNets = std::multiset<flow::Net>(real.nets.begin(), real.nets.end());
	auto expectedNets = std::multiset<flow::Net>(expected.nets.begin(), expected.nets.end());
	EXPECT_EQ(realNets, expectedNets); 

	std::string realStr = parse_cog::export_func(real).to_string("");
	std::string expectedStr = parse_cog::export_func(expected).to_string("");

	auto diff = lineDiff(realStr, expectedStr);
	EXPECT_TRUE(isMatch(diff)) << diff;
}

void expectEq(std::string expected, std::string actual) {
	auto diff = lineDiff(actual, expected);
	EXPECT_TRUE(isMatch(diff)) << diff;
}

