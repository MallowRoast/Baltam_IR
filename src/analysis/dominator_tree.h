#ifndef BALTAM_IR_ANALYSIS_DOMINATOR_TREE_H
#define BALTAM_IR_ANALYSIS_DOMINATOR_TREE_H

#include <unordered_map>
#include <vector>

#include "analysis/analysis_manager.h"

namespace baltam {
namespace analysis {

/**
 * @brief 基于当前函数显式 CFG 计算 immediate dominator 关系的 analysis。
 *
 * 该 analysis 依赖：
 *
 * - `CFGAnalysis`
 *
 * 它只在入口可达子图上工作，不为不可达块建立支配关系。
 */
class DominatorTree {
public:
    /**
     * @brief DominatorTree 的结果对象。
     *
     * 输出包括：
     *
     * - `idom`：块到其 immediate dominator 的映射
     * - `children`：支配树上的孩子列表
     *
     * 其中入口块会出现在 `idom` 中，但其 `idom` 值为 `nullptr`。
     */
    struct Result {
        std::unordered_map<const BasicBlock*, const BasicBlock*> idom;
        std::unordered_map<const BasicBlock*, std::vector<const BasicBlock*>> children;

        /**
         * @brief 返回某个块的 immediate dominator。
         *
         * 若块不可达、为空，或块本身就是入口块，则返回 `nullptr`。
         */
        const BasicBlock* immediate_dominator(const BasicBlock* block) const;

        /**
         * @brief 返回某个块在支配树上的直接孩子列表。
         *
         * 若块不可达或没有孩子，返回空列表。
         */
        const std::vector<const BasicBlock*>& children_of(const BasicBlock* block) const;

        /**
         * @brief 判断 `dominator` 是否支配 `block`。
         *
         * 约定：
         *
         * - 不可达块不参与支配关系，返回 `false`
         * - 可达块总是支配自己
         */
        bool dominates(const BasicBlock* dominator, const BasicBlock* block) const;

        /**
         * @brief 判断 `dominator` 是否严格支配 `block`。
         */
        bool strictly_dominates(const BasicBlock* dominator, const BasicBlock* block) const;
    };

    /**
     * @brief 在一个函数上运行 DominatorTree analysis。
     *
     * @throws std::runtime_error 当可达块的 immediate dominator 无法确定时抛出。
     */
    Result run(Function& function, FunctionAnalysisManager& analysis_manager) const;
};

}  // namespace analysis
}  // namespace baltam

#endif
