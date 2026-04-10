#ifndef BALTAM_IR_ANALYSIS_CFG_ANALYSIS_H
#define BALTAM_IR_ANALYSIS_CFG_ANALYSIS_H

#include <cstddef>
#include <unordered_map>
#include <vector>

#include "analysis/analysis_manager.h"

namespace baltam {
namespace analysis {

/**
 * @brief 从当前显式 CFG 中提取标准遍历顺序和 RPO 索引的函数级 analysis。
 *
 * 该 analysis 假定输入函数已经通过结构 verifier：
 *
 * - CFG 边关系自洽
 * - entry block 存在且属于当前函数
 *
 * 它不重建 CFG，也不修复 CFG，只在现有 `successors()` 关系上计算：
 *
 * - `postorder`
 * - `reverse_postorder`
 * - `rpo_index`
 */
class CFGAnalysis {
public:
    /**
     * @brief CFGAnalysis 的结果对象。
     *
     * 当前采用“平衡版”接口：
     *
     * - `postorder` 供反向分析直接使用
     * - `reverse_postorder` 供前向分析和 dominator 类 analysis 使用
     * - `rpo_index` 提供块到 RPO 位置的快速索引
     *
     * 这三项已经足以表达“可达块集合 + 标准遍历顺序”；其余信息都可以从
     * 这三项和 `Function` 本身间接得到，因此不再单独缓存。
     */
    struct Result {
        std::vector<const BasicBlock*> postorder;
        std::vector<const BasicBlock*> reverse_postorder;
        std::unordered_map<const BasicBlock*, std::size_t> rpo_index;

        /**
         * @brief 判断某个块是否位于入口可达子图中。
         *
         * 当前实现直接用 `rpo_index` 判断，因为只有可达块才会被写入该映射。
         */
        bool is_reachable(const BasicBlock* block) const;
    };

    /**
     * @brief 在一个函数上运行 CFGAnalysis。
     *
     * @throws std::runtime_error 当函数缺少入口块，或入口块不属于当前函数时抛出。
     */
    Result run(Function& function, FunctionAnalysisManager& analysis_manager) const;
};

}  // namespace analysis
}  // namespace baltam

#endif
