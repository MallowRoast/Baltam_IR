#include "analysis/cfg_analysis.h"

#include <stdexcept>
#include <string>
#include <unordered_set>

namespace baltam {
namespace analysis {
namespace {

void build_postorder(const BasicBlock* block,
                     const std::unordered_set<const BasicBlock*>& known_blocks,
                     std::unordered_set<const BasicBlock*>& visited,
                     std::vector<const BasicBlock*>& postorder) {
    if (block == nullptr || known_blocks.find(block) == known_blocks.end()) {
        return;
    }
    if (!visited.insert(block).second) {
        return;
    }

    for (BasicBlock* successor : block->successors()) {
        build_postorder(successor, known_blocks, visited, postorder);
    }

    postorder.push_back(block);
}

}  // namespace

bool CFGAnalysis::Result::is_reachable(const BasicBlock* block) const {
    if (block == nullptr) {
        return false;
    }

    return rpo_index.find(block) != rpo_index.end();
}

CFGAnalysis::Result CFGAnalysis::run(Function& function,
                                     FunctionAnalysisManager& analysis_manager) const {
    (void)analysis_manager;

    Result result;

    const BasicBlock* entry = function.entry_block();
    if (entry == nullptr) {
        throw std::runtime_error("CFGAnalysis 失败：函数 `" + function.name() +
                                 "` 缺少入口基本块。");
    }

    std::unordered_set<const BasicBlock*> known_blocks;
    for (const auto& block : function.blocks()) {
        if (block == nullptr) {
            continue;
        }
        known_blocks.insert(block.get());
    }

    if (known_blocks.find(entry) == known_blocks.end()) {
        throw std::runtime_error("CFGAnalysis 失败：函数 `" + function.name() +
                                 "` 的入口基本块不属于当前函数。");
    }

    std::unordered_set<const BasicBlock*> visited;
    build_postorder(entry, known_blocks, visited, result.postorder);

    result.reverse_postorder.assign(result.postorder.rbegin(), result.postorder.rend());
    for (std::size_t i = 0; i < result.reverse_postorder.size(); ++i) {
        result.rpo_index.emplace(result.reverse_postorder[i], i);
    }

    return result;
}

}  // namespace analysis
}  // namespace baltam
