#include "algorithm.h"

#include <algorithm>
#include <utility>

namespace temporal_topk {

CliqueContainer BKMaximalCliqueEnumerator::run(const GraphSnapshot& g,
                                               TimeId t,
                                               int init_interval) {
  CliqueContainer out;
  std::vector<NodeId> p;
  p.reserve(g.node_count());
  for (NodeId u = 0; static_cast<std::size_t>(u) < g.node_count(); ++u) {
    if (g.degree(u) > 0) p.push_back(u);
  }
  std::vector<NodeId> r;
  std::vector<NodeId> x;
  bron_kerbosch(g, r, p, x, out, t, init_interval);
  return out;
}

std::vector<NodeId> BKMaximalCliqueEnumerator::intersect_with_neighbors(
    const std::vector<NodeId>& in,
    const GraphSnapshot& g,
    NodeId pivot) {
  std::vector<NodeId> out;
  const auto& nb = g.neighbor_set(pivot);
  out.reserve(in.size());
  for (NodeId x : in) {
    if (nb.find(x) != nb.end()) out.push_back(x);
  }
  return out;
}

void BKMaximalCliqueEnumerator::bron_kerbosch(const GraphSnapshot& g,
                                              std::vector<NodeId> r,
                                              std::vector<NodeId> p,
                                              std::vector<NodeId> x,
                                              CliqueContainer& out,
                                              TimeId t,
                                              int init_interval) {
  if (p.empty() && x.empty()) {
    out.add(Clique(std::move(r), init_interval, t, t));
    return;
  }
  auto p_copy = p;
  for (NodeId v : p_copy) {
    std::vector<NodeId> r_next = r;
    r_next.push_back(v);
    auto p_next = intersect_with_neighbors(p, g, v);
    auto x_next = intersect_with_neighbors(x, g, v);
    bron_kerbosch(g, std::move(r_next), std::move(p_next), std::move(x_next), out, t,
                  init_interval);

    p.erase(std::remove(p.begin(), p.end(), v), p.end());
    x.push_back(v);
  }
}

TopKManager::TopKManager(std::size_t k) : k_(k) {}

void TopKManager::consider(const Clique& c) {
  if (k_ == 0) return;
  if (c.size() < 3) return;

  const auto sig = c.signature();
  auto it = best_by_signature_.find(sig);
  if (it == best_by_signature_.end()) {
    best_by_signature_.emplace(sig, c);
    return;
  }

  if (c.score() > it->second.score()) {
    it->second = c;
  }
}

std::vector<Clique> TopKManager::export_sorted() const {
  std::vector<Clique> all;
  all.reserve(best_by_signature_.size());
  for (const auto& kv : best_by_signature_) {
    all.push_back(kv.second);
  }

  std::sort(all.begin(), all.end(),
            [](const Clique& a, const Clique& b) {
              if (a.score() != b.score()) return a.score() > b.score();
              if (a.size() != b.size()) return a.size() > b.size();
              if (a.born_at != b.born_at) return a.born_at < b.born_at;
              if (a.last_seen_at != b.last_seen_at) return a.last_seen_at < b.last_seen_at;
              return a.signature() > b.signature();
            });
            [](const Clique& a, const Clique& b) { return a.score() > b.score(); });

  if (all.size() > k_) {
    all.resize(k_);
  }
  return all;
}
TopKManager::TopKManager(std::size_t k) : q_(k) {}

void TopKManager::consider(const Clique& c) {
  if (q_.contains(c)) return;
  if (q_.size() == q_.capacity() && c.score() <= q_.min_score()) return;
  q_.push(c);
}

std::vector<Clique> TopKManager::export_sorted() const { return q_.sorted_descending(); }

CliqueContainer IncrementalEngine::initialize(const GraphSnapshot& g0, TimeId t0) const {
  return BKMaximalCliqueEnumerator::run(g0, t0, 1);
}

CliqueContainer IncrementalEngine::inc_rmce(
    const GraphSnapshot& curr,
    const std::unordered_set<Edge, EdgeHash>& added_edges,
    TimeId t) const {
  CliqueContainer m_new;
  for (const auto& e : added_edges) {
    if (!curr.has_edge(e.u, e.v)) continue;

    std::vector<NodeId> p;
    const auto& nu = curr.neighbor_set(e.u);
    const auto& nv = curr.neighbor_set(e.v);
    const auto& small = (nu.size() < nv.size()) ? nu : nv;
    const auto& large = (nu.size() < nv.size()) ? nv : nu;
    p.reserve(small.size());
    for (NodeId x : small) {
      if (large.find(x) != large.end()) p.push_back(x);
    }

    std::vector<NodeId> r = {e.u, e.v};
    std::vector<NodeId> x;
    local_bk(curr, r, p, x, m_new, t);
  }
  return m_new;
}

IncrementalEngine::StepResult IncrementalEngine::process_step(const GraphSnapshot& curr,
                                                              const GraphSnapshot& prev,
                                                              const CliqueContainer& m_prev,
                                                              TimeId t) const {
  GraphDelta delta = GraphDeltaAnalyzer::diff(curr, prev);
  StepResult ret;

  // If removals happen, new maximal cliques can emerge from split components;
  // use full BK to keep semantics aligned with expected outputs.
  if (!delta.removed_edges.empty() || !delta.removed_nodes.empty()) {
    ret.m_new = BKMaximalCliqueEnumerator::run(curr, t, 1);
  } else {
    ret.m_new = inc_rmce(curr, delta.added_edges, t);
  }

  process_clique_relationships(ret.m_new, m_prev, ret.p_set, t, ret.p_emitted);
  ret.m_new = inc_rmce(curr, delta.added_edges, t);

  process_clique_relationships(ret.m_new, m_prev, ret.p_set, t);

  m_prev.for_each([&](const Clique& c) {
    if (!InvalidationUtils::is_invalidated(c, curr, delta)) ret.m_curr.add(c);
  });
  ret.m_new.for_each([&](const Clique& c) { ret.m_curr.add(c); });

  return ret;
}

void IncrementalEngine::process_clique_relationships(
    CliqueContainer& m_new,
    const CliqueContainer& m_prev,
    std::unordered_set<std::string>& p_set,
    TimeId t,
    std::vector<Clique>& p_emitted) const {
    TimeId t) const {
  m_new.for_each([&](Clique& c_new) {
    if (p_set.find(c_new.signature()) != p_set.end()) return;

    bool matched = false;
    for (const auto* c_prev :
         m_prev.candidates_by_size_range(1, static_cast<std::size_t>(-1))) {
      if (SetUtils::is_strict_subset(c_new, *c_prev)) {
        c_new.set_interval_count(c_prev->interval_count + 1);
        c_new.born_at = t;
        c_new.last_seen_at = t;
        p_set.insert(c_new.signature());
        p_emitted.push_back(c_new);
        c_new.born_at = c_prev->born_at;
        c_new.last_seen_at = t;
        p_set.insert(c_new.signature());
        matched = true;
        break;
      }
      if (SetUtils::is_strict_subset(*c_prev, c_new)) {
        c_new.set_interval_count(1);
        c_new.born_at = t;
        c_new.last_seen_at = t;
        p_set.insert(c_new.signature());
        p_set.insert(c_prev->signature());

        Clique prev_updated = *c_prev;
        prev_updated.increment_interval_count(1);
        prev_updated.last_seen_at = t;
        p_emitted.push_back(prev_updated);
        p_emitted.push_back(c_new);

        matched = true;
        break;
      }
    }

    if (!matched) {
      c_new.set_interval_count(1);
      c_new.born_at = t;
      c_new.last_seen_at = t;
      p_set.insert(c_new.signature());
      p_emitted.push_back(c_new);
    }
  });
}

std::vector<NodeId> IncrementalEngine::intersect_with_neighbors(
    const std::vector<NodeId>& in,
    const GraphSnapshot& g,
    NodeId pivot) {
  std::vector<NodeId> out;
  const auto& nb = g.neighbor_set(pivot);
  out.reserve(in.size());
  for (NodeId x : in) {
    if (nb.find(x) != nb.end()) out.push_back(x);
  }
  return out;
}

void IncrementalEngine::local_bk(const GraphSnapshot& g,
                                 std::vector<NodeId> r,
                                 std::vector<NodeId> p,
                                 std::vector<NodeId> x,
                                 CliqueContainer& out,
                                 TimeId t) {
  if (p.empty() && x.empty()) {
    out.add(Clique(std::move(r), 1, t, t));
    return;
  }

  auto p_copy = p;
  for (NodeId v : p_copy) {
    std::vector<NodeId> r_next = r;
    r_next.push_back(v);
    auto p_next = intersect_with_neighbors(p, g, v);
    auto x_next = intersect_with_neighbors(x, g, v);
    local_bk(g, std::move(r_next), std::move(p_next), std::move(x_next), out, t);

    p.erase(std::remove(p.begin(), p.end(), v), p.end());
    x.push_back(v);
  }
}

std::vector<Clique> RefinedIncrementalTopK::run(const TemporalGraphDataset& dataset,
                                                std::size_t k) {
  if (dataset.empty() || k == 0) return {};

  TopKManager topk(k);  // Line 1
  IncrementalEngine engine;

  const TimeId t_begin = dataset.min_time();
  const TimeId t_end = dataset.max_time();

  // Line 2: M_prev <- RMCE(G_0)
  CliqueContainer m_prev = engine.initialize(dataset.snapshot(t_begin), t_begin);

  // Line 3: initialize Q with cliques from M_prev
  m_prev.for_each([&](Clique& c) {
    c.set_interval_count(1);
    c.born_at = t_begin;
    c.last_seen_at = t_begin;
    topk.consider(c);
  });

  // Line 4: for t <- 1 to n
  for (TimeId t = t_begin + 1; t <= t_end; ++t) {
    const auto& prev_g = dataset.snapshot(t - 1);
    const auto& curr_g = dataset.snapshot(t);

    // Line 5-16: strict path via process_step
    IncrementalEngine::StepResult step = engine.process_step(curr_g, prev_g, m_prev, t);

    // Line 17-21: update I(c), compute F(c), and maintain Q
    for (const auto& c : step.p_emitted) {
      topk.consider(c);
    }

    step.m_curr.for_each([&](Clique& c) {
      const auto sig = c.signature();
      if (step.p_set.find(sig) == step.p_set.end()) {
        c.increment_interval_count(1);
        c.last_seen_at = t;
        step.p_set.insert(sig);
      }
      topk.consider(c);
    });

    // Line 22: M_prev <- M_curr
    m_prev = std::move(step.m_curr);
  }

  // Line 23: return SortDescending(Q)
  return topk.export_sorted();
}

}  // namespace temporal_topk

