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

void run_file_pass(
    IRFilePass& pass,
    MFileUnit& mfile,
    IRPassManagerResult& result,
    IRPassRunSummary& summary) {
    IRPassContext context;
    context.file = &mfile;

    ++summary.invocations;
    append_pass_result(result, summary, pass.name(), pass.run(mfile, context));
}

void run_code_unit_invocation(
    IRCodeUnitPass& pass,
    MFileUnit* file,
    CodeUnit& unit,
    IRPassManagerResult& result,
    IRPassRunSummary& summary) {
    IRPassContext context;
    context.file = file;
    context.unit = &unit;

    ++summary.invocations;
    append_pass_result(result, summary, pass.name(), pass.run(unit, context));
}

void run_code_unit_pass(
    IRCodeUnitPass& pass,
    MFileUnit& mfile,
    const IRPassManagerOptions& options,
    IRPassManagerResult& result,
    IRPassRunSummary& summary) {
    for (const auto& unit_ptr : mfile.code_units) {
        if (unit_ptr == nullptr) {
            report_manager_error(result, "IR file contains a null code unit");
            if (should_stop(result, options)) {
                return;
            }
            continue;
        }

        run_code_unit_invocation(
            pass,
            &mfile,
            *unit_ptr,
            result,
            summary);
        if (should_stop(result, options)) {
            return;
        }
    }
}

void run_pass(
    IRPass& pass,
    MFileUnit& mfile,
    const IRPassManagerOptions& options,
    IRPassManagerResult& result,
    IRPassRunSummary& summary) {
    switch (pass.scope()) {
        case IRPassScope::File: {
            auto* file_pass = dynamic_cast<IRFilePass*>(&pass);
            if (file_pass == nullptr) {
                report_manager_error(result, "file pass has an incompatible concrete type");
                return;
            }
            run_file_pass(*file_pass, mfile, result, summary);
            return;
        }
        case IRPassScope::CodeUnit: {
            auto* code_unit_pass = dynamic_cast<IRCodeUnitPass*>(&pass);
            if (code_unit_pass == nullptr) {
                report_manager_error(result, "code unit pass has an incompatible concrete type");
                return;
            }
            run_code_unit_pass(*code_unit_pass, mfile, options, result, summary);
            return;
        }
    }

    report_manager_error(result, "pass has an unknown scope");
}

} // namespace

std::string_view ir_pass_scope_name(IRPassScope scope) noexcept {
    switch (scope) {
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

IRPassManagerResult IRPassManager::run(MFileUnit& mfile) {
    IRPassManagerResult result;

    if (options_.verify_before_pipeline) {
        append_verify_result(
            result,
            "before pipeline",
            verify_ir(mfile, options_.verify_options));
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

        run_pass(*pass_ptr, mfile, options_, result, summary);
        result.pass_runs.push_back(std::move(summary));
        if (should_stop(result, options_)) {
            return result;
        }

        if (options_.verify_after_each_pass) {
            append_verify_result(
                result,
                "after pass " + result.pass_runs.back().pass_name,
                verify_ir(mfile, options_.verify_options));
            if (should_stop(result, options_)) {
                return result;
            }
        }
    }

    if (options_.verify_after_pipeline) {
        append_verify_result(
            result,
            "after pipeline",
            verify_ir(mfile, options_.verify_options));
    }

    return result;
}

} // namespace baltam
