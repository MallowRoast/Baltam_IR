#pragma once

#include "pass/ir_pass_manager.h"

namespace baltam {

/**
 * @brief 转发同一 basic block 内可直接复用的 load_slot。
 *
 * 当某个 slot 已经被写入且中间没有破坏该 slot 观测值的 barrier 时，后续 load_slot 会被
 * 替换成前面的写入值，并删除对应的 load 指令。
 */
class LoadForwardingPass final : public IRCodeUnitPass {
public:
    [[nodiscard]] std::string_view name() const noexcept override {
        return "load-forwarding";
    }

    IRPassResult run(CodeUnit& unit, IRPassContext& context) override;
};

} // namespace baltam
