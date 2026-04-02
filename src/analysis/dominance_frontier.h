#ifndef BALTAM_IR_ANALYSIS_DOMINANCE_FRONTIER_H
#define BALTAM_IR_ANALYSIS_DOMINANCE_FRONTIER_H

#include <unordered_map>
#include <vector>

#include "analysis/analysis_manager.h"

namespace baltam {
namespace analysis {

/**
 * @brief 基于 CFG 和支配树计算每个可达块的 dominance frontier。
 *
 * 当前实现只在入口可达子图上工作，不为不可达块建立 frontier。
 */
class DominanceFrontier {
public:
    /**
     * @brief DominanceFrontier 的结果对象。
     *
     * 当前结果只保留“块 -> frontier 块列表”这一个核心映射；
     * 后续 iterated frontier 或名字级 phi 插入结果不属于本 analysis。
     */
    struct Result {
        std::unordered_map<const BasicBlock*, std::vector<const BasicBlock*>> frontier;

        /**
         * @brief 返回某个块的 dominance frontier。
         *
         * 若块为空、不可达，或其 frontier 为空，则返回空列表。
         */
        const std::vector<const BasicBlock*>& frontier_of(const BasicBlock* block) const;
    };

    /**
     * @brief 在一个函数上运行 DominanceFrontier analysis。
     */
    Result run(Function& function, FunctionAnalysisManager& analysis_manager) const;
};

}  // namespace analysis
}  // namespace baltam

#endif
