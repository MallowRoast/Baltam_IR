#pragma once

#include "ir/ir_units.h"
#include "ir/ir_verify.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace baltam {

/**
 * @brief IR pass 的调度作用域。
 */
enum class IRPassScope : std::uint8_t {
    File,
    CodeUnit,
};

/**
 * @brief 返回 pass 作用域的稳定文本名。
 */
[[nodiscard]] std::string_view ir_pass_scope_name(IRPassScope scope) noexcept;

/**
 * @brief IR pass 诊断信息。
 */
struct IRPassDiagnostic {
    /**
     * @brief 诊断级别。
     */
    enum Severity : std::uint8_t {
        Log,
        Warning,
        Error,
    };

    Severity severity = Error;
    InternedString pass_name;
    InternedString message;
    SourceSpan source_span;
};

/**
 * @brief 单次 pass invocation 的返回结果。
 */
struct IRPassResult {
    bool changed = false;
    std::vector<IRPassDiagnostic> diagnostics;

    /**
     * @brief 判断该结果是否没有 error 级诊断。
     */
    [[nodiscard]] bool ok() const noexcept;

    /**
     * @brief 追加一条 pass 诊断。
     */
    void report(
        IRPassDiagnostic::Severity severity,
        std::string_view message,
        SourceSpan source_span = SourceSpan::invalid());
};

/**
 * @brief 当前 pass invocation 的上下文。
 */
struct IRPassContext {
    MFileUnit* file = nullptr;
    CodeUnit* unit = nullptr;
};

/**
 * @brief IR pass 基类。
 */
class IRPass {
public:
    virtual ~IRPass() = default;

    /**
     * @brief pass 的稳定名字，用于诊断和统计。
     */
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;

    /**
     * @brief pass 的调度作用域。
     */
    [[nodiscard]] virtual IRPassScope scope() const noexcept = 0;
};

/**
 * @brief 运行在单个文件级 IR 单元上的 pass。
 */
class IRFilePass : public IRPass {
public:
    [[nodiscard]] IRPassScope scope() const noexcept final {
        return IRPassScope::File;
    }

    virtual IRPassResult run(MFileUnit& mfile, IRPassContext& context) = 0;
};

/**
 * @brief 运行在单个可执行代码单元上的 pass。
 */
class IRCodeUnitPass : public IRPass {
public:
    [[nodiscard]] IRPassScope scope() const noexcept final {
        return IRPassScope::CodeUnit;
    }

    virtual IRPassResult run(CodeUnit& unit, IRPassContext& context) = 0;
};

/**
 * @brief PassManager 运行选项。
 */
struct IRPassManagerOptions {
    bool verify_before_pipeline = false;
    bool verify_after_each_pass = false;
    bool verify_after_pipeline = false;
    bool stop_on_error = true;
    IRVerifyOptions verify_options;
};

/**
 * @brief 单个 pass 在整条 pipeline 中的运行统计。
 */
struct IRPassRunSummary {
    InternedString pass_name;
    IRPassScope scope = IRPassScope::File;
    std::size_t invocations = 0;
    std::size_t changed_invocations = 0;
    bool changed = false;
};

/**
 * @brief PassManager 最终返回结果。
 */
struct IRPassManagerResult {
    bool changed = false;
    std::vector<IRPassDiagnostic> diagnostics;
    std::vector<IRPassRunSummary> pass_runs;

    /**
     * @brief 判断整条 pipeline 是否没有 error 级诊断。
     */
    [[nodiscard]] bool ok() const noexcept;
};

/**
 * @brief IR pass 调度器。
 */
class IRPassManager final {
public:
    explicit IRPassManager(IRPassManagerOptions options = {});

    IRPassManager(const IRPassManager&) = delete;
    IRPassManager& operator=(const IRPassManager&) = delete;

    IRPassManager(IRPassManager&&) noexcept = default;
    IRPassManager& operator=(IRPassManager&&) noexcept = default;

    /**
     * @brief 追加一个已构造的 pass。
     */
    void add_pass(std::unique_ptr<IRPass> pass);

    /**
     * @brief 原地构造并追加一个 pass。
     */
    template <typename PassT, typename... Args>
    PassT& add_pass(Args&&... args) {
        static_assert(std::is_base_of_v<IRPass, PassT>, "PassT must derive from IRPass");

        auto pass = std::make_unique<PassT>(std::forward<Args>(args)...);
        PassT& pass_ref = *pass;
        add_pass(std::move(pass));
        return pass_ref;
    }

    /**
     * @brief 判断当前 manager 是否没有注册 pass。
     */
    [[nodiscard]] bool empty() const noexcept {
        return passes_.empty();
    }

    /**
     * @brief 返回已注册 pass 数量。
     */
    [[nodiscard]] std::size_t size() const noexcept {
        return passes_.size();
    }

    /**
     * @brief 在单个文件级 IR 单元上运行已注册 pass pipeline。
     */
    [[nodiscard]] IRPassManagerResult run(MFileUnit& mfile);

private:
    IRPassManagerOptions options_;
    std::vector<std::unique_ptr<IRPass>> passes_;
};

} // namespace baltam
