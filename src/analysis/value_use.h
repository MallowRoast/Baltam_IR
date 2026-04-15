#ifndef BALTAM_IR_ANALYSIS_VALUE_USE_H
#define BALTAM_IR_ANALYSIS_VALUE_USE_H

#include <cstddef>
#include <unordered_map>
#include <vector>

#include "analysis/analysis_manager.h"

namespace baltam {
namespace analysis {

/**
 * @brief 统计 SSA `ValueId` 到所有 use 点的映射关系。
 *
 * 结果覆盖函数中的 phi、正文指令和终结节点里出现的所有 `ValueRef`。
 * 参数值或无效值不会出现在结果中。
 */
class ValueUseAnalysis {
public:
    struct Use {
        UntypedSSANode* user = nullptr;
        std::size_t index = 0;

        /**
         * @brief 若当前 use 仍引用 `old_value`，则把它改写为 `new_value`。
         *
         * `index` 的含义由 `user->type()` 决定：
         *
         * - `SSA_Phi` / `SSA_Return` / `SSA_Call`：第几个操作数
         * - `SSA_BinOp`：0 表示 lhs，1 表示 rhs
         * - 其他单操作数节点固定为 0
         */
        bool replace_with(ValueRef old_value, ValueRef new_value) const;
    };

    struct Result {
        std::unordered_map<ValueId, std::vector<Use>> uses_of_value;

        /**
         * @brief 返回某个 SSA 值的所有 use 点；若没有 use 则返回空列表。
         */
        const std::vector<Use>& uses_of(ValueId value_id) const;
    };

    /**
     * @brief 在 untyped SSA 函数上建立 `ValueId -> use sites` 索引。
     */
    Result run(Function& function, FunctionAnalysisManager& analysis_manager) const;
};

}  // namespace analysis
}  // namespace baltam

#endif
