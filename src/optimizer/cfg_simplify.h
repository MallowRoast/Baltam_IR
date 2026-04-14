#ifndef BALTAM_IR_OPTIMIZER_CFG_SIMPLIFY_H
#define BALTAM_IR_OPTIMIZER_CFG_SIMPLIFY_H

#include "optimizer/pass.h"

namespace baltam {
namespace optimizer {

/**
 * @brief 在 untyped SSA 上做基础 CFG 简化。
 *
 * 当前版本支持两类基础简化：
 *
 * 1. 删除满足以下条件的跳板块：
 * - 不是入口块
 * - 没有 phi
 * - 没有正文指令
 * - 终结节点是 `SSAJumpNode`
 * - 只有一个前驱，且该前驱自身也以 `SSAJumpNode` 跳到该块
 *
 * 2. 合并满足以下条件的线性块：
 * - 前驱块以 `SSAJumpNode` 直接跳到后继
 * - 前驱只有这一个后继
 * - 后继只有这一个前驱
 * - 后继没有 phi
 *
 * 简化时会同步更新受影响后继块 phi 中的 incoming 前驱。
 */
class UntypedSSACFGSimplifyPass final : public FunctionPass {
public:
    const char* name() const override;

    analysis::PreservedAnalyses run(Function& function,
                                    analysis::FunctionAnalysisManager& analysis_manager) override;
};

}  // namespace optimizer
}  // namespace baltam

#endif
