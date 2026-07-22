#pragma once

#include "pass/ir_pass_manager.h"

namespace baltam {

/**
 * @brief 将条件已静态确定的分支改写为无条件跳转。
 */
class DeadBranchEliminationPass final : public IRCodeUnitPass {
public:
    [[nodiscard]] std::string_view name() const noexcept override {
        return "dead-branch-elimination";
    }

    IRPassResult run(CodeUnit& unit, IRPassContext& context) override;
};

} // namespace baltam
