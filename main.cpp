#include <iostream>
#include <string>

#include "algorithm.h"

int main(int argc, char* argv[]) {
  using namespace temporal_topk;

  try {
    TemporalGraphDataset dataset;

    // 用法1：命令行传入数据文件（每行: time_id node_u node_v）
    if (argc > 1) {
      dataset.load_from_file(argv[1]);
      std::cout << "Loaded temporal graph from: " << argv[1] << "\n";
    } else {
      // 用法2：内置示例，便于在 VS2022 新建控制台项目后直接 F5 运行
      dataset.add_temporal_edge(0, 1, 2);
      dataset.add_temporal_edge(0, 1, 3);
      dataset.add_temporal_edge(0, 2, 3);

      dataset.add_temporal_edge(1, 1, 2);
      dataset.add_temporal_edge(1, 1, 3);
      dataset.add_temporal_edge(1, 2, 3);
      dataset.add_temporal_edge(1, 2, 4);
      dataset.add_temporal_edge(1, 3, 4);

      dataset.add_temporal_edge(2, 1, 2);
      dataset.add_temporal_edge(2, 2, 3);

      std::cout << "Using built-in demo temporal graph (no input file provided).\n";
    }

    constexpr std::size_t kTop = 5;
    auto result = RefinedIncrementalTopK::run(dataset, kTop);

    std::cout << "\nTop-" << kTop << " maximal cliques by score F(c)=I(c)*|c|:\n";
    for (std::size_t i = 0; i < result.size(); ++i) {
      const auto& c = result[i];
      std::cout << "[" << i + 1 << "] score=" << c.score()
                << ", I(c)=" << c.interval_count << ", |c|=" << c.size()
                << ", born=" << c.born_at << ", last_seen=" << c.last_seen_at
                << ", nodes={";
      for (std::size_t j = 0; j < c.vertices.size(); ++j) {
        if (j) std::cout << ',';
        std::cout << c.vertices[j];
      }
      std::cout << "}\n";
    }

    if (result.empty()) {
      std::cout << "No clique result found.\n";
    }

    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "Error: " << ex.what() << '\n';
    return 1;
  }
}

