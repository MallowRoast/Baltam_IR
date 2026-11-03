#pragma once

#include "ir/ir_unit.h"

#include <cstddef>
#include <optional>
#include <unordered_map>
#include <vector>

namespace baltam {

// 输入由 Verifier 保证结构合法，查询参数由调用方保证有效。
// 当前阶段版本的控制流快照；调用方保证基本块存活，控制流变化后重新构建。
class CFGAnalysis final {
public:
    using BlockIndex = std::size_t;

    explicit CFGAnalysis(const IRStageVersion& version);

    [[nodiscard]] const BasicBlock* entry() const noexcept;

    [[nodiscard]] std::size_t size() const noexcept;

    [[nodiscard]] BlockIndex index(const BasicBlock* block) const;

    [[nodiscard]] const BasicBlock* block(BlockIndex index) const;

    [[nodiscard]] const std::vector<BlockIndex>& predecessors(BlockIndex index) const;

    [[nodiscard]] const std::vector<BlockIndex>& successors(BlockIndex index) const;

private:
    struct BlockInfo {
        const BasicBlock* block;
        std::vector<BlockIndex> predecessors;
        std::vector<BlockIndex> successors;
    };

    const BasicBlock* entry_ = nullptr;
    std::vector<BlockInfo> blocks_;
    std::unordered_map<const BasicBlock*, BlockIndex> indices_;
};

// 输入 CFG 的入口无前驱等结构约束由 Verifier 保证。
// 仅分析入口可达区域，结果沿用 CFG 的块索引，不持有基本块或 CFG 的所有权。
class DominatorAnalysis final {
public:
    using BlockIndex = CFGAnalysis::BlockIndex;

    explicit DominatorAnalysis(const CFGAnalysis& cfg);

    [[nodiscard]] bool reachable(BlockIndex block) const;

    [[nodiscard]] std::optional<BlockIndex> immediate_dominator(BlockIndex block) const;

    [[nodiscard]] const std::vector<BlockIndex>& children(BlockIndex block) const;

    [[nodiscard]] const std::vector<BlockIndex>& frontier(BlockIndex block) const;

    [[nodiscard]] bool dominates(BlockIndex dominator, BlockIndex block) const;

private:
    struct BlockInfo {
        bool reachable = false;
        std::optional<BlockIndex> immediate_dominator;
        std::vector<BlockIndex> children;
        std::vector<BlockIndex> frontier;
        std::size_t tree_enter = 0;
        std::size_t tree_exit = 0;
    };

    std::vector<BlockInfo> blocks_;
};

} // baltam 命名空间
