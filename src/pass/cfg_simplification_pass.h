#pragma once

#include "pass/ir_pass_manager.h"

namespace baltam {

/**
 * @brief 简化基础 CFG 形状。
 *
 * 第一版覆盖 planning notes 中的保守规则：
 * - 清理过时 CFG 边。
 * - 删除不可达 block。
 * - 删除空跳转块并折叠连续跳转。
 * - 合并单前驱、单后继的线性 block。
 */
class CFGSimplificationPass final : public IRCodeUnitPass {
public:
    CFGSimplificationPass() noexcept = default;

    [[nodiscard]] std::string_view name() const noexcept override {
        return "cfg-simplification";
    }

    IRPassResult run(CodeUnit& unit, IRPassContext& context) override;
};

} // namespace baltam
