#include "analysis/dominance_frontier.h"

#include <algorithm>
#include <unordered_set>

#include "analysis/cfg_analysis.h"
#include "analysis/dominator_tree.h"

namespace baltam {
namespace analysis {

const std::vector<const BasicBlock*>& DominanceFrontier::Result::frontier_of(
    const BasicBlock* block) const {
    static const std::vector<const BasicBlock*> empty_frontier;

    if (block == nullptr) {
        return empty_frontier;
    }

    auto it = frontier.find(block);
    if (it == frontier.end()) {
        return empty_frontier;
    }
    return it->second;
}

DominanceFrontier::Result DominanceFrontier::run(
    Function& function, FunctionAnalysisManager& analysis_manager) const {
    const CFGAnalysis::Result& cfg = analysis_manager.get<CFGAnalysis>(function);
    const DominatorTree::Result& dom = analysis_manager.get<DominatorTree>(function);
    (void)function;

    Result result;
    std::unordered_map<const BasicBlock*, std::unordered_set<const BasicBlock*>> frontier_sets;

    for (const BasicBlock* block : cfg.reverse_postorder) {
        result.frontier.emplace(block, std::vector<const BasicBlock*>{});
        frontier_sets.emplace(block, std::unordered_set<const BasicBlock*>{});
    }

    // 对每个合流块，从其可达前驱沿 idom 链向上爬，把该合流块记录到
    // 各个 runner 的 frontier 中。
    for (const BasicBlock* block : cfg.reverse_postorder) {
        std::vector<const BasicBlock*> reachable_predecessors;
        for (BasicBlock* predecessor : block->predecessors()) {
            if (cfg.is_reachable(predecessor)) {
                reachable_predecessors.push_back(predecessor);
            }
        }

        if (reachable_predecessors.size() < 2) {
            continue;
        }

        const BasicBlock* block_idom = dom.immediate_dominator(block);
        for (const BasicBlock* predecessor : reachable_predecessors) {
            const BasicBlock* runner = predecessor;
            while (runner != nullptr && runner != block_idom) {
                frontier_sets[runner].insert(block);
                runner = dom.immediate_dominator(runner);
            }
        }
    }

    for (auto& entry : result.frontier) {
        auto frontier_it = frontier_sets.find(entry.first);
        if (frontier_it == frontier_sets.end()) {
            continue;
        }

        std::vector<const BasicBlock*>& blocks = entry.second;
        blocks.assign(frontier_it->second.begin(), frontier_it->second.end());
        std::sort(blocks.begin(), blocks.end(),
                  [&](const BasicBlock* lhs, const BasicBlock* rhs) {
                      return cfg.rpo_index.at(lhs) < cfg.rpo_index.at(rhs);
                  });
    }

    return result;
}

}  // namespace analysis
}  // namespace baltam
