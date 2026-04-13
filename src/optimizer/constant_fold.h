#ifndef BALTAM_IR_OPTIMIZER_CONSTANT_FOLD_H
#define BALTAM_IR_OPTIMIZER_CONSTANT_FOLD_H

#include "optimizer/pass.h"

namespace baltam {
namespace optimizer {

/**
 * @brief 在 untyped SSA 上折叠可静态求值的数值常量表达式。
 *
 * 当前版本处理：
 *
 * - `SSA_Number`
 * - `SSA_Copy`
 * - `SSA_UnaryOp`
 * - `SSA_BinOp(Add/Subtract/Times/Multiply/LDivide/MLeftDivide/MRightDivide/RDivide)`
 *
 * 当一元或二元运算的输入可递归求值为常量时，该 pass 会把对应的
 * `SSA_UnaryOpNode` / `SSABinOpNode` 原地替换成同 `result` 的
 * `SSANumberNode`。
 *
 * 对 `NonSSA` 或其他阶段函数，该 pass 会直接 no-op。
 */
class UntypedSSAConstantFoldPass final : public FunctionPass {
public:
    const char* name() const override;

    analysis::PreservedAnalyses run(Function& function,
                                    analysis::FunctionAnalysisManager& analysis_manager) override;
};

}  // namespace optimizer
}  // namespace baltam

#endif
