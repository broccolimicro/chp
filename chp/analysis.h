#pragma once

#include <common/standard.h>

namespace chp
{

//TODO(steven.kneiser): ultimately, we should pass around some sort of `[Static]Analysis` pass

//TODO(steven.kneiser): migrate named-size_t's beyond this namespace? Already duplicated in chp/graph.h
typedef size_t TransitionIdx;
typedef size_t VarIdx;
typedef size_t VarValue;
typedef size_t VarDSAIdx;
typedef size_t BlockIdx;

struct useDefChain {
	string name;
	VarIdx varIdx;
	//VarDSAIdx DSAIndex = 0;
	vector<TransitionIdx> defs;
	vector<TransitionIdx> uses;

	//TODO(steven.kneiser): integrate depSet & invDepSet here below
	//unordered_map<VarIdx, TransitionIdx> next, prev;
	//unordered_map<TransitionIdx, VarIdx> next, prev; ??
	//Mapping<size_t>(...) neighbors;  // should be TransitionIdx <-> VarIdx
	// perhaps ...
	friend ostream &operator<<(std::ostream &os, const useDefChain &chain);
};


//TODO(steven.kneiser): First, migrate these from chp::graph. Getters, setters, etc. Explicitly pass in a chp::graph
//void setUseDef(VarIdx var_idx, TransitionIdx transition_idx, bool is_definition=false);
////TODO(steven.kneiser): even if they update multiple useDefChains,
//// ideally this would return a more transparent vec/set of new useDefChainss
//// without modifying this->useDefChains in-place. Sometimes I just want to read.
//void extractUseDefFromExpression(TransitionIdx transition_idx, const arithmetic::Expression& expr, bool is_definition=false);
//void extractUseDefFromTransition(TransitionIdx transition_idx);
//void computeUseDefChains();


//TODO(steven.kneiser): migrate depSets, invDepSets, & ProjSets ...and controlFlowBlocks over time
//  This is the analysis/synthesis/decomposition `petri::pass` infra for lib/chp
// like prs/bubble.h, hse/synthesize.h, etc

}
