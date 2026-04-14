#ifndef BALTAM_IR_OPTIMIZER_COPY_PROPAGATION_H
#define BALTAM_IR_OPTIMIZER_COPY_PROPAGATION_H

#include "optimizer/pass.h"

namespace baltam {
namespace optimizer {

/**
 * @brief 在 untyped SSA 上做基于 SSA_Copy 的复制传播。
 *
 * 当前版本会：
 *
 * - 把 use 端看到的 copy 链折叠到最终非 copy 源值
 * - 删除同名的冗余 copy，例如 `%x.3 = copy %x.2`
 *
 * 该 pass 不直接做一般常量折叠；新暴露出来的死 copy / 死定义交给后续 DCE。
 */
class UntypedSSACopyPropagationPass final : public FunctionPass {
public:
    const char* name() const override;

    analysis::PreservedAnalyses run(Function& function,
                                    analysis::FunctionAnalysisManager& analysis_manager) override;
};

}  // namespace optimizer
}  // namespace baltam

#endif
