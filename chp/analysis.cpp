#include "analysis.h"

namespace chp
{

ostream &operator<<(std::ostream &os, const useDefChain &chain) {
    //os << "Use-Def Chain (" << endl;
    //os << "  varIdx: " << chain.varIdx << endl;
    //os << "  name: " << chain.name << endl;
		os << chain.name << "  @" << chain.varIdx << endl;

		if (not chain.defs.empty()) {
			os << endl << "defs: ";
			for (auto it = chain.defs.begin(); it != chain.defs.end(); ++it) {
				if (it != chain.defs.begin()) { os << ", "; }
				os << *it;
			}
		}

		if (not chain.uses.empty()) {
			os << endl << "uses: ";
			for (auto it = chain.uses.begin(); it != chain.uses.end(); ++it) {
				if (it != chain.uses.begin()) { os << ", "; }
				os << *it;
			}
		}

		if (not chain.ins.empty()) {
			os << endl << "ins: ";
			for (auto it = chain.ins.begin(); it != chain.ins.end(); ++it) {
				if (it != chain.ins.begin()) { os << ", "; }
				os << it->first << ":" << it->second;
			}
		}

		if (not chain.outs.empty()) {
			os << endl << "outs: ";
			for (auto it = chain.outs.begin(); it != chain.outs.end(); ++it) {
				if (it != chain.outs.begin()) { os << ", "; }
				os << it->first << ":" << it->second;
			}
		}

		bool hasCopyProcess = chain.hasCopyProcess();
		bool isSpecial = hasCopyProcess || chain.isChannel;
		if (isSpecial) { os << endl; }

		if (hasCopyProcess) {
			os << endl << "CopyProc: " << std::to_string(chain.copyProcess);
		}
		if (chain.isChannel) {
			os << endl << "chan: " << (chain.isSend ? "!" : "?");
		}
    return os;
}

}
