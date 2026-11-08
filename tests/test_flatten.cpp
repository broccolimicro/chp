#include <gtest/gtest.h>

#include "helpers.h"
#include "dot.h"

#include <petri/tree.h>
#include <petri/synthesize.h>
#include <chp/synthesize.h>
#include <interpret_chp/export_cog.h>
#include <interpret_chp/export_dot.h>
#include <interpret_flow/export_dot.h>

using std::filesystem::absolute;
using std::filesystem::current_path;

void testBranchFlatten(const string &source, const string &target, bool debug=false) {
	const ::testing::TestInfo* const testInfo =
			::testing::UnitTest::GetInstance()->current_test_info();
	std::string testSuiteName = testInfo->test_suite_name();
	std::string testName = testInfo->name();
	std::string path = (BUILD_DIR / (testSuiteName + "_" + testName)).string();

	chp::graph sourceGraph = importCHPFromCogString("", source);
	sourceGraph.flatten(debug);

	if (debug) {
		gvdot::render(path, chp::export_graph(sourceGraph, true).to_string());
	}

	EXPECT_TRUE(sourceGraph.isFlat()) << "result not flat";
	EXPECT_TRUE(is_clean());

	petri::graph<chp::place, controlflow::Tree<chp::transition, chp::place>, chp::state> result;
	EXPECT_TRUE(graph_to_tree(result, sourceGraph)) << "result not properly nested";
	if (debug) {
		result.print();
	}
	for (size_t i = 0; i < result.transitions.size(); i++) {
		if (not result.transitions.is_valid(i)) continue;

		expectEq(target, parse_cog::export_cog(result.transitions[i], sourceGraph).to_string(""));
	}
}

TEST(BranchFlatten, Buffer) {
	std::string source = R"""(while {
	l = L.recv()
	R.send(l)
})""";

	std::string target = R"""(until {
	l=L.recv()
	R.send(l)
})""";

	testBranchFlatten(source, target, true);
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

	std::string target = R"""(until {
	c=C.recv()
	unless c==1 {
		if c==0
		x=A.recv()
		R.send(x)
		c=C.recv()
	}
	x=B.recv()
	R.send(x)
})""";

	testBranchFlatten(source, target, true);
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

	std::string target = R"""(until {
	c=C.recv()
	x=L.recv()
	unless c==1 {
		if c==0
		A.send(x)
		c=C.recv()
		x=L.recv()
	}
	B.send(x)
})""";/*R"""(until {
	if C.probe() == 0 & L.probe() {
		C.recv()
		x = L.recv()
		A.send(x)
	} or if C.probe() == 1 & L.probe() {
		C.recv()
		x = L.recv()
		B.send(x)
	}
})""";*/

	testBranchFlatten(source, target);
}

/*
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
*/
