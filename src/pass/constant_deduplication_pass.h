#pragma once

#include "pass/ir_pass_manager.h"

namespace baltam {

/**
 * @brief 合并同一 CodeUnit 内重复的常量定义。
 *
 * 相同 `ConstInst::value` 只保留一个 canonical `ValueId`，后续使用重复常量结果的地方
 * 会被重写为引用 canonical value。重复 `ConstInst` 删除后，对应旧 `ValueInfo.def` 会被清空。
 */
class ConstantDeduplicationPass final : public IRCodeUnitPass {
public:
    [[nodiscard]] std::string_view name() const noexcept override {
        return "constant-deduplication";
    }

    IRPassResult run(CodeUnit& unit, IRPassContext& context) override;
};

} // namespace baltam
