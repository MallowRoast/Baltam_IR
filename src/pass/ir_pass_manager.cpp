#include "pass/ir_pass_manager.h"

#include <algorithm>
#include <string>
#include <utility>

namespace baltam {
namespace {

constexpr std::string_view kPassManagerName = "ir-pass-manager";
constexpr std::string_view kVerifierName = "ir-verifier";

[[nodiscard]] bool has_error(const std::vector<IRPassDiagnostic>& diagnostics) noexcept {
    return std::any_of(
        diagnostics.begin(),
        diagnostics.end(),
        [](const IRPassDiagnostic& diagnostic) {
            return diagnostic.severity == IRPassDiagnostic::Error;
        });
}

void append_diagnostic(
    IRPassManagerResult& result,
    std::string_view pass_name,
    IRPassDiagnostic diagnostic) {
    if (diagnostic.pass_name.empty()) {
        diagnostic.pass_name = std::string(pass_name);
    }
    result.diagnostics.push_back(std::move(diagnostic));
}

void report_manager_error(
    IRPassManagerResult& result,
    std::string_view message,
    SourceSpan source_span = SourceSpan::invalid()) {
    result.diagnostics.push_back({
        IRPassDiagnostic::Error,
        std::string(kPassManagerName),
        std::string(message),
        source_span,
    });
}

void append_pass_result(
    IRPassManagerResult& manager_result,
    IRPassRunSummary& summary,
    std::string_view pass_name,
    IRPassResult pass_result) {
    if (pass_result.changed) {
        manager_result.changed = true;
        summary.changed = true;
        ++summary.changed_invocations;
    }

    for (IRPassDiagnostic& diagnostic : pass_result.diagnostics) {
        append_diagnostic(manager_result, pass_name, std::move(diagnostic));
    }
}

[[nodiscard]] IRPassDiagnostic::Severity map_verify_severity(
    IRVerifyDiagnostic::Severity severity) noexcept {
    switch (severity) {
        case IRVerifyDiagnostic::Warning:
            return IRPassDiagnostic::Warning;
        case IRVerifyDiagnostic::Error:
            return IRPassDiagnostic::Error;
    }
    return IRPassDiagnostic::Error;
}

void append_verify_result(
    IRPassManagerResult& result,
    std::string_view phase,
    IRVerifyResult verify_result) {
    for (const IRVerifyDiagnostic& diagnostic : verify_result.diagnostics) {
        result.diagnostics.push_back({
            map_verify_severity(diagnostic.severity),
            std::string(kVerifierName),
            std::string(phase) + ": " + diagnostic.message,
            diagnostic.source_span,
        });
    }
}

[[nodiscard]] bool should_stop(
    const IRPassManagerResult& result,
    const IRPassManagerOptions& options) noexcept {
    return options.stop_on_error && !result.ok();
}

[[nodiscard]] MFileUnit* source_file_for(CodeUnit& unit) noexcept {
    if (unit.is_script()) {
        return static_cast<ScriptUnit&>(unit).file;
    }

    if (unit.is_function()) {
        return static_cast<FunctionUnit&>(unit).file;
    }

    if (unit.is_anonymous_function()) {
        auto& function = static_cast<AnonymousFunctionUnit&>(unit);
        return function.lexical_parent != nullptr
            ? source_file_for(*function.lexical_parent)
            : nullptr;
    }

    return nullptr;
}

void run_module_pass(
    IRModulePass& pass,
    IRModule& module,
    IRPassManagerResult& result,
    IRPassRunSummary& summary) {
    IRPassContext context;
    context.module = &module;

    ++summary.invocations;
    append_pass_result(result, summary, pass.name(), pass.run(module, context));
}

void run_file_pass(
    IRFilePass& pass,
    IRModule& module,
    const IRPassManagerOptions& options,
    IRPassManagerResult& result,
    IRPassRunSummary& summary) {
    for (const auto& file_ptr : module.files) {
        if (file_ptr == nullptr) {
            report_manager_error(result, "IR module contains a null file unit");
            if (should_stop(result, options)) {
                return;
            }
            continue;
        }

        IRPassContext context;
        context.module = &module;
        context.file = file_ptr.get();

        ++summary.invocations;
        append_pass_result(result, summary, pass.name(), pass.run(*file_ptr, context));
        if (should_stop(result, options)) {
            return;
        }
    }
}

void run_code_unit_invocation(
    IRCodeUnitPass& pass,
    IRModule& module,
    MFileUnit* file,
    CodeUnit& unit,
    IRPassManagerResult& result,
    IRPassRunSummary& summary) {
    IRPassContext context;
    context.module = &module;
    context.file = file;
    context.unit = &unit;

    ++summary.invocations;
    append_pass_result(result, summary, pass.name(), pass.run(unit, context));
}

void run_code_unit_pass(
    IRCodeUnitPass& pass,
    IRModule& module,
    const IRPassManagerOptions& options,
    IRPassManagerResult& result,
    IRPassRunSummary& summary) {
    for (const auto& file_ptr : module.files) {
        if (file_ptr == nullptr) {
            report_manager_error(result, "IR module contains a null file unit");
            if (should_stop(result, options)) {
                return;
            }
            continue;
        }

        for (const auto& unit_ptr : file_ptr->code_units) {
            if (unit_ptr == nullptr) {
                report_manager_error(result, "IR file contains a null code unit");
                if (should_stop(result, options)) {
                    return;
                }
                continue;
            }

            run_code_unit_invocation(
                pass,
                module,
                file_ptr.get(),
                *unit_ptr,
                result,
                summary);
            if (should_stop(result, options)) {
                return;
            }
        }
    }

    for (const auto& function_ptr : module.anonymous_functions.functions) {
        if (function_ptr == nullptr) {
            report_manager_error(result, "IR module contains a null anonymous function unit");
            if (should_stop(result, options)) {
                return;
            }
            continue;
        }

        run_code_unit_invocation(
            pass,
            module,
            source_file_for(*function_ptr),
            *function_ptr,
            result,
            summary);
        if (should_stop(result, options)) {
            return;
        }
    }
}

void run_pass(
    IRPass& pass,
    IRModule& module,
    const IRPassManagerOptions& options,
    IRPassManagerResult& result,
    IRPassRunSummary& summary) {
    switch (pass.scope()) {
        case IRPassScope::Module: {
            auto* module_pass = dynamic_cast<IRModulePass*>(&pass);
            if (module_pass == nullptr) {
                report_manager_error(result, "module pass has an incompatible concrete type");
                return;
            }
            run_module_pass(*module_pass, module, result, summary);
            return;
        }
        case IRPassScope::File: {
            auto* file_pass = dynamic_cast<IRFilePass*>(&pass);
            if (file_pass == nullptr) {
                report_manager_error(result, "file pass has an incompatible concrete type");
                return;
            }
            run_file_pass(*file_pass, module, options, result, summary);
            return;
        }
        case IRPassScope::CodeUnit: {
            auto* code_unit_pass = dynamic_cast<IRCodeUnitPass*>(&pass);
            if (code_unit_pass == nullptr) {
                report_manager_error(result, "code unit pass has an incompatible concrete type");
                return;
            }
            run_code_unit_pass(*code_unit_pass, module, options, result, summary);
            return;
        }
    }

    report_manager_error(result, "pass has an unknown scope");
}

} // namespace

std::string_view ir_pass_scope_name(IRPassScope scope) noexcept {
    switch (scope) {
        case IRPassScope::Module:
            return "module";
        case IRPassScope::File:
            return "file";
        case IRPassScope::CodeUnit:
            return "code-unit";
    }
    return "unknown";
}

bool IRPassResult::ok() const noexcept {
    return !has_error(diagnostics);
}

void IRPassResult::report(
    IRPassDiagnostic::Severity severity,
    std::string_view message,
    SourceSpan source_span) {
    diagnostics.push_back({
        severity,
        {},
        std::string(message),
        source_span,
    });
}

bool IRPassManagerResult::ok() const noexcept {
    return !has_error(diagnostics);
}

IRPassManager::IRPassManager(IRPassManagerOptions options)
    : options_(options) {}

void IRPassManager::add_pass(std::unique_ptr<IRPass> pass) {
    passes_.push_back(std::move(pass));
}

IRPassManagerResult IRPassManager::run(IRModule& module) {
    IRPassManagerResult result;

    if (options_.verify_before_pipeline) {
        append_verify_result(
            result,
            "before pipeline",
            verify_ir(module, options_.verify_options));
        if (should_stop(result, options_)) {
            return result;
        }
    }

    for (const auto& pass_ptr : passes_) {
        if (pass_ptr == nullptr) {
            report_manager_error(result, "pass pipeline contains a null pass");
            if (should_stop(result, options_)) {
                return result;
            }
            continue;
        }

        IRPassRunSummary summary;
        summary.pass_name = std::string(pass_ptr->name());
        summary.scope = pass_ptr->scope();

        run_pass(*pass_ptr, module, options_, result, summary);
        result.pass_runs.push_back(std::move(summary));
        if (should_stop(result, options_)) {
            return result;
        }

        if (options_.verify_after_each_pass) {
            append_verify_result(
                result,
                "after pass " + result.pass_runs.back().pass_name,
                verify_ir(module, options_.verify_options));
            if (should_stop(result, options_)) {
                return result;
            }
        }
    }

    if (options_.verify_after_pipeline) {
        append_verify_result(
            result,
            "after pipeline",
            verify_ir(module, options_.verify_options));
    }

    return result;
}

} // namespace baltam
