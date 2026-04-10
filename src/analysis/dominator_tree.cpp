#include "analysis/dominator_tree.h"

#include <stdexcept>
#include <string>

#include "analysis/cfg_analysis.h"

namespace baltam {
namespace analysis {
namespace {

const BasicBlock* intersect(const BasicBlock* lhs, const BasicBlock* rhs,
                            const std::unordered_map<const BasicBlock*, const BasicBlock*>& idom,
                            const std::unordered_map<const BasicBlock*, std::size_t>& rpo_index) {
    const BasicBlock* finger1 = lhs;
    const BasicBlock* finger2 = rhs;

    while (finger1 != finger2) {
        while (rpo_index.at(finger1) > rpo_index.at(finger2)) {
            finger1 = idom.at(finger1);
        }
        while (rpo_index.at(finger2) > rpo_index.at(finger1)) {
            finger2 = idom.at(finger2);
        }
    }

    return finger1;
}

}  // namespace

const BasicBlock* DominatorTree::Result::immediate_dominator(const BasicBlock* block) const {
    if (block == nullptr) {
        return nullptr;
    }

    auto it = idom.find(block);
    if (it == idom.end()) {
        return nullptr;
    }
    return it->second;
}

const std::vector<const BasicBlock*>& DominatorTree::Result::children_of(
    const BasicBlock* block) const {
    static const std::vector<const BasicBlock*> empty_children;

    if (block == nullptr) {
        return empty_children;
    }

    auto it = children.find(block);
    if (it == children.end()) {
        return empty_children;
    }
    return it->second;
}

bool DominatorTree::Result::dominates(const BasicBlock* dominator, const BasicBlock* block) const {
    if (dominator == nullptr || block == nullptr) {
        return false;
    }
    if (idom.find(dominator) == idom.end() || idom.find(block) == idom.end()) {
        return false;
    }

    for (const BasicBlock* finger = block; finger != nullptr; finger = immediate_dominator(finger)) {
        if (finger == dominator) {
            return true;
        }
    }

    return false;
}

bool DominatorTree::Result::strictly_dominates(const BasicBlock* dominator,
                                               const BasicBlock* block) const {
    return dominator != block && dominates(dominator, block);
}

DominatorTree::Result DominatorTree::run(Function& function,
                                         FunctionAnalysisManager& analysis_manager) const {
    const CFGAnalysis::Result& cfg = analysis_manager.get<CFGAnalysis>(function);

    Result result;
    if (cfg.reverse_postorder.empty()) {
        return result;
    }

    const BasicBlock* entry = cfg.reverse_postorder.front();
    result.idom.emplace(entry, nullptr);

    bool changed = true;
    while (changed) {
        changed = false;

        for (std::size_t i = 1; i < cfg.reverse_postorder.size(); ++i) {
            const BasicBlock* block = cfg.reverse_postorder[i];
            const BasicBlock* new_idom = nullptr;

            for (BasicBlock* predecessor : block->predecessors()) {
                if (!cfg.is_reachable(predecessor)) {
                    continue;
                }

                if (result.idom.find(predecessor) == result.idom.end()) {
                    continue;
                }

                if (new_idom == nullptr) {
                    new_idom = predecessor;
                } else {
                    new_idom = intersect(predecessor, new_idom, result.idom, cfg.rpo_index);
                }
            }

            if (new_idom == nullptr) {
                throw std::runtime_error("DominatorTree 失败：函数 `" + function.name() +
                                         "` 的可达基本块 `" + block->name() +
                                         "` 没有可用的支配前驱。");
            }

            auto it = result.idom.find(block);
            if (it == result.idom.end()) {
                result.idom.emplace(block, new_idom);
                changed = true;
            } else if (it->second != new_idom) {
                it->second = new_idom;
                changed = true;
            }
        }
    }

    for (const BasicBlock* block : cfg.reverse_postorder) {
        result.children.emplace(block, std::vector<const BasicBlock*>{});
    }

    for (std::size_t i = 1; i < cfg.reverse_postorder.size(); ++i) {
        const BasicBlock* block = cfg.reverse_postorder[i];
        const BasicBlock* parent = result.idom.at(block);
        if (parent == nullptr) {
            throw std::runtime_error("DominatorTree 失败：函数 `" + function.name() +
                                     "` 的可达基本块 `" + block->name() +
                                     "` 缺少 immediate dominator。");
        }
        result.children[parent].push_back(block);
    }

    return result;
}

}  // namespace analysis
}  // namespace baltam
