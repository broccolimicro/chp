#pragma once

#include <common/standard.h>
#include <common/net.h>
#include <arithmetic/action.h>
#include <petri/graph.h>

#include "state.h"

namespace chp
{

// TODO(edward.bingham) by default, a guard in CHP should be non-atomic
// while a guard in HSE is atomic by default. This comes from the different
// levels of abstraction. An HSE is assumed to be written such that state
// variable insertion has already happened for the majority of the
// handshake and all expressions and transitions are boolean. This means
// that and by default there won't be added transitions between a guard and
// a transition. However, in CHP expressions and transitions are arithmetic
// and it is assumed that hanshake reshuffling and state variable insertion
// hasn't happened yet. This means that by default it is more likely than
// not that a state transition will be inserted between a guard and another
// statement. So, in CHP we need an extra syntax for the user to say either
// "using the atomic complex gate assumption, assume that this expression
// or transition is atomic" or "flag an error if it is not possible to
// implement this expression or transition in an atomic way". This should
// look something like the following:
//
// atomic await condition {
//   action
// } or atomic await condition {
//   action
// }
//
// assume atomic await condition {
//   action
// } or assume atomic await condition {
//   action
// }
//
// As a result if the user specifies the atomic flag, the default is to
// enforce atomicity in the synthesis, and only if they add "assume" do
// they get to use the atomic complex gate assumption.
//
// This means that arithmetic expressions need to be broken apart by
// default in the CHP, so we need to merge the arithmetic expression
// structure into the CHP graph, along with expression simplification, etc.
// This matches fairly well to existing compiler infrastructure, except for
// the direct acknowledgement of concurrency/sequencing independent of data
// dependency. Most compiler backends just have a flat ordered list of
// operators. We need to maintain a parallel/choice/sequence graph of such
// operators, and still be able to apply arithmetic simplification
// operations on top of that.
//
// The arithmetic library is still useful for things that don't fit well
// into that graph structure. In fact, I wonder if it would be possible to
// somehow interpret the simplification rules in the arithmetic library
// into an expression simplification system in the graph datastructure.
//
// Also, this means that I can reasonably put off the quantifier
// elimination algorithm and the base syntax will behave correctly given
// the above definitions. And once I add the quantifier elimination
// algorithm, it will insert nicely with the above new syntax.

using petri::iterator;
using petri::parallel;
using petri::choice;
using petri::sequence;

//TODO(steven.kneiser): migrate named-size_t's beyond this namespace?
typedef size_t TransitionIdx;
typedef size_t VarIdx;
typedef size_t VarValue;
typedef size_t VarDSAIdx;
typedef size_t BlockIdx;

struct place : petri::place
{
	place();
	~place();

	// inherited from petri::place
	// vector<split_group> groups;

	// if true, more than one output transition from this place can be enabled
	// simultaneously. This means that the hardware needs to make a
	// non-deterministic decision about which one to fire. This is generally done
	// with an arbiter.
	bool arbiter;

	static place merge(int composition, const place &p0, const place &p1);
};

ostream &operator<<(ostream &os, const place &p);

struct transition : petri::transition
{
	transition();
	transition(arithmetic::Expression guard, arithmetic::Choice assign=true);
	~transition();

	arithmetic::Expression guard;
	arithmetic::Choice action;

	static transition merge(int composition, const transition &t0, const transition &t1);
	static bool mergeable(int composition, const transition &t0, const transition &t1);

	bool is_infeasible();
	bool is_vacuous();
};

ostream &operator<<(ostream &os, const transition &t);

struct variable {
	variable();
	variable(string name, int region=0);
	~variable();

	string name;
	int region;

	vector<int> remote;
};

//NOTE(steven.kneiser): ProjectionItem inter-operates with size_t by design, so equality & hashing ONLY compare index (not other metadata like isChannel)
//TODO(steven.kneiser): lowercase the camelCase to match codebase
struct ProjectionItem {
	VarIdx index;
	bool isChannel = false;
	bool isSend = false;
	//bool isInternal;
	//TODO(steven.kneiser): does channel partner always exist?
	//ProjectionItem partner;  // if this is a channel, reference other side of isSend

  // Make ProjectionItem operate as size_t
	operator size_t() const noexcept { return index; }
	friend ProjectionItem operator+(ProjectionItem lhs, size_t rhs) {
		lhs.index += rhs;
		return lhs;
	}

	bool operator()(const ProjectionItem& a, const ProjectionItem& b) const {
		return a.index < b.index;
	}
	bool operator()(size_t a, const ProjectionItem& b) const {
		return a < b.index;
	}
	bool operator()(const ProjectionItem& a, size_t b) const {
		return a.index < b;
	}
	ProjectionItem& operator=(const ProjectionItem &other) {
		this->index = other.index;
		this->isChannel = other.isChannel;
		this->isSend = other.isSend;
		return *this;
	}
	ProjectionItem& operator=(size_t num) {
		this->index = num;
		return *this;
	}
	//bool operator==(const ProjectionItem&) const = default;
};

struct graph : petri::graph<chp::place, chp::transition, petri::token, chp::state>
{
	typedef petri::graph<chp::place, chp::transition, petri::token, chp::state> super;

	graph();
	~graph();

	string name;

	vector<variable> vars;

	bool controlFlowGraphReady = false;
	bool useDefChainsReady = false;

	int netIndex(string name, bool define=false);
	int netIndex(string name) const;
	string netAt(int uid) const;
	int netCount() const;

	// TODO(edward.bingham) tie this into the typesystem
	arithmetic::State U() const;

	using super::create;
	int create(variable n = variable());

	void connect_remote(int from, int to);
	vector<vector<int> > remote_groups();

	chp::transition &at(term_index idx);
	arithmetic::Parallel &term(term_index idx);

	using super::merge;
	Mapping<petri::iterator> merge(graph g);

	arithmetic::Expression exclusion(int index) const;
	void expand();
	void flatten(bool debug=false);
	bool isFlat() const;  //TODO: cache in property for quick look-up
	void post_process(bool proper_nesting=false, bool aggressive=false);

	struct controlFlowBlock {
		BlockIdx uid;
		bool reset = false;
		vector<petri::iterator> transitions;

		// analysis metadata
		set<BlockIdx> ins;
		set<BlockIdx> outs;
		//petri::iterator first;
		//petri::iterator last;
		unordered_map<TransitionIdx, VarIdx> gens;  // transition_idx of def -> var defined
		unordered_map<TransitionIdx, TransitionIdx> kills;  // transition_idx of redef -> prev transition_idx def
		unordered_map<VarIdx, VarDSAIdx> preDefs;  // var_idx -> dsa_idx
		unordered_map<VarIdx, VarDSAIdx> postDefs;  // var_idx -> dsa_idx
		//TODO(steven.kneiser): more explicitly rename these as "pre(Block)Defs" & "postBlockDefs"?
	};

	unordered_map<TransitionIdx, BlockIdx> transitionToBlock;
	vector<controlFlowBlock> controlFlowGraph;

	//size_t getReachingDef(VarIdx var_idx, TransitionIdx transition_idx);
	pair<int, vector<TransitionIdx>> getPreviousDefinitions(petri::iterator it, const vector<petri::iterator> &v);
	VarIdx getEnumeratedVar(VarIdx varIdx, VarDSAIdx num, string delimiter="_");
	//TODO(steven.kneiser): would a normalizing (DE)numerator be useful or encourage bad habits?
	//VarIdx getUnenumeratedVar(VarIdx varIdx);

	void increaseBlockVarToDSAIndex(BlockIdx blockIdx, VarIdx varIdx, VarDSAIdx dsaCountAfter);
	unordered_map<VarIdx, VarDSAIdx> mergeDefinitionsBeforeBlock(BlockIdx blockId);
	void computeControlFlowGraph();
	void convertToDSA();

	struct useDefChain {
		string name;
		//VarIdx index;
		//VarDSAIdx DSAIndex = 0;
		vector<TransitionIdx> defs;
		vector<TransitionIdx> uses;
	};

	unordered_map<VarIdx, useDefChain> useDefChains;
	void setUseDef(VarIdx var_idx, TransitionIdx transition_idx, bool is_definition=false);
	void extractUseDefFromExpression(TransitionIdx transition_idx, const arithmetic::Expression& expr, bool is_definition=false);
	void extractUseDefFromTransition(TransitionIdx transition_idx);
	void computeUseDefChains();

	//TODO(steven.kneiser): better name for higher-order transformation? substitution? variable renaming? lifetime / live range splitting?
	//  hmm, it includes renaming defintion AND references, but it's more semantic than just a complete rename
	void renameVarAtTransition(VarIdx varIdx, TransitionIdx transitionIdx);

	//TODO(steven.kneiser): same signatures ...remerge? this->rewriteAssignmentsAsChannels()
	void rewriteEachSingleUseVarAsDirectChannel(
			set<petri::iterator> &umbilicalCords,
			const unordered_map<VarIdx, set<VarIdx>> &invertedDependencySets,
			unordered_map<VarIdx, set<ProjectionItem>> &projectionSets);
	void rewriteEachMultiUseVarAsCopyProcess(
			set<petri::iterator> &umbilicalCords,
			const unordered_map<VarIdx, set<VarIdx>> &invertedDependencySets,
			unordered_map<VarIdx, set<ProjectionItem>> &projectionSets);
	void rewriteEachGuardVarUsedInMultiDefinitionSelectionsAsCopyProcess(
			set<petri::iterator> &umbilicalCords,
			const unordered_map<VarIdx, set<VarIdx>> &invertedDependencySets,
			unordered_map<VarIdx, set<ProjectionItem>> &projectionSets);
	unordered_map<VarIdx, set<ProjectionItem>> computeProjectionSets();
	vector<graph> project();
	vector<graph> decompose();

	//string to_string(const arithmetic::Expression &e) const; //TODO: idea for pretty-printing WITH var names rendered, but I don't want to make dependency
	void renderReset();
};

vector<VarIdx> getVarsFromExpression(const arithmetic::Expression &e);
vector<VarIdx> findInputChannelsInExpression(const arithmetic::Expression &e);
vector<VarIdx> findOutputChannelsInExpression(const arithmetic::Expression &e);
bool isDisqualifyingItemInExpression(const arithmetic::Expression &e, const set<ProjectionItem> &items, bool debug=false);

}
