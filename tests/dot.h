#pragma once

#include <cmath>
#include <string>

#ifdef GRAPHVIZ_SUPPORTED
namespace graphviz
{
	#include <graphviz/cgraph.h>
	#include <graphviz/gvc.h>
}
#endif

using std::string;

namespace gvdot {

void render(string filename, string content);
	
}
