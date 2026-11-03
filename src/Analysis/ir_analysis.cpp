#include "Analysis/ir_analysis.h"

#include <algorithm>
#include <utility>

namespace baltam {

CFGAnalysis::CFGAnalysis(const IRStageVersion& version) : entry_(version.entry_block()) {
    blocks_.reserve(version.blocks().size());
    for (const auto& block : version.blocks()) {
        indices_.emplace(block.get(), blocks_.size());
        blocks_.push_back({block.get(), {}, {}});
    }
    if (blocks_.empty()) return;

    for (BlockIndex source = 0; source < blocks_.size(); ++source) {
        const auto* terminator = blocks_[source].block->terminator();
        const auto add_edge = [this, source](const BasicBlock* target) {
            const auto destination = index(target);
            auto& successors = blocks_[source].successors;
            if (std::find(successors.begin(), successors.end(), destination) != successors.end())
                return;
            successors.push_back(destination);
            blocks_[destination].predecessors.push_back(source);
        };
        switch (terminator->type()) {
        case Instruction::Type::Goto:
            add_edge(static_cast<const GotoInst*>(terminator)->target);
            break;
        case Instruction::Type::Branch: {
            const auto& branch = static_cast<const BranchInst&>(*terminator);
            add_edge(branch.true_target);
            add_edge(branch.false_target);
            break;
        }
        case Instruction::Type::Return:
            break;
        default:
            break;
        }
    }
}

const BasicBlock* CFGAnalysis::entry() const noexcept {
    return entry_;
}

std::size_t CFGAnalysis::size() const noexcept {
    return blocks_.size();
}

CFGAnalysis::BlockIndex CFGAnalysis::index(const BasicBlock* block) const {
    const auto found = indices_.find(block);
    return found->second;
}

const BasicBlock* CFGAnalysis::block(BlockIndex index) const {
    return blocks_[index].block;
}

const std::vector<CFGAnalysis::BlockIndex>& CFGAnalysis::predecessors(BlockIndex index) const {
    return blocks_[index].predecessors;
}

const std::vector<CFGAnalysis::BlockIndex>& CFGAnalysis::successors(BlockIndex index) const {
    return blocks_[index].successors;
}

DominatorAnalysis::DominatorAnalysis(const CFGAnalysis& cfg) : blocks_(cfg.size()) {
    if (!cfg.entry())
        return;
    const auto entry = cfg.index(cfg.entry());

    // 显式栈生成后序，避免深层控制流消耗调用栈。
    std::vector<BlockIndex> order;
    std::vector<std::pair<BlockIndex, std::size_t>> stack{{entry, 0}};
    blocks_[entry].reachable = true;
    while (!stack.empty()) {
        auto& frame = stack.back();
        const auto& successors = cfg.successors(frame.first);
        if (frame.second == successors.size()) {
            order.push_back(frame.first);
            stack.pop_back();
            continue;
        }
        const auto next = successors[frame.second++];
        if (!blocks_[next].reachable) {
            blocks_[next].reachable = true;
            stack.emplace_back(next, 0);
        }
    }
    std::reverse(order.begin(), order.end());
    const auto unknown = cfg.size();
    std::vector<BlockIndex> rank(cfg.size(), unknown);
    std::vector<BlockIndex> idom(cfg.size(), unknown);
    for (std::size_t i = 0; i < order.size(); ++i)
        rank[order[i]] = i;
    idom[entry] = entry;

    const auto intersect = [&rank, &idom](BlockIndex a, BlockIndex b) {
        while (a != b) {
            while (rank[a] > rank[b])
                a = idom[a];
            while (rank[b] > rank[a])
                b = idom[b];
        }
        return a;
    };
    bool changed;
    do {
        changed = false;
        for (const auto block : order) {
            if (block == entry)
                continue;
            auto parent = unknown;
            for (const auto predecessor : cfg.predecessors(block)) {
                if (idom[predecessor] == unknown)
                    continue;
                parent = parent == unknown ? predecessor : intersect(parent, predecessor);
            }
            if (parent != idom[block]) {
                idom[block] = parent;
                changed = true;
            }
        }
    } while (changed);

    // 按块索引建树，使子节点顺序不依赖哈希表或 DFS 顺序。
    for (BlockIndex block = 0; block < cfg.size(); ++block) {
        if (!blocks_[block].reachable || block == entry)
            continue;
        blocks_[block].immediate_dominator = idom[block];
        blocks_[idom[block]].children.push_back(block);
    }
    std::size_t clock = 0;
    stack.emplace_back(entry, 0);
    blocks_[entry].tree_enter = clock++;
    while (!stack.empty()) {
        auto& frame = stack.back();
        const auto& children = blocks_[frame.first].children;
        if (frame.second == children.size()) {
            blocks_[frame.first].tree_exit = clock++;
            stack.pop_back();
            continue;
        }
        const auto next = children[frame.second++];
        blocks_[next].tree_enter = clock++;
        stack.emplace_back(next, 0);
    }

    for (const auto block : order) {
        const auto& predecessors = cfg.predecessors(block);
        const auto count =
            std::count_if(predecessors.begin(), predecessors.end(),
                          [this](BlockIndex predecessor) { return blocks_[predecessor].reachable; });
        if (count < 2)
            continue;
        for (auto runner : predecessors) {
            if (!blocks_[runner].reachable)
                continue;
            while (runner != idom[block]) {
                blocks_[runner].frontier.push_back(block);
                runner = idom[runner];
            }
        }
    }
    for (auto& block : blocks_) {
        auto& frontier = block.frontier;
        std::sort(frontier.begin(), frontier.end());
        frontier.erase(std::unique(frontier.begin(), frontier.end()), frontier.end());
    }
}

bool DominatorAnalysis::reachable(BlockIndex block) const {
    return blocks_[block].reachable;
}

std::optional<DominatorAnalysis::BlockIndex>
DominatorAnalysis::immediate_dominator(BlockIndex block) const {
    return blocks_[block].immediate_dominator;
}

const std::vector<DominatorAnalysis::BlockIndex>&
DominatorAnalysis::children(BlockIndex block) const {
    return blocks_[block].children;
}

const std::vector<DominatorAnalysis::BlockIndex>&
DominatorAnalysis::frontier(BlockIndex block) const {
    return blocks_[block].frontier;
}

bool DominatorAnalysis::dominates(BlockIndex dominator, BlockIndex block) const {
    const bool dominator_reachable = reachable(dominator);
    const bool block_reachable = reachable(block);
    if (!dominator_reachable || !block_reachable)
        return false;
    return blocks_[dominator].tree_enter <= blocks_[block].tree_enter &&
           blocks_[block].tree_exit <= blocks_[dominator].tree_exit;
}

} // baltam 命名空间
