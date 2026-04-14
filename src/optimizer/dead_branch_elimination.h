#ifndef BALTAM_IR_OPTIMIZER_DEAD_BRANCH_ELIMINATION_H
#define BALTAM_IR_OPTIMIZER_DEAD_BRANCH_ELIMINATION_H

#include "optimizer/pass.h"

namespace baltam {
namespace optimizer {

/**
 * @brief 在 untyped SSA 上删除由常量条件暴露出来的死分支。
 *
 * 当前版本只处理能静态证明为 `bool` 常量的 `SSA_CondJump`：
 *
 * - 把 `condbr` 改写成对应目标的 `jump`
 * - 删除因此变得不可达的基本块
 * - 清理受影响 phi 的 dead incoming
 * - 把单 incoming phi 降成 `SSA_Copy`
 *
 * 对 `NonSSA` 或其他阶段函数，该 pass 会直接 no-op。
 */
class UntypedSSADeadBranchEliminationPass final : public FunctionPass {
public:
    const char* name() const override;

    analysis::PreservedAnalyses run(Function& function,
                                    analysis::FunctionAnalysisManager& analysis_manager) override;
};

}  // namespace optimizer
}  // namespace baltam

#endif
