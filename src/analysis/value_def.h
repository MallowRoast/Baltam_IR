#ifndef BALTAM_IR_ANALYSIS_VALUE_DEF_H
#define BALTAM_IR_ANALYSIS_VALUE_DEF_H

#include <unordered_map>

#include "analysis/analysis_manager.h"

namespace baltam {
namespace analysis {

/**
 * @brief 统计 SSA `ValueId` 到定义节点的映射关系。
 *
 * 这个 analysis 和 `DefUse` 的边界不同：
 *
 * - `DefUse` 面向 non-SSA 名字文本，统计 defs / uses / def-blocks
 * - `ValueDefAnalysis` 面向 SSA `ValueId`，只回答“这个值是谁定义的”
 *
 * 因为 SSA 里每个值至多定义一次，所以结果是一张单值映射表，而不是定义列表。
 * 参数值或未定义的值没有对应节点时，查询结果返回 `nullptr`。
 *
 * 当前结果覆盖整个函数里的所有 block，不按入口可达性过滤。
 */
class ValueDefAnalysis {
public:
    struct Result {
        std::unordered_map<ValueId, const UntypedSSANode*> defs_of_value;

        /**
         * @brief 返回某个 SSA 值对应的定义节点；若没有定义则返回 `nullptr`。
         */
        const UntypedSSANode* definition_of(ValueId value_id) const;
    };

    /**
     * @brief 在 untyped SSA 函数上建立 `ValueId -> def node` 索引。
     */
    Result run(Function& function, FunctionAnalysisManager& analysis_manager) const;
};

}  // namespace analysis
}  // namespace baltam

#endif
