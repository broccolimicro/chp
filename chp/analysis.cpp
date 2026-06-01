#include "analysis.h"

namespace chp
{

ostream &operator<<(std::ostream &os, const useDefChain &chain) {
    //os << "Use-Def Chain (" << endl;
    //os << "  varIdx: " << chain.varIdx << endl;
    //os << "  name: " << chain.name << endl;
		os << chain.name << "  @" << chain.varIdx << endl << endl;

    os << "defs: ";
    for (TransitionIdx def = 0; def < chain.defs.size(); ++def) {
        os << chain.defs[def];
        if (def + 1 < chain.defs.size())
            os << ", ";
    }

    os << endl << "uses: ";
    for (TransitionIdx use = 0; use < chain.uses.size(); ++use) {
        os << chain.uses[use];
        if (use + 1 < chain.uses.size())
            os << ", ";
    }

		string copyProc = (chain.hasCopyProcess()) ? std::to_string(chain.copyProcess) : "ºø";
    os << endl << "CopyProc? " << copyProc;
    return os;
}

}
