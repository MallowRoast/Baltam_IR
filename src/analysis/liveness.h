#ifndef BALTAM_IR_ANALYSIS_LIVENESS_H
#define BALTAM_IR_ANALYSIS_LIVENESS_H

#include <string>
#include <unordered_map>
#include <vector>

#include "analysis/analysis_manager.h"

namespace baltam {
namespace analysis {

/**
 * @brief 基于名字级 def/use 关系计算块级 live-in / live-out 的 analysis。
 *
 * 该 analysis 依赖：
 *
 * - `CFGAnalysis`
 *
 * 当前分析域是 non-SSA IR 中出现的 `NamedValue.name`，并且只在入口可达子图
 * 上工作。
 */
class Liveness {
public:
    /**
     * @brief Liveness 的结果对象。
     *
     * `live_in` / `live_out` 只为可达块建表；结果中的名字按字典序排序，保证
     * 查询和测试时结果稳定。
     */
    struct Result {
        std::unordered_map<const BasicBlock*, std::vector<std::string>> live_in;
        std::unordered_map<const BasicBlock*, std::vector<std::string>> live_out;

        /**
         * @brief 返回某个块的 live-in 名字集合。
         *
         * 若块为空、不可达，或集合为空，则返回空列表。
         */
        const std::vector<std::string>& live_in_of(const BasicBlock* block) const;

        /**
         * @brief 返回某个块的 live-out 名字集合。
         *
         * 若块为空、不可达，或集合为空，则返回空列表。
         */
        const std::vector<std::string>& live_out_of(const BasicBlock* block) const;
    };

    /**
     * @brief 在一个函数上运行 Liveness analysis。
     */
    Result run(Function& function, FunctionAnalysisManager& analysis_manager) const;
};

}  // namespace analysis
}  // namespace baltam

#endif
