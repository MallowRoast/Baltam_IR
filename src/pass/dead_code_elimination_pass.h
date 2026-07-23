#pragma once

#include "pass/ir_pass_manager.h"

namespace baltam {

/**
 * @brief 删除明确无用的指令。
 */
class DeadCodeEliminationPass final : public IRCodeUnitPass {
public:
    [[nodiscard]] std::string_view name() const noexcept override {
        return "dead-code-elimination";
    }

    IRPassResult run(CodeUnit& unit, IRPassContext& context) override;
};

} // namespace baltam
