#pragma once

#include "ir/ir_units.h"

#include <cstdint>
#include <vector>

namespace baltam {

/**
 * @brief IR verifier 诊断信息。
 */
struct IRVerifyDiagnostic {
    /**
     * @brief 诊断级别。
     */
    enum Severity : std::uint8_t {
        Warning,
        Error,
    };

    Severity severity = Error;
    InternedString message;
    SourceSpan source_span;
};

/**
 * @brief IR verifier 选项。
 */
struct IRVerifyOptions {
    /**
     * @brief 是否要求每个基本块都以终结指令结束。
     *
     * 当前 lowering 已经按 closed block 生成 IR，因此默认开启。后续如果需要验证构建中间态，
     * 可以关闭该选项。
     */
    bool require_terminated_blocks = true;

    /**
     * @brief 是否要求 `SlotId` 按当前 builder 的 unit-local 递增规则排列。
     */
    bool require_dense_slot_ids = false;
};

/**
 * @brief IR verifier 返回结果。
 */
struct IRVerifyResult {
    std::vector<IRVerifyDiagnostic> diagnostics;

    /**
     * @brief 判断 verifier 是否没有发现 error 级诊断。
     */
    [[nodiscard]] bool ok() const noexcept;
};

/**
 * @brief 验证整个文件级 IR 单元。
 */
[[nodiscard]] IRVerifyResult verify_ir(
    const MFileUnit& mfile,
    const IRVerifyOptions& options = {});

} // namespace baltam
