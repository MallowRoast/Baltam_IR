#ifndef BALTAM_IR_OPTIMIZER_DCE_H
#define BALTAM_IR_OPTIMIZER_DCE_H

#include "optimizer/pass.h"

namespace baltam {
namespace optimizer {

/**
 * @brief 在 untyped SSA 上删除无 uses 且无副作用的死节点。
 *
 * 当前版本只删除确定纯、且结果不再被任何节点使用的 SSA 节点：
 *
 * - `SSAPhiNode`
 * - `SSANumberNode`
 * - `SSATextNode`
 * - `SSAUndefNode`
 * - `SSACopyNode`
 * - `SSAUnaryOpNode`
 * - `SSABinOpNode`
 *
 * 第一版明确不删除：
 *
 * - `SSAGlobalLoadNode`
 * - `SSAGlobalStoreNode`
 * - `SSACallNode`
 * - 所有 terminator
 *
 * 对 `NonSSA` 或其他阶段函数，该 pass 会直接 no-op。
 */
class UntypedSSADCEPass final : public FunctionPass {
public:
    const char* name() const override;

    analysis::PreservedAnalyses run(Function& function,
                                    analysis::FunctionAnalysisManager& analysis_manager) override;
};

}  // namespace optimizer
}  // namespace baltam

#endif
