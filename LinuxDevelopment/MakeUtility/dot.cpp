#include "dot.h"

void save_graph(Graph &g, char *filename) {
  ofstream fout;
  fout.open(filename);
  if (fout.fail()) { return; }

  fout << "graph g {"                         << endl;
  fout << "  node [style=bold, shape=circle, colorscheme=dark28, color=5, fontcolor=white, style=filled]" << endl;
  fout << "  edge [style=bold, colorscheme=dark28, color=1]" << endl;

  // iterate through all nodes of the graph
  for (int i = 0; i < g.get_nodes(); ++i) {
    // print the node
    fout << "  " << i << endl;
    // print all its edges (unless they were printed already)
    for (int j = 0; j < i; ++j) {
      if (g.adjacent(i,j)) {
        fout << "  " << i << " -- " << j << endl;
      }
    }
  }
  fout << "}"<< endl;
  fout.close();
}