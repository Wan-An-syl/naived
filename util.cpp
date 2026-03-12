#include "util.h"

#include <algorithm>

namespace temporal_topk {

Edge::Edge() = default;
Edge::Edge(NodeId a, NodeId b) {
  if (a <= b) {
    u = a;
    v = b;
  } else {
    u = b;
    v = a;
  }
}

bool Edge::operator==(const Edge& rhs) const { return u == rhs.u && v == rhs.v; }

std::size_t EdgeHash::operator()(const Edge& e) const noexcept {
  return (static_cast<std::size_t>(e.u) << 32) ^ static_cast<std::size_t>(e.v);
}

bool SetUtils::is_strict_subset(const std::vector<NodeId>& sub,
                                const std::vector<NodeId>& super) {
  if (sub.size() >= super.size()) return false;
  return std::includes(super.begin(), super.end(), sub.begin(), sub.end());
}

bool SetUtils::is_strict_subset(const Clique& sub, const Clique& super) {
  return is_strict_subset(sub.vertices, super.vertices);
}

bool GraphDelta::empty() const {
  return added_nodes.empty() && removed_nodes.empty() && added_edges.empty() &&
         removed_edges.empty();
}

GraphDelta GraphDeltaAnalyzer::diff(const GraphSnapshot& curr, const GraphSnapshot& prev) {
  GraphDelta delta;
  const std::size_t upper = std::max(curr.node_count(), prev.node_count());
  for (NodeId u = 0; static_cast<std::size_t>(u) < upper; ++u) {
    const bool in_curr = u < static_cast<NodeId>(curr.node_count()) && curr.degree(u) > 0;
    const bool in_prev = u < static_cast<NodeId>(prev.node_count()) && prev.degree(u) > 0;
    if (in_curr && !in_prev) delta.added_nodes.insert(u);
    if (!in_curr && in_prev) delta.removed_nodes.insert(u);
  }

  for (NodeId u = 0; static_cast<std::size_t>(u) < curr.node_count(); ++u) {
    for (NodeId v : curr.neighbor_set(u)) {
      if (u >= v) continue;
      if (!prev.has_edge(u, v)) delta.added_edges.insert(Edge(u, v));
    }
  }
  for (NodeId u = 0; static_cast<std::size_t>(u) < prev.node_count(); ++u) {
    for (NodeId v : prev.neighbor_set(u)) {
      if (u >= v) continue;
      if (!curr.has_edge(u, v)) delta.removed_edges.insert(Edge(u, v));
    }
  }
  return delta;
}

bool InvalidationUtils::is_invalidated(const Clique& c,
                                       const GraphSnapshot& curr,
                                       const GraphDelta& delta) {
  for (NodeId v : c.vertices) {
    if (delta.removed_nodes.find(v) != delta.removed_nodes.end()) return true;
  }
  for (std::size_t i = 0; i < c.vertices.size(); ++i) {
    for (std::size_t j = i + 1; j < c.vertices.size(); ++j) {
      Edge e(c.vertices[i], c.vertices[j]);
      if (delta.removed_edges.find(e) != delta.removed_edges.end()) return true;
    }
  }
  for (std::size_t i = 0; i < c.vertices.size(); ++i) {
    for (std::size_t j = i + 1; j < c.vertices.size(); ++j) {
      if (!curr.has_edge(c.vertices[i], c.vertices[j])) return true;
    }
  }
  return false;
}

}  // namespace temporal_topk

