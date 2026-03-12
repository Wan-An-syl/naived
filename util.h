#pragma once

#include <unordered_set>
#include <vector>

#include "graph.h"

namespace temporal_topk {

struct Edge {
  NodeId u = -1;
  NodeId v = -1;

  Edge();
  Edge(NodeId a, NodeId b);

  bool operator==(const Edge& rhs) const;
};

struct EdgeHash {
  std::size_t operator()(const Edge& e) const noexcept;
};

class SetUtils {
 public:
  static bool is_strict_subset(const std::vector<NodeId>& sub,
                               const std::vector<NodeId>& super);
  static bool is_strict_subset(const Clique& sub, const Clique& super);
};

struct GraphDelta {
  std::unordered_set<NodeId> added_nodes;
  std::unordered_set<NodeId> removed_nodes;
  std::unordered_set<Edge, EdgeHash> added_edges;
  std::unordered_set<Edge, EdgeHash> removed_edges;

  bool empty() const;
};

class GraphDeltaAnalyzer {
 public:
  static GraphDelta diff(const GraphSnapshot& curr, const GraphSnapshot& prev);
};

class InvalidationUtils {
 public:
  static bool is_invalidated(const Clique& c,
                             const GraphSnapshot& curr,
                             const GraphDelta& delta);
};

}  // namespace temporal_topk

