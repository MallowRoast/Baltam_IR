#pragma once

#include "pass/ir_pass_manager.h"

namespace baltam {

/**
 * @brief 删除入口块不可达的基本块。
 *
 * 该 pass 只依赖 `BasicBlock::successors` 表达的 CFG。删除 block 后会同步清理剩余 block 的
 * predecessor 边，并清空被删除指令在 `ValueTable` 中的 def 指针。
 */
class UnreachableBlockEliminationPass final : public IRCodeUnitPass {
public:
    [[nodiscard]] std::string_view name() const noexcept override {
        return "unreachable-block-elimination";
    }

    IRPassResult run(CodeUnit& unit, IRPassContext& context) override;
};

} // namespace baltam
