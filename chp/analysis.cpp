#include "analysis.h"

namespace chp
{

ostream &operator<<(std::ostream &os, const useDefChain &chain) {
    //os << "Use-Def Chain (" << endl;
    //os << "  varIdx: " << chain.varIdx << endl;
    //os << "  name: " << chain.name << endl;
		os << chain.name << "  @" << chain.varIdx << endl << endl;

    os << "defs: ";
    for (auto it = chain.defs.begin(); it != chain.defs.end(); ++it) {
			if (it != chain.defs.begin()) { os << ", "; }
			os << *it;
    }

    os << endl << "uses: ";
    for (auto it = chain.uses.begin(); it != chain.uses.end(); ++it) {
			if (it != chain.uses.begin()) { os << ", "; }
        os << *it;
    }

		string isCopyProc = (chain.hasCopyProcess()) ? std::to_string(chain.copyProcess) : "ºø";
    os << endl << "Channel? " << (chain.isChannel ? (chain.isSend ? "Y!" : "Y?") : "N");
    os << endl << "CopyProc? " << isCopyProc;
    return os;
}

}
