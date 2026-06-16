#include "analysis.h"

namespace chp
{

ostream &operator<<(std::ostream &os, const useDefChain &chain) {
    //os << "Use-Def Chain (" << endl;
    //os << "  varIdx: " << chain.varIdx << endl;
    //os << "  name: " << chain.name << endl;
		os << chain.name << "  @" << chain.varIdx << endl;

    os << endl << "defs: ";
    for (auto it = chain.defs.begin(); it != chain.defs.end(); ++it) {
			if (it != chain.defs.begin()) { os << ", "; }
			os << *it;
    }

    os << endl << "uses: ";
    for (auto it = chain.uses.begin(); it != chain.uses.end(); ++it) {
			if (it != chain.uses.begin()) { os << ", "; }
        os << *it;
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
