#include <gtest/gtest.h>

#include "support.h"
#include "dot.h"

#include <petri/tree.h>
#include <petri/synthesize.h>
#include <chp/synthesize.h>
#include <interpret_chp/export_dot.h>
#include <interpret_flow/export_dot.h>

using std::filesystem::absolute;
using std::filesystem::current_path;

void testBranchFlatten(const string &source, const string &target, bool debug=false) {
	chp::graph targetGraph = importCHPFromCogString("", target);
	chp::graph sourceGraph = importCHPFromCogString("", source);

	const ::testing::TestInfo* const test_info =
			::testing::UnitTest::GetInstance()->current_test_info();
	std::string test_suite_name = test_info->test_suite_name();
	std::string test_name = test_info->name();

	if (debug) {
		string filenameWithoutExtension = (BUILD_DIR / ("_in_" + test_name)).string();
		string graphvizRaw = chp::export_graph(sourceGraph, true).to_string();
		gvdot::render(filenameWithoutExtension, graphvizRaw);

		filenameWithoutExtension = (BUILD_DIR / ("_out_" + test_name)).string();
		graphvizRaw = chp::export_graph(targetGraph, true).to_string();
		gvdot::render(filenameWithoutExtension, graphvizRaw);
	}

	sourceGraph.flatten(debug);
	if (debug) {
		string filenameWithoutExtension = (BUILD_DIR / ("_mid_" + test_name)).string();
		string graphvizRaw = chp::export_graph(sourceGraph, true).to_string();
		gvdot::render(filenameWithoutExtension, graphvizRaw);
	}

	//EXPECT_EQ(sourceGraph, targetGraph); //TODO: needs comparison operator
	//return false;
	EXPECT_TRUE(sourceGraph.isFlat()) << "resulting graph not flat";
	EXPECT_TRUE(is_clean());

	petri::graph<chp::place, controlflow::Tree<chp::transition>, chp::state> t;
	EXPECT_TRUE(graph_to_tree(t, sourceGraph));
	t.print();
	for (size_t i = 0; i < t.transitions.size(); i++) {
		if (not t.transitions.is_valid(i)) continue;

		printf("T%d\t", i);
		t.transitions[i].print();
		printf("\n");
	}
}

TEST(BranchFlatten, Merge) {
	std::string source = R"""(while {
	c = C.recv()
	if c == 0 {
		x = A.recv()
	} or if c == 1 {
		x = B.recv()
	}
	R.send(x)
})""";

	//*[c=C?;
	//	[ c==0 -> x=A?
	//	[] c==1 -> x=B?
	//	]; R!x
	// ]

	//TODO: rewind initial marking to match "c=C?" start-up sequence
	std::string target = R"""(while {
	if c == 0 {
		x = A.recv()
		R.send(x)
		C.recv()
	} or if c == 1 {
		x = A.recv()
		R.send(x)
		C.recv()
	}
})""";

	// *[[ c==0 -> x=A?; R!x; c=C?
	// 	[] c==1 -> x=B?; R!x; c=C?
	// ]]

	testBranchFlatten(source, target);
}


TEST(BranchFlatten, Split) {
	std::string source = R"""(while {
	c = C.recv()
	x = L.recv()
	if c == 0 {
		A.send(x)
	} or if c == 1 {
		B.send(x)
	}
})""";

	// *[c=C?; x=L?;
	// 	[ c==0 -> A!x
	// 	[] c==1 -> B!x
	// 	]
	// ]

	//TODO: rewind initial marking to match "c=C?" start-up sequence
	std::string target = R"""(while {
	if C.probe() == 0 & L.probe() {
		C.recv()
		x = L.recv()
		A.send(x)
	} or if C.probe() == 1 & L.probe() {
		C.recv()
		x = L.recv()
		B.send(x)
	}
})""";

	// *[[ c==0 -> A!x; c=C?; x=L?
	// 	[] c==1 -> B!x; c=C?; x=L?
	// ]]

	testBranchFlatten(source, target);
}


TEST(BranchFlatten, Decoder2) {
	std::string source = R"""(while {
	await b1 {
		await b0 {
			out3.send(v)
		} or await ~b0 {
			out2.send(v)
		}
	} or await ~b1 {
		await b0 {
			out1.send(v)
		} or await ~b0 {
			out0.send(v)
		}
	}
})""";

	//*[
	//  [   b1 ->
	//    [   b0 -> out3!v
	//    [] ~b0 -> out2!v
	//    ]
	//  [] ~b1 ->
	//    [   b0 -> out1!v
	//    [] ~b0 -> out0!v
	//    ]
	//  ]
	//]

	std::string target = R"""(while {
	await b1 & b0 {
		out3.send(v)
	} or await b1 & ~b0 {
		out2.send(v)
	} or await ~b1 & b0 {
		out1.send(v)
	} or await ~b1 & ~b0 {
		out0.send(v)
	}
})""";

	//*[
	//  [  ( b1 &  b0) -> out3!v
	//  [] ( b1 & ~b0) -> out2!v
	//  [] (~b1 &  b0) -> out1!v
	//  [] (~b1 & ~b0) -> out0!v
	//  ]
	//]

	testBranchFlatten(source, target);
}

TEST(BranchFlatten, Counter) {
	std::string source = R"""(while {
		lc = Lc.recv()
		if lc == 1 { // increment
			if v == 0 {
				v = 1
			} or if v == 1 {
				v = 2
			} or if v == 2 {
				v = 3
			} or if v == 3 {
				v = 0
				Rc.send(1) // increment
				vz = Rz.recv()
			}
		} or if lc == 0 { // decrement
			if v == 0 {
				v = 3
				Rc.send(0) // decrement
				vz = Rz.recv()
			} or if v == 1 {
				v = 0
			} or if v == 2 {
				v = 1
			} or if v == 3 {
				v = 2
			}
		}
		Lz.send((v == 0) ^ (vz == 1))
	})""";

	//*[lc=Lc?;
	//	[ lc==inc -> 
	//		[ v==0 -> v=1
	//		[] v==1 -> v=2
	//		[] v==2 -> v=3
	//		[] v==3 -> v=0; Rc!inc; vz=Rz?
	//		]
	//	[] lc==dec -> 
	//		[ v==0 -> v=3; Rc!dec; vz=Rz?
	//		[] v==1 -> v=0
	//		[] v==2 -> v=1
	//		[] v==3 -> v=2
	//		]
	//	]; Lz!(v==0 ^ vz==1)
	//]

	std::string target = R"""(while {
		lc = Lc.recv()
		if lc == 1 & v == 0 {
			v = 1
		} or if lc == 1 & v == 1 {
			v = 2
		} or if lc == 1 & v == 2 {
			v = 3
		} or if lc == 1 & v == 3 {
			v = 0
			Rc.send(1)
			vz = Rz.recv()
		} or if lc == 0 & v == 0 {
			v = 3
			Rc.send(0)
			vz = Rz.recv()
		} or if lc == 0 & v == 1 {
			v = 0
		} or if lc == 0 & v == 2 {
			v = 1
		} or if lc == 0 & v == 3 {
			v = 2
		}
		Lz.send((v == 0) ^ (vz == 1))
	})""";

	//*[lc=Lc?;
	//	[  lc==inc && v==0 -> v=1
	//	[] lc==inc && v==1 -> v=2
	//	[] lc==inc && v==2 -> v=3
	//	[] lc==inc && v==3 -> v=0; Rc!inc; vz=Rz?
	//	[] lc==dec && v==0 -> v=3; Rc!dec; vz=Rz?
	//	[] lc==dec && v==1 -> v=0
	//	[] lc==dec && v==2 -> v=1
	//	[] lc==dec && v==3 -> v=2
	//	]; Lz!(v==0 && vz==1)
	//]

	testBranchFlatten(source, target, true);
}

TEST(BranchFlatten, DSAdder) {
	std::string source = R"""(while {
		s = ((A.d) + (B.d) + ci) % pow(2, N)
		co = ((A.d) + (B.d) + ci) / pow(2, N)
		if ~A.c | ~B.c {
			Sc.send(0)
			Sd.send(s)
			ci = co
			if ~A.c {
				A.recv()
			} and if ~B.c {
				B.recv()
			}
		} or if A.c & B.c & co != ci {
			Sc.send(0)
			Sd.send(s)
			ci = co
		} or if A.c & B.c & co == ci {
			Sc.send(1)
			Sd.send(s)
			ci = 0
			A.recv()
			B.recv()
		}
	})""";

	//*[ s = ((#A).d + (#B).d + ci) % pow(2, N);
	//	co = ((#A).d + (#B).d + ci) / pow(2, N);
	//	[ !(#A).c | !(#B).c -> Sc!0, Sd!s; ci=co;
	//		[ !(#A).c -> A? [] else -> skip ],
	//		[ !(#B).c -> B? [] else -> skip ]
	//	[] (#A).c & (#B).c & co!=ci -> Sc!0, Sd!s; ci=co
	//	[] (#A).c & (#B).c & co==ci -> Sc!1, Sd!s; ci=0; A?, B?
	//	]
	//]

	std::string target = R"""(while {
		s = (Ad + Bd + ci) % pow(2, N)
		co = (Ad + Bd + ci) / pow(2, N)
		if ~Ac & ~Bc {
			Sc.send(0)
			Sd.send(s)
			ci = co
			Ac.recv()
			Ad.recv()
			Bc.recv()
			Bd.recv()
		} or if Ac & ~Bc {
			Sc.send(0)
			Sd.send(s)
			Bc.recv()
			Bd.recv()
		} or if ~Ac & Bc {
			Sc.send(0)
			Sd.send(s)
			ci = co
			Ac.recv()
			Ad.recv()
		} or if Ac & Bc & co != ci {
			Sc.send(0)
			Sd.send(s)
			ci = co
		} or if Ac & Bc & co == ci {
			Sc.send(1)
			Sd.send(s)
			ci = 0
			Ac.recv()
			Ad.recv()
			Bc.recv()
			Bd.recv()
		}
	})""";

	//TODO: rewind initial marking to match "c=C?" start-up sequence
	//*[[ !Ac & !Bc -> Sc!0,Sd!s; ci=co; Ac?, Ad?, Bc?, Bd?; s = (Ad + Bd + ci) % pow(2, N); co = (Ad + Bd + ci) / pow(2, N)
	//  [] Ac & !Bc -> Sc!0,Sd!s; ci=ci; Bc?, Bd?; s = (Ad + Bd + ci) % pow(2, N); co = (Ad + Bd + ci) / pow(2, N)
	//  [] !Ac & Bc -> Sc!0,Sd!s; ci=co; Ac?, Ad?; s = (Ad + Bd + ci) % pow(2, N); co = (Ad + Bd + ci) / pow(2, N)
	//	[] Ac & Bc & co!=ci -> Sc!0,Sd!s; ci=co; s = (Ad + Bd + ci) % pow(2, N); co = (Ad + Bd + ci) / pow(2, N)
	//	[] Ac & Bc & co==ci -> Sc!1,Sd!s; ci=0; Ac?, Ad?, Bc?, Bd?; s = (Ad + Bd + ci) % pow(2, N); co = (Ad + Bd + ci) / pow(2, N)
	//]]

	testBranchFlatten(source, target, true);
}

TEST(BranchFlatten, Collatz) {
	std::string source = R"""(while {
	n = N.recv()

	while n != 1 {
		log.send(n)
		if n % 2 == 0 {
			n = n / 2
		} or {
			n = 3 * n + 1
		}
	}

	log.send(n)
})""";

	std::string target = R"""(n = 1
while {
	if n == 1 {
		n = N.recv()
		log.send(n)
	} or if n != 1 && n % 2 == 0 {
		n = n / 2
		log.send(n)
	} or if n != 1 && n % 2 == 1 {
		n = 3 * n + 1
		log.send(n)
	}
})""";

	testBranchFlatten(source, target, true);
}

TEST(BranchFlatten, IsEven) {
	std::string source = R"""(
		i = 10
		while {
			if i < 10 {
				if i % 2 == 0 {
					log.send(i)
				} or if i % 2 != 0 {
					skip
				}
				i = i + 1
			} or if i >= 10 {
				i = I.recv()
			}
		}
	)""";

	std::string target = R"""(
		i = 10
		while {
			if i < 10 & i % 2 == 0 {
				log.send(i)
				i = i + 1
			} or if i < 10 & i % 2 != 0 {
				i = i + 1
			} or if i >= 10 {
				i = I.recv()
			}
		}
	)""";

	testBranchFlatten(source, target, true);
}

TEST(BranchFlatten, IsPrime) {
	std::string source = R"""(while {
	n = N.recv()
	isPrime = true
	i = 2
	while i < n {
		if n % i == 0 {
			isPrime = false
		} or if n % i != 0 {
			skip
		}
		i = i + 1
	}
	if isPrime {
		out.send(n)
	} or if !isPrime {
		skip
	}
})""";

	std::string target = R"""(
n = 0
i = 2
isPrime = false
while {
	if i < n && n % i == 0 {
		isPrime = false
		i = i + 1
	} or if i < n && n % i != 0 {
		i = i + 1
	} or if i >= n && !isPrime {
		isPrime = true
		n = N.recv()
	} or if i >= n && isPrime {
		out.send(n)
		n = N.recv()
	}
})""";

	testBranchFlatten(source, target, true);
}

//TEST(BranchFlatten, Router) {
//	std::string source = R"(
//*[
//  packet_in?(header, payload);
//  [ header.dest == 0 ->
//    [ port0_ready? -> port0_out!(payload)
//    [] ~port0_ready? -> drop!(payload)
//    ]
//  [] header.dest == 1 ->
//    [ port1_ready? -> port1_out!(payload)
//    [] ~port1_ready? -> drop!(payload)
//    ]
//  [] otherwise -> error_handler!()
//  ]
//]
//		)";
//
//	std::string target = R"(
//*[
//  packet_in?(header, payload);
//  [ header.dest == 0 & port0_ready? -> port0_out!(payload)
//  [] header.dest == 0 & ~port0_ready? -> drop!(payload)
//  [] header.dest == 1 & port1_ready? -> port1_out!(payload)
//  [] header.dest == 1 & ~port1_ready? -> drop!(payload)
//  [] otherwise -> error_handler!()
//  ]
//]
//		)";
//
//	testBranchFlatten(source, target);
//}
//
//
//TEST(BranchFlatten, Stall) {
//	std::string source = R"(
//*[
//  [ data_hazard? ->
//    stall!x
//  [] ~data_hazard? ->
//    [ resource_conflict? ->
//      stall!x
//    [] ~resource_conflict? ->
//      proceed!x
//    ]
//  ]
//]
//		)";
//
//	std::string target = R"(
//*[
//  [ data_hazard? or resource_conflict? ->
//    stall!x
//  [] ~data_hazard? and ~resource_conflict? ->
//    proceed!x
//  ]
//]
//		)";
//
//	testBranchFlatten(source, target);
//}
//
//
//TEST(BranchFlatten, MemoryAccess) {
//	std::string source = R"(
//*[
//  [ read_req?(addr) ->
//    [ is_valid(addr) ->
//      mem_read!addr; data_out!data
//    [] ~is_valid(addr) ->
//      error_flag!x
//    ]
//  [] write_req?(addr, data) ->
//    [ is_valid(addr) ->
//      mem_write!(addr, data)
//    [] ~is_valid(addr) ->
//      error_flag!x
//    ]
//  ]
//]
//		)";
//
//	std::string target = R"(
//*[
//  [ read_req?(addr) & is_valid(addr) -> mem_read!(addr); data_out!(data)
//  [] read_req?(addr) & ~is_valid(addr) -> error_flag!
//  [] write_req?(addr, data) & is_valid(addr) -> mem_write!(addr, data)
//  [] write_req?(addr, data) & ~is_valid(addr) -> error_flag!
//  ]
//]
//		)";
//
//	testBranchFlatten(source, target);
//}



TEST(BranchFlatten, Decoder3) {
	std::string source = R"""(while {
		await b2 {
			await b1 {
				await b0 {
					out7.send(v)
				} or await ~b0 {
					out6.send(v)
				}
			} or await ~b1 {
				await b0 {
					out5.send(v)
				} or await ~b0 {
					out4.send(v)
				}
			}
		} or await ~b2 {
			await b1 {
				await b0 {
					out3.send(v)
				} or await ~b0 {
					out2.send(v)
				}
			} or await ~b1 {
				await b0 {
					out1.send(v)
				} or await ~b0 {
					out0.send(v)
				}
			}
		}
	})""";

	std::string target = R"""(while {
		await b2 & b1 & b0 {
			out7.send(v)
		} or await b2 & b1 & ~b0 {
			out6.send(v)
		} or await b2 & ~b1 & b0 {
			out5.send(v)
		} or await b2 & ~b1 & ~b0 {
			out4.send(v)
		} or await ~b2 & b1 & b0 {
			out3.send(v)
		} or await ~b2 & b1 & ~b0 {
			out2.send(v)
		} or await ~b2 & ~b1 & b0 {
			out1.send(v)
		} or await ~b2 & ~b1 & ~b0 {
			out0.send(v)
		}
	})""";

	testBranchFlatten(source, target, true);
}

TEST(BranchFlatten, Decoder4) {
	std::string source = R"""(while {
		await b3 {
			await b2 {
				await b1 {
					await b0 {
						out15.send(v)
					} or await ~b0 {
						out14.send(v)
					}
				} or await ~b1 {
					await b0 {
						out13.send(v)
					} or await ~b0 {
						out12.send(v)
					}
				}
			} or await ~b2 {
				await b1 {
					await b0 {
						out11.send(v)
					} or await ~b0 {
						out10.send(v)
					}
				} or await ~b1 {
					await b0 {
						out9.send(v)
					} or await ~b0 {
						out8.send(v)
					}
				}
			}
		} or await ~b3 {
			await b2 {
				await b1 {
					await b0 {
						out7.send(v)
					} or await ~b0 {
						out6.send(v)
					}
				} or await ~b1 {
					await b0 {
						out5.send(v)
					} or await ~b0 {
						out4.send(v)
					}
				}
			} or await ~b2 {
				await b1 {
					await b0 {
						out3.send(v)
					} or await ~b0 {
						out2.send(v)
					}
				} or await ~b1 {
					await b0 {
						out1.send(v)
					} or await ~b0 {
						out0.send(v)
					}
				}
			}
		}
	})""";

	std::string target = R"""(while {
		await b3 & b2 & b1 & b0 {
			out15.send(v)
		} or await b3 & b2 & b1 & ~b0 {
			out14.send(v)
		} or await b3 & b2 & ~b1 & b0 {
			out13.send(v)
		} or await b3 & b2 & ~b1 & ~b0 {
			out12.send(v)
		} or await b3 & ~b2 & b1 & b0 {
			out11.send(v)
		} or await b3 & ~b2 & b1 & ~b0 {
			out10.send(v)
		} or await b3 & ~b2 & ~b1 & b0 {
			out9.send(v)
		} or await b3 & ~b2 & ~b1 & ~b0 {
			out8.send(v)
		} or await ~b3 & b2 & b1 & b0 {
			out7.send(v)
		} or await ~b3 & b2 & b1 & ~b0 {
			out6.send(v)
		} or await ~b3 & b2 & ~b1 & b0 {
			out5.send(v)
		} or await ~b3 & b2 & ~b1 & ~b0 {
			out4.send(v)
		} or await ~b3 & ~b2 & b1 & b0 {
			out3.send(v)
		} or await ~b3 & ~b2 & b1 & ~b0 {
			out2.send(v)
		} or await ~b3 & ~b2 & ~b1 & b0 {
			out1.send(v)
		} or await ~b3 & ~b2 & ~b1 & ~b0 {
			out0.send(v)
		}
	})""";

	testBranchFlatten(source, target, true);
}

