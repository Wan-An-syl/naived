#include "graph.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace temporal_topk {

GraphSnapshot::GraphSnapshot() = default;
GraphSnapshot::GraphSnapshot(std::size_t node_count)
    : adjacency_(node_count), edge_count_(0) {}
std::size_t GraphSnapshot::node_count() const { return adjacency_.size(); }
std::size_t GraphSnapshot::edge_count() const { return edge_count_; }

bool GraphSnapshot::contains_node(NodeId u) const {
  return u >= 0 && static_cast<std::size_t>(u) < adjacency_.size();
}

void GraphSnapshot::ensure_node(NodeId u) {
  if (u < 0) throw std::invalid_argument("node id must be non-negative");
  if (static_cast<std::size_t>(u) >= adjacency_.size()) {
    adjacency_.resize(static_cast<std::size_t>(u) + 1);
  }
}

bool GraphSnapshot::add_edge(NodeId u, NodeId v) {
  if (u == v) return false;
  ensure_node(u);
  ensure_node(v);
  auto& nu = adjacency_[static_cast<std::size_t>(u)];
  auto& nv = adjacency_[static_cast<std::size_t>(v)];
  const bool inserted_u = nu.insert(v).second;
  const bool inserted_v = nv.insert(u).second;
  if (inserted_u != inserted_v) {
    throw std::logic_error("graph adjacency is inconsistent");
  }
  if (inserted_u) ++edge_count_;
  return inserted_u;
}

bool GraphSnapshot::has_edge(NodeId u, NodeId v) const {
  if (!contains_node(u) || !contains_node(v)) return false;
  return adjacency_[static_cast<std::size_t>(u)].find(v) !=
         adjacency_[static_cast<std::size_t>(u)].end();
}

std::size_t GraphSnapshot::degree(NodeId u) const {
  if (!contains_node(u)) return 0;
  return adjacency_[static_cast<std::size_t>(u)].size();
}

std::vector<NodeId> GraphSnapshot::neighbors(NodeId u) const {
  if (!contains_node(u)) return {};
  const auto& n = adjacency_[static_cast<std::size_t>(u)];
  return {n.begin(), n.end()};
}

const std::unordered_set<NodeId>& GraphSnapshot::neighbor_set(NodeId u) const {
  static const std::unordered_set<NodeId> kEmpty;
  if (!contains_node(u)) return kEmpty;
  return adjacency_[static_cast<std::size_t>(u)];
}

std::vector<NodeId> GraphSnapshot::active_nodes() const {
  std::vector<NodeId> out;
  out.reserve(adjacency_.size());
  for (NodeId u = 0; static_cast<std::size_t>(u) < adjacency_.size(); ++u) {
    if (!adjacency_[static_cast<std::size_t>(u)].empty()) out.push_back(u);
  }
  return out;
}

bool NodeChange::empty() const {
  return changed_nodes.empty() && appeared_nodes.empty() && disappeared_nodes.empty();
}

TemporalGraphDataset::TemporalGraphDataset() = default;
std::size_t TemporalGraphDataset::snapshot_count() const { return snapshots_.size(); }
std::size_t TemporalGraphDataset::total_edges() const { return total_edges_; }
std::size_t TemporalGraphDataset::max_node_id() const { return max_node_id_; }
bool TemporalGraphDataset::empty() const { return snapshots_.empty(); }
TimeId TemporalGraphDataset::min_time() const { return min_time_; }
TimeId TemporalGraphDataset::max_time() const { return max_time_; }
const GraphSnapshot& TemporalGraphDataset::snapshot(TimeId t) const {
  return snapshots_.at(time_to_index(t));
}
GraphSnapshot& TemporalGraphDataset::snapshot(TimeId t) {
  return snapshots_.at(time_to_index(t));
}

void TemporalGraphDataset::clear() {
  snapshots_.clear();
  min_time_ = -1;
  max_time_ = -1;
  total_edges_ = 0;
  max_node_id_ = 0;
}

void TemporalGraphDataset::add_temporal_edge(TimeId t, NodeId u, NodeId v) {
  if (t < 0) throw std::invalid_argument("time id must be non-negative");
  const std::size_t idx = ensure_snapshot(t);
  if (snapshots_[idx].add_edge(u, v)) {
    ++total_edges_;
    max_node_id_ = std::max(max_node_id_, static_cast<std::size_t>(std::max(u, v)));
  }
}

void TemporalGraphDataset::load_from_file(const std::string& path) {
  std::ifstream fin(path);
  if (!fin.is_open()) throw std::runtime_error("failed to open temporal graph file: " + path);
  clear();
  std::string line;
  while (std::getline(fin, line)) {
    if (line.empty() || line.front() == '#') continue;
    std::istringstream iss(line);
    TimeId t = -1;
    NodeId u = -1;
    NodeId v = -1;
    if (!(iss >> t >> u >> v)) throw std::runtime_error("invalid temporal graph record: " + line);
    add_temporal_edge(t, u, v);
  }
}

void TemporalGraphDataset::save_to_file(const std::string& path) const {
  std::ofstream fout(path, std::ios::trunc);
  if (!fout.is_open()) throw std::runtime_error("failed to open output file: " + path);
  fout << "# time_id node_u node_v\n";
  for (std::size_t idx = 0; idx < snapshots_.size(); ++idx) {
    const TimeId t = static_cast<TimeId>(idx) + min_time_;
    const auto& g = snapshots_[idx];
    for (NodeId u = 0; static_cast<std::size_t>(u) < g.node_count(); ++u) {
      for (NodeId v : g.neighbor_set(u)) {
        if (u < v) fout << t << ' ' << u << ' ' << v << '\n';
      }
    }
  }
}

NodeChange TemporalGraphDataset::get_node_change(TimeId t_curr, TimeId t_prev) const {
  const auto& curr = snapshot(t_curr);
  const auto& prev = snapshot(t_prev);
  NodeChange delta;
  const std::size_t upper = std::max(curr.node_count(), prev.node_count());
  for (NodeId u = 0; static_cast<std::size_t>(u) < upper; ++u) {
    const bool in_curr = u < static_cast<NodeId>(curr.node_count()) && curr.degree(u) > 0;
    const bool in_prev = u < static_cast<NodeId>(prev.node_count()) && prev.degree(u) > 0;
    if (in_curr && !in_prev) {
      delta.appeared_nodes.insert(u);
      delta.changed_nodes.insert(u);
      continue;
    }
    if (!in_curr && in_prev) {
      delta.disappeared_nodes.insert(u);
      delta.changed_nodes.insert(u);
      continue;
    }
    if (!in_curr && !in_prev) continue;
    if (curr.neighbor_set(u) != prev.neighbor_set(u)) delta.changed_nodes.insert(u);
  }
  return delta;
}

std::size_t TemporalGraphDataset::ensure_snapshot(TimeId t) {
  if (min_time_ < 0) {
    min_time_ = t;
    max_time_ = t;
    snapshots_.resize(1);
    return 0;
  }
  if (t < min_time_) {
    const std::size_t pad = static_cast<std::size_t>(min_time_ - t);
    snapshots_.insert(snapshots_.begin(), pad, GraphSnapshot{});
    min_time_ = t;
  }
  if (t > max_time_) {
    snapshots_.resize(static_cast<std::size_t>(t - min_time_) + 1);
    max_time_ = t;
  }
  return static_cast<std::size_t>(t - min_time_);
}

std::size_t TemporalGraphDataset::time_to_index(TimeId t) const {
  if (min_time_ < 0 || t < min_time_ || t > max_time_) {
    throw std::out_of_range("time id out of range");
  }
  return static_cast<std::size_t>(t - min_time_);
}

Clique::Clique() = default;
Clique::Clique(std::vector<NodeId> nodes, int interval, TimeId born, TimeId seen)
    : vertices(std::move(nodes)), interval_count(interval), born_at(born), last_seen_at(seen) {
  normalize();
  refresh_score();
}
void Clique::normalize() {
  std::sort(vertices.begin(), vertices.end());
  vertices.erase(std::unique(vertices.begin(), vertices.end()), vertices.end());
}
std::size_t Clique::size() const { return vertices.size(); }
void Clique::set_interval_count(int value) {
  interval_count = value;
  refresh_score();
}
void Clique::increment_interval_count(int delta) {
  interval_count += delta;
  refresh_score();
}
double Clique::score() const { return score_cache; }
std::string Clique::signature() const {
  if (vertices.empty()) return "{}";
  std::string out;
  out.reserve(vertices.size() * 6);
  for (std::size_t i = 0; i < vertices.size(); ++i) {
    if (i) out.push_back(',');
    out += std::to_string(vertices[i]);
  }
  return out;
}
bool Clique::operator==(const Clique& rhs) const { return vertices == rhs.vertices; }
bool Clique::is_subset_of(const Clique& rhs) const {
  return std::includes(rhs.vertices.begin(), rhs.vertices.end(), vertices.begin(),
                       vertices.end());
}
bool Clique::is_superset_of(const Clique& rhs) const { return rhs.is_subset_of(*this); }
void Clique::refresh_score() {
  score_cache = static_cast<double>(interval_count) * static_cast<double>(vertices.size());
}

bool CliqueContainer::empty() const { return data_.empty(); }
std::size_t CliqueContainer::size() const { return data_.size(); }
const CliqueContainer::Storage& CliqueContainer::items() const { return data_; }
CliqueContainer::Storage& CliqueContainer::items() { return data_; }
void CliqueContainer::clear() {
  data_.clear();
  index_.clear();
  by_size_.clear();
}
bool CliqueContainer::contains(const Clique& c) const {
  return index_.find(c.signature()) != index_.end();
}
bool CliqueContainer::add(Clique c) {
  c.normalize();
  auto key = c.signature();
  if (index_.find(key) != index_.end()) return false;
  const std::size_t pos = data_.size();
  data_.push_back(std::move(c));
  index_[std::move(key)] = pos;
  by_size_[data_[pos].size()].push_back(pos);
  return true;
}
Clique* CliqueContainer::find(const Clique& c) {
  auto it = index_.find(c.signature());
  if (it == index_.end()) return nullptr;
  return &data_[it->second];
}
const Clique* CliqueContainer::find(const Clique& c) const {
  auto it = index_.find(c.signature());
  if (it == index_.end()) return nullptr;
  return &data_[it->second];
}
std::vector<const Clique*> CliqueContainer::candidates_by_size_range(std::size_t min_size,
                                                                     std::size_t max_size) const {
  std::vector<const Clique*> out;
  for (const auto& kv : by_size_) {
    if (kv.first < min_size || kv.first > max_size) continue;
    for (auto idx : kv.second) out.push_back(&data_[idx]);
  }
  return out;
}

TopKBuffer::TopKBuffer(std::size_t k) : capacity_(k) {}
std::size_t TopKBuffer::capacity() const { return capacity_; }
std::size_t TopKBuffer::size() const { return heap_.size(); }
bool TopKBuffer::empty() const { return heap_.empty(); }
bool TopKBuffer::contains(const Clique& c) const {
  return signatures_.find(c.signature()) != signatures_.end();
}
void TopKBuffer::push(const Clique& c) {
  if (capacity_ == 0) return;
  const auto sig = c.signature();
  if (signatures_.find(sig) != signatures_.end()) return;
  if (heap_.size() < capacity_) {
    heap_.push(c);
    signatures_.insert(sig);
    return;
  }
  if (c.score() <= heap_.top().score()) return;
  signatures_.erase(heap_.top().signature());
  heap_.pop();
  heap_.push(c);
  signatures_.insert(sig);
}

double TopKBuffer::min_score() const { return heap_.empty() ? 0.0 : heap_.top().score(); }
std::vector<Clique> TopKBuffer::sorted_descending() const {
  auto copy = heap_;
  std::vector<Clique> out;
  out.reserve(copy.size());
  while (!copy.empty()) {
    out.push_back(copy.top());
    copy.pop();
  }
  std::sort(out.begin(), out.end(),
            [](const Clique& a, const Clique& b) { return a.score() > b.score(); });
  return out;
}

bool TopKBuffer::CompareByScoreAsc::operator()(const Clique& a, const Clique& b) const {
  return a.score() > b.score();
}

}  // namespace temporal_topk

