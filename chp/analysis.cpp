#include "analysis.h"

namespace chp
{

ostream &operator<<(std::ostream &os, const useDefChain &chain) {
    os << "Use-Def Chain {" << endl;
    os << "  varIdx: " << chain.varIdx << endl;
    os << "  name: \"" << chain.name << endl;
    os << "  defs: [";

    for (size_t i = 0; i < chain.defs.size(); ++i) {
        os << chain.defs[i];
        if (i + 1 < chain.defs.size())
            os << ", ";
    }

    os << "]" << endl;
    os << "  uses: [";

    for (size_t i = 0; i < chain.uses.size(); ++i) {
        os << chain.uses[i];
        if (i + 1 < chain.uses.size())
            os << ", ";
    }
    os << "]" << endl << "}";

    return os;
}

}
