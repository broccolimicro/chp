#pragma once

#include <set>

#include <common/mapping.h>
#include <flow/func.h>

#include "graph.h"

namespace chp {

flow::Func synthesizeFuncFromCHP(const graph &g, bool debug=false);

}
