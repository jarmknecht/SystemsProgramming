#ifndef DOT_H
#define DOT_H

#include "graph.h"
#include <fstream>
#include <iostream>

// Save the graph object in a Graphviz dot file format
void save_graph(Graph &g, char *filename);

#endif