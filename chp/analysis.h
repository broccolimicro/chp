#pragma once

#include <common/standard.h>

namespace chp
{

//TODO(steven.kneiser): ultimately, we should pass around some sort of `[Static]Analysis` pass

//TODO(steven.kneiser): migrate named-size_t's beyond this namespace? Already duplicated in chp/graph.h
typedef size_t BlockIdx;
typedef size_t TransitionIdx;
typedef size_t UseDefIdx;
typedef size_t VarDSAIdx;
typedef size_t VarIdx;
typedef size_t VarValue;

struct useDefChain {
	//TODO(steven.kneiser): UseDefIdx index;
	string name;  //TODO(steven.kneiser): delete. Should be reference/looked-up in this->vars
	VarIdx varIdx;
	bool isChannel = false;
	bool isSend = false;
	//VarDSAIdx varDSAIdx = 0;  //std::numeric_limits<size_t>::max();
	//bool isChannel = false;  //TODO(steven.kneiser): perhaps this should be more like ProjectionItem, if we're going to encapsulate ProjSets
	set<TransitionIdx> defs;
	set<TransitionIdx> uses;


	//TODO(steven.kneiser): these should eventually be vector<useDefChain*>,
  //   no: vector<useDefIdx> into g.useDefChains would fit our pattern
	set<UseDefIdx> dependsOn;
	set<UseDefIdx> requiredFor;
	//TODO(steven.kneiser): integrate depSet & invDepSet here below
	//unordered_map<VarIdx, TransitionIdx> next, prev;
	//unordered_map<TransitionIdx, VarIdx> next, prev; ??
	//Mapping<size_t>(...) neighbors;  // should be TransitionIdx <-> VarIdx
	// perhaps ...

	//TODO(steven.kneiser): aHA: here's where our idempotent/singleton should dedup/resolve/merge
	//VarIdx forkVarIdx = std::numeric_limits<VarIdx>::max();
	TransitionIdx copyProcess = std::numeric_limits<UseDefIdx>::max();  //TODO(steven.kneiser): return to UseDefIdx
	//TODO(steven.kneiser): should this be a vector<TrIdx> for each tail?
	//   no, this should become another useDefIdx
	//IDEA(steven.kneiser): while UseDefIdx should be more correct,
	//  I really WANT just a: TransitionIdx fork;

	bool hasCopyProcess() const {
		return this->copyProcess != std::numeric_limits<UseDefIdx>::max();
	}

	//TODO(steven.kneiser): implement
	//bool hasConsumerVar() const { return false; }
	//VarIdx getConsumervar() const {
	//	return 0;
	//}

	//TODO(steven.kneiser): getDefinition(), getCopyProcess(), etc etc etc helpers to handle the frequent error-handling/bound-checking
	//   ...aHA, also chp::graph should have a getUseDef() to handle the errors too
	// or at very least follow the "isValid" pattern we use with Transitions

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
