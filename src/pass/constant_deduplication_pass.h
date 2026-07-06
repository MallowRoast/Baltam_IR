#pragma once

#include "pass/ir_pass_manager.h"

namespace baltam {

/**
 * @brief 合并同一 CodeUnit 内重复的常量定义，并把简单循环中的常量外提。
 *
 * 相同 `ConstInst::value` 只保留一个 canonical `ValueId`，后续使用重复常量结果的地方
 * 会被重写为引用 canonical value。重复 `ConstInst` 删除后，对应旧 `ValueInfo.def` 会被清空。
 * 对 lowering 生成的 `for.header` / `while.header` 自然循环，循环体内剩余的 `ConstInst`
 * 会被移动到唯一循环外前驱的 terminator 之前。
 */
class ConstantDeduplicationPass final : public IRCodeUnitPass {
public:
    [[nodiscard]] std::string_view name() const noexcept override {
        return "constant-deduplication";
    }

    IRPassResult run(CodeUnit& unit, IRPassContext& context) override;
};

} // namespace baltam
