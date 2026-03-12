#pragma once

#include <cstddef>
#include <string>
#include <unordered_set>
#include <vector>

#include "graph.h"
#include "util.h"

namespace temporal_topk {

class BKMaximalCliqueEnumerator {
 public:
  static CliqueContainer run(const GraphSnapshot& g, TimeId t, int init_interval = 1);

 private:
  static std::vector<NodeId> intersect_with_neighbors(const std::vector<NodeId>& in,
                                                      const GraphSnapshot& g,
                                                      NodeId pivot);

  static void bron_kerbosch(const GraphSnapshot& g,
                            std::vector<NodeId> r,
                            std::vector<NodeId> p,
                            std::vector<NodeId> x,
                            CliqueContainer& out,
                            TimeId t,
                            int init_interval);
};

class TopKManager {
 public:
  explicit TopKManager(std::size_t k);

  void consider(const Clique& c);
  std::vector<Clique> export_sorted() const;

 private:
  std::size_t k_ = 0;
  std::unordered_map<std::string, Clique> best_by_signature_;
};

class IncrementalEngine {
 public:
  struct StepResult {
    CliqueContainer m_new;
    CliqueContainer m_curr;
    std::unordered_set<std::string> p_set;
  };

  CliqueContainer initialize(const GraphSnapshot& g0, TimeId t0 = 0) const;

  CliqueContainer inc_rmce(const GraphSnapshot& curr,
                           const std::unordered_set<Edge, EdgeHash>& added_edges,
                           TimeId t) const;

  StepResult process_step(const GraphSnapshot& curr,
                          const GraphSnapshot& prev,
                          const CliqueContainer& m_prev,
                          TimeId t) const;

  void process_clique_relationships(CliqueContainer& m_new,
                                    const CliqueContainer& m_prev,
                                    std::unordered_set<std::string>& p_set,
                                    TimeId t) const;

 private:
  static std::vector<NodeId> intersect_with_neighbors(const std::vector<NodeId>& in,
                                                      const GraphSnapshot& g,
                                                      NodeId pivot);

  static void local_bk(const GraphSnapshot& g,
                       std::vector<NodeId> r,
                       std::vector<NodeId> p,
                       std::vector<NodeId> x,
                       CliqueContainer& out,
                       TimeId t);
};

class RefinedIncrementalTopK {
 public:
  static std::vector<Clique> run(const TemporalGraphDataset& dataset, std::size_t k);
};

}  // namespace temporal_topk
