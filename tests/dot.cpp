#include "dot.h"
#include <string>
#include <common/message.h>

namespace gvdot {

void render(string filename, string content) {
	string format = "png";
	size_t pos = filename.find_last_of(".");
	if (pos != string::npos) {
		format = filename.substr(pos+1);
		filename = filename.substr(0, pos);
	}

	if (format == "dot") {
		FILE *file = fopen((filename+".dot").c_str(), "w");
		fprintf(file, "%s\n", content.c_str());
		fclose(file);
	} else {
#ifdef GRAPHVIZ_SUPPORTED
		// Render an SVG for every layout engine
		/*
		format = "svg";
    const char* layouts[] = {"dot", "neato", "fdp", "sfdp", "twopi", "circo"};
    size_t num_layouts = sizeof(layouts) / sizeof(layouts[0]);

    for (size_t i = 0; i < num_layouts; ++i) {
        const char* layout = layouts[i];

        graphviz::Agraph_t* G = graphviz::agmemread(content.c_str());
        graphviz::GVC_t* gvc = graphviz::gvContext();
        graphviz::gvLayout(gvc, G, layout);
        graphviz::agsafeset(G, const_cast<char *>("dpi"), const_cast<char *>("300"), const_cast<char *>(""));

        std::string outname = filename + "-" + layout + "." + format;
				std::cout << "graphviz gvdot::render(" << outname << ")" << endl;
        graphviz::gvRenderFilename(gvc, G, format.c_str(), outname.c_str());

        graphviz::gvFreeLayout(gvc, G);
        graphviz::agclose(G);
        graphviz::gvFreeContext(gvc);
    }
		*/

		// Render classic png
		format = "png";
		graphviz::Agraph_t* G = graphviz::agmemread(content.c_str());
		graphviz::GVC_t* gvc = graphviz::gvContext();
		graphviz::gvLayout(gvc, G, "dot");
		graphviz::agsafeset(G, const_cast<char *>("dpi"), const_cast<char *>("300"), const_cast<char *>(""));
		graphviz::gvRenderFilename(gvc, G, format.c_str(), (filename+"."+format).c_str());
		graphviz::gvFreeLayout(gvc, G);
		graphviz::agclose(G);
		graphviz::gvFreeContext(gvc);

#else
		string tfilename = filename;
		FILE *temp = NULL;
		int num = 0;
		for (; temp == NULL; num++)
			temp = fopen((tfilename + (num > 0 ? to_string(num) : "") + ".dot").c_str(), "wx");
		num--;
		tfilename += (num > 0 ? to_string(num) : "") + ".dot";

		fprintf(temp, "%s\n", content.c_str());
		fclose(temp);

		if (system(("dot -T" + format + " " + tfilename + " > " + filename + "." + format).c_str()) != 0)
			error("", "Graphviz DOT not supported", __FILE__, __LINE__);
		else if (system(("rm -f " + tfilename).c_str()) != 0)
			warning("", "Temporary files not cleaned up", __FILE__, __LINE__);
#endif
	}
}

}
