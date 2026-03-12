#pragma once

#include <cstddef>
#include <cstdint>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace temporal_topk {

using NodeId = std::int32_t;
using TimeId = std::int32_t;

class GraphSnapshot {
 public:
  GraphSnapshot();
  explicit GraphSnapshot(std::size_t node_count);

  std::size_t node_count() const;
  std::size_t edge_count() const;

  bool contains_node(NodeId u) const;
  void ensure_node(NodeId u);
  bool add_edge(NodeId u, NodeId v);

  bool has_edge(NodeId u, NodeId v) const;
  std::size_t degree(NodeId u) const;
  std::vector<NodeId> neighbors(NodeId u) const;
  const std::unordered_set<NodeId>& neighbor_set(NodeId u) const;
  std::vector<NodeId> active_nodes() const;

 private:
  std::vector<std::unordered_set<NodeId>> adjacency_;
  std::size_t edge_count_ = 0;
};

struct NodeChange {
  std::unordered_set<NodeId> changed_nodes;
  std::unordered_set<NodeId> appeared_nodes;
  std::unordered_set<NodeId> disappeared_nodes;

  bool empty() const;
};

class TemporalGraphDataset {
 public:
  TemporalGraphDataset();

  std::size_t snapshot_count() const;
  std::size_t total_edges() const;
  std::size_t max_node_id() const;
  bool empty() const;

  TimeId min_time() const;
  TimeId max_time() const;

  const GraphSnapshot& snapshot(TimeId t) const;
  GraphSnapshot& snapshot(TimeId t);

  void clear();
  void add_temporal_edge(TimeId t, NodeId u, NodeId v);

  void load_from_file(const std::string& path);
  void save_to_file(const std::string& path) const;

  NodeChange get_node_change(TimeId t_curr, TimeId t_prev) const;

 private:
  std::size_t ensure_snapshot(TimeId t);
  std::size_t time_to_index(TimeId t) const;

  std::vector<GraphSnapshot> snapshots_;
  TimeId min_time_ = -1;
  TimeId max_time_ = -1;
  std::size_t total_edges_ = 0;
  std::size_t max_node_id_ = 0;
};

struct Clique {
  std::vector<NodeId> vertices;
  int interval_count = 0;
  double score_cache = 0.0;
  TimeId born_at = -1;
  TimeId last_seen_at = -1;
  bool invalidated = false;

  Clique();
  explicit Clique(std::vector<NodeId> nodes,
                  int interval = 0,
                  TimeId born = -1,
                  TimeId seen = -1);

  void normalize();
  std::size_t size() const;

  void set_interval_count(int value);
  void increment_interval_count(int delta = 1);

  double score() const;
  std::string signature() const;

  bool operator==(const Clique& rhs) const;
  bool is_subset_of(const Clique& rhs) const;
  bool is_superset_of(const Clique& rhs) const;

 private:
  void refresh_score();
};

class CliqueContainer {
 public:
  using Storage = std::vector<Clique>;

  bool empty() const;
  std::size_t size() const;

  const Storage& items() const;
  Storage& items();

  void clear();
  bool contains(const Clique& c) const;
  bool add(Clique c);

  Clique* find(const Clique& c);
  const Clique* find(const Clique& c) const;

  std::vector<const Clique*> candidates_by_size_range(std::size_t min_size,
                                                       std::size_t max_size) const;

  template <typename F>
  void for_each(F&& fn) {
    for (auto& c : data_) fn(c);
  }

  template <typename F>
  void for_each(F&& fn) const {
    for (const auto& c : data_) fn(c);
  }

 private:
  Storage data_;
  std::unordered_map<std::string, std::size_t> index_;
  std::unordered_map<std::size_t, std::vector<std::size_t>> by_size_;
};

class TopKBuffer {
 public:
  explicit TopKBuffer(std::size_t k);

  std::size_t capacity() const;
  std::size_t size() const;
  bool empty() const;

  bool contains(const Clique& c) const;
  void push(const Clique& c);
  double min_score() const;

  std::vector<Clique> sorted_descending() const;

 private:
  struct CompareByScoreAsc {
    bool operator()(const Clique& a, const Clique& b) const;
  };

  std::size_t capacity_ = 0;
  std::priority_queue<Clique, std::vector<Clique>, CompareByScoreAsc> heap_;
  std::unordered_set<std::string> signatures_;
};

}  // namespace temporal_topk

