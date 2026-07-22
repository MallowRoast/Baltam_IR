#pragma once

#include "pass/ir_pass_manager.h"

namespace baltam {

/**
 * @brief 通过 builtin 计算标量常量表达式。
 *
 * 当前阶段只要一元/二元运算符的操作数或 direct call 的实参都由 `ConstInst` 定义，
 * 就尝试调用对应 builtin 折叠。后续更精确的 MATLAB 分派安全规则应集中收敛在可折叠
 * 判断中，不改 builtin 调用和指令替换主流程。
 */
class ConstantFoldingPass final : public IRCodeUnitPass {
public:
    [[nodiscard]] std::string_view name() const noexcept override {
        return "constant-folding";
    }

    IRPassResult run(CodeUnit& unit, IRPassContext& context) override;
};

} // namespace baltam
