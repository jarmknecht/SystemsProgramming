#include "graph.h"
#include "random.h"
//comment
Graph::Graph(int nn, double p) :
  n(nn),
  m(nn, vector<bool>(nn, false)) // make a matrix nn-by-nn initialized with false
{
  for(int i = 0; i < n; ++i) {
    for(int j = 0; j < i; ++j) {
      if (Random::uniform_double() < p) {
        m[i][j] = true;
        m[j][i] = true; // adj matrix is symmetric
      }
    }
  }
}

int Graph::get_nodes() {
  return n;
}

int Graph::get_edges() {
  int sum = 0;
  for(int i = 0; i < n; ++i) {
    for(int j = 0; j <= i; ++j) {
      if (adjacent(i,j)) sum ++;
    }
  }
  return sum;
}

bool Graph::adjacent(int i, int j) {
  if (i >= 0 && i < n && j >= 0 && j < n) 
    return m[i][j];
  else
    return false;
}
