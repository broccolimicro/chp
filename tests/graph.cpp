#include <filesystem>

#include <gtest/gtest.h>

#include <chp/graph.h>
//#include <common/standard.h>
#include <interpret_chp/export_dot.h>
#include <interpret_chp/import_chp.h>
#include <parse/default/block_comment.h>
#include <parse/default/line_comment.h>
#include <parse/tokenizer.h>
#include <parse_chp/composition.h>
#include <parse_chp/factory.h>

#include "dot.h"

using std::filesystem::absolute;
using std::filesystem::current_path;

const std::filesystem::path TEST_DIR = absolute(current_path() / "tests");

chp::graph _importCHPFromString(const string &chp_string, bool debug=false) {
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

	//TODO: document this deviation from tests/synthesize.cpp copy
	//if (!debug) {
	g.post_process(true, false);
	//}
	return g;
}


bool testBranchFlatten(const string &source, const string &target, bool render=true) {
	chp::graph targetGraph = _importCHPFromString(target);
	chp::graph sourceGraph = _importCHPFromString(source);
	sourceGraph.flatten();
	//EXPECT_EQ(sourceGraph, targetGraph);

	if (render) {
    const ::testing::TestInfo* const test_info =
        ::testing::UnitTest::GetInstance()->current_test_info();

    std::string test_suite_name = test_info->test_suite_name();
    std::string test_name = test_info->name();


		string filenameWithoutExtension = (TEST_DIR / ("_in_" + test_name)).string();
		string graphvizRaw = chp::export_graph(sourceGraph, true).to_string();
		gvdot::render(filenameWithoutExtension, graphvizRaw);

		filenameWithoutExtension = (TEST_DIR / ("_out_" + test_name)).string();
		graphvizRaw = chp::export_graph(targetGraph, true).to_string();
		gvdot::render(filenameWithoutExtension, graphvizRaw);
	}

	return false;
}


TEST(BranchFlatten, Decoder2) {
	std::string source = R"(
*[
  [   b1 ->
    [   b0 -> out3!v
    [] ~b0 -> out2!v
    ]
  [] ~b1 ->
    [   b0 -> out1!v
    [] ~b0 -> out0!v
    ]
  ]
]
		)";

	std::string target = R"(
*[
  [  ( b1 &  b0) -> out3!v
  [] ( b1 & ~b0) -> out2!v
  [] (~b1 &  b0) -> out1!v
  [] (~b1 & ~b0) -> out0!v
  ]
]
		)";

	EXPECT_TRUE(testBranchFlatten(source, target));
}


TEST(BranchFlatten, Decoder3) {
	std::string source = R"(
*[
	[   b2 ->
		[   b1 ->
			[   b0 -> out7!v
			[] ~b0 -> out6!v
			]
		[] ~b1 ->
			[   b0 -> out5!v
			[] ~b0 -> out4!v
			]
		]
	[] ~b2 ->
		[  b1 ->
			[   b0 -> out3!v
			[] ~b0 -> out2!v
			]
		[] ~b1 ->
			[   b0 -> out1!v
			[] ~b0 -> out0!v
			]
		]
	]
]
		)";

	std::string target = R"(
*[
  [  ( b2 &  b1 &  b0) -> out7!v
  [] ( b2 &  b1 & ~b0) -> out6!v
  [] ( b2 & ~b1 &  b0) -> out5!v
  [] ( b2 & ~b1 & ~b0) -> out4!v
  [] (~b2 &  b1 &  b0) -> out3!v
  [] (~b2 &  b1 & ~b0) -> out2!v
  [] (~b2 & ~b1 &  b0) -> out1!v
  [] (~b2 & ~b1 & ~b0) -> out0!v
  ]
]
		)";

	EXPECT_TRUE(testBranchFlatten(source, target));
}


TEST(BranchFlatten, Decoder4) {
	std::string source = R"(
*[
	[   b3 ->
		[   b2 ->
			[   b1 ->
				[   b0 -> out15!v
				[] ~b0 -> out14!v
				]
			[] ~b1 ->
				[   b0 -> out13!v
				[] ~b0 -> out12!v
				]
			]
		[] ~b2 ->
			[   b1 ->
				[   b0 -> out11!v
				[] ~b0 -> out10!v
				]
			[] ~b1 ->
				[   b0 -> out9!v
				[] ~b0 -> out8!v
				]
			]
		]
	[] ~b3 ->
		[   b2 ->
			[   b1 ->
				[   b0 -> out7!v
				[] ~b0 -> out6!v
				]
			[] ~b1 ->
				[   b0 -> out5!v
				[] ~b0 -> out4!v
				]
			]
		[] ~b2 ->
			[   b1 ->
				[   b0 -> out3!v
				[] ~b0 -> out2!v
				]
			[] ~b1 ->
				[   b0 -> out1!v
				[] ~b0 -> out0!v
				]
			]
		]
	]
]
		)";

	std::string target = R"(
*[
  [  ( b3 &  b2 &  b1 &  b0) -> out15!v
  [] ( b3 &  b2 &  b1 & ~b0) -> out14!v
  [] ( b3 &  b2 & ~b1 &  b0) -> out13!v
  [] ( b3 &  b2 & ~b1 & ~b0) -> out12!v
  [] ( b3 & ~b2 &  b1 &  b0) -> out11!v
  [] ( b3 & ~b2 &  b1 & ~b0) -> out10!v
  [] ( b3 & ~b2 & ~b1 &  b0) -> out9!v
  [] ( b3 & ~b2 & ~b1 & ~b0) -> out8!v
  [] (~b3 &  b2 &  b1 &  b0) -> out7!v
  [] (~b3 &  b2 &  b1 & ~b0) -> out6!v
  [] (~b3 &  b2 & ~b1 &  b0) -> out5!v
  [] (~b3 &  b2 & ~b1 & ~b0) -> out4!v
  [] (~b3 & ~b2 &  b1 &  b0) -> out3!v
  [] (~b3 & ~b2 &  b1 & ~b0) -> out2!v
  [] (~b3 & ~b2 & ~b1 &  b0) -> out1!v
  [] (~b3 & ~b2 & ~b1 & ~b0) -> out0!v
  ]
]
		)";

	EXPECT_TRUE(testBranchFlatten(source, target));
}


TEST(BranchFlatten, Counter) {
	std::string source = R"(
*[lc=Lc?;
	[ lc==inc -> 
		[ v==0 -> v=1
		[] v==1 -> v=2
		[] v==2 -> v=3
		[] v==3 -> v=0; Rc!inc; vz=Rz?
		]
	[] lc==dec -> 
		[ v==0 -> v=3; Rc!dec; vz=Rz?
		[] v==1 -> v=0
		[] v==2 -> v=1
		[] v==3 -> v=2
		]
	]; Lz!(v==0 ^ vz==1)
]
		)";

	std::string target = R"(
*[lc=Lc?;
	[  lc==inc ^ v==0 -> v=1
	[] lc==inc ^ v==1 -> v=2
	[] lc==inc ^ v==2 -> v=3
	[] lc==inc ^ v==3 -> v=0; Rc!inc; vz=Rz?
	[] lc==dec ^ v==0 -> v=3; Rc!dec; vz=Rz?
	[] lc==dec ^ v==1 -> v=0
	[] lc==dec ^ v==2 -> v=1
	[] lc==dec ^ v==3 -> v=2
	]; Lz!(v==0 ^ vz==1)
]
		)";

	EXPECT_TRUE(testBranchFlatten(source, target));
}


TEST(BranchFlatten, DSAdder) {
	std::string source = R"(
*[ s = (Ad + Bd + ci) % pow(2, N);
	co = (Ad + Bd + ci) / pow(2, N);
	[ !Ac | !Bc -> Sc!0,Sd!s; ci=co;
		[ !Ac -> A? [] else -> skip ],
		[ !Bc -> B? [] else -> skip ]
	[] Ac & Bc & co~=ci -> Sc!0,Sd!s; ci=co
	[] Ac & Bc & co==ci -> Sc!1,Sd!s; ci=0; A?, B?
	]
]
		)";

	std::string target = R"(
*[ s = (Ad + Bd + ci) % pow(2, N);
	co = (Ad + Bd + ci) / pow(2, N);
	[ !Ac & !Bc -> Sc!0,Sd!s; ci=co; Ac?, Ad?, Bc?, Bd?
  [] Ac & !Bc -> Sc!0,Sd!s; ci=co; Bc?, Bd?
  [] !Ac & Bc -> Sc!0,Sd!s; ci=co; Ac?, Ad?
	[] Ac & Bc & co~=ci -> Sc!0,Sd!s; ci=co
	[] Ac & Bc & co==ci -> Sc!1,Sd!s; ci=0; Ac?, Ad?, Bc?, Bd?
	]
]
		)";

	EXPECT_TRUE(testBranchFlatten(source, target));
}


/*
TEST(BranchFlatten, Arbiter) {
	std::string source = R"(
*[
  [ req0? & req1? ->
    [ prio? -> ack1!x; gnt1!x
    [] ~prio? -> ack0!x; gnt0!x
    ]
  [] req0? & ~req1? -> ack0!x; gnt0!x
  [] ~req0? & req1? -> ack1!x; gnt1!x
  [] ~req0? & ~req1? -> skip
  ]
]
		)";

	std::string target = R"(
*[
  [ req0? & req1? & prio? -> ack1!x; gnt1!x
  [] req0? & req1? & ~prio? -> ack0!x; gnt0!x
  [] req0? & ~req1? -> ack0!x; gnt0!x
  [] ~req0? & req1? -> ack1!x; gnt1!x
  [] ~req0? & ~req1? -> skip
  ]
]
		)";

	EXPECT_TRUE(testBranchFlatten(source, target));
}


TEST(BranchFlatten, Router) {
	std::string source = R"(
*[
  packet_in?(header, payload);
  [ header.dest == 0 ->
    [ port0_ready? -> port0_out!(payload)
    [] ~port0_ready? -> drop!(payload)
    ]
  [] header.dest == 1 ->
    [ port1_ready? -> port1_out!(payload)
    [] ~port1_ready? -> drop!(payload)
    ]
  [] otherwise -> error_handler!()
  ]
]
		)";

	std::string target = R"(
*[
  packet_in?(header, payload);
  [ header.dest == 0 & port0_ready? -> port0_out!(payload)
  [] header.dest == 0 & ~port0_ready? -> drop!(payload)
  [] header.dest == 1 & port1_ready? -> port1_out!(payload)
  [] header.dest == 1 & ~port1_ready? -> drop!(payload)
  [] otherwise -> error_handler!()
  ]
]
		)";

	EXPECT_TRUE(testBranchFlatten(source, target));
}


TEST(BranchFlatten, Stall) {
	std::string source = R"(
*[
  [ data_hazard? ->
    stall!x
  [] ~data_hazard? ->
    [ resource_conflict? ->
      stall!x
    [] ~resource_conflict? ->
      proceed!x
    ]
  ]
]
		)";

	std::string target = R"(
*[
  [ data_hazard? or resource_conflict? ->
    stall!x
  [] ~data_hazard? and ~resource_conflict? ->
    proceed!x
  ]
]
		)";

	EXPECT_TRUE(testBranchFlatten(source, target));
}


TEST(BranchFlatten, MemoryAccess) {
	std::string source = R"(
*[
  [ read_req?addr ->
    [ is_valid(addr) ->
      mem_read!addr; data_out!data
    [] ~is_valid(addr) ->
      error_flag!x
    ]
  [] write_req?(addr, data) ->
    [ is_valid(addr) ->
      mem_write!(addr, data)
    [] ~is_valid(addr) ->
      error_flag!x
    ]
  ]
]
		)";

	std::string target = R"(
*[
  [ read_req?(addr) & is_valid(addr) -> mem_read!(addr); data_out!(data)
  [] read_req?(addr) & ~is_valid(addr) -> error_flag!
  [] write_req?(addr, data) & is_valid(addr) -> mem_write!(addr, data)
  [] write_req?(addr, data) & ~is_valid(addr) -> error_flag!
  ]
]
		)";

	EXPECT_TRUE(testBranchFlatten(source, target));
}
*/
