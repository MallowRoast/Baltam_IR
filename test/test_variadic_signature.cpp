#include <iostream>
#include <string>

#include "analysis/verifier.h"
#include "bt_ast_interface.h"
#include "lowering/lowering.h"
#include "m_script_test_support.h"
#include "optimizer/construct_untyped_ssa.h"

using namespace baltam;
using namespace baltam::test_support;

namespace {

Function& require_function(Module& module, const std::string& name) {
    for (const auto& function : module.functions()) {
        if (function != nullptr && function->name() == name) {
            return *function;
        }
    }
    fail("missing function: " + name);
}

Module lower_test_script_to_non_ssa(const std::string& script_name) {
    const std::string script_path = resolve_test_script_path(script_name).string();

    std::string msg;
    const auto parsed_units =
        bt_ast_interface::parse_mfile(script_path, ParserOpts{ParserOpts::DEFAULT}, msg);
    expect(!parsed_units.empty(), "failed to parse " + script_name + ".m: " + msg);

    Module non_ssa_module = lower_parsed_units_to_ir(parsed_units);
    analysis::verify_module_or_throw(non_ssa_module);
    return non_ssa_module;
}

void expect_variadic_signature(const Function& function, bool has_varargin, bool has_varargout,
                               std::size_t fixed_input_count, std::size_t fixed_output_count,
                               const std::string& label) {
    expect(function.has_varargin() == has_varargin, label + " has_varargin mismatch.");
    expect(function.has_varargout() == has_varargout, label + " has_varargout mismatch.");
    expect(function.fixed_input_count() == fixed_input_count,
           label + " fixed_input_count mismatch.");
    expect(function.fixed_output_count() == fixed_output_count,
           label + " fixed_output_count mismatch.");
}

void test_lowering_marks_variadic_signature() {
    Module module = lower_test_script_to_non_ssa("test4");

    expect_variadic_signature(require_function(module, "test4"),
                              false, false, 0, 0, "test4 script");
    expect_variadic_signature(require_function(module, "f1"),
                              false, false, 3, 3, "test4/f1");
    expect_variadic_signature(require_function(module, "f2"),
                              true, false, 0, 0, "test4/f2");
    expect_variadic_signature(require_function(module, "f3"),
                              true, false, 0, 0, "test4/f3");
    expect_variadic_signature(require_function(module, "f4"),
                              true, false, 1, 0, "test4/f4");
    expect_variadic_signature(require_function(module, "f5"),
                              true, false, 1, 0, "test4/f5");
}

void test_untyped_ssa_preserves_variadic_signature() {
    Module non_ssa_module = lower_test_script_to_non_ssa("test4_2");
    Module ssa_module = optimizer::construct_untyped_ssa_module(non_ssa_module);
    analysis::verify_module_or_throw(ssa_module);

    expect_variadic_signature(require_function(ssa_module, "varargout_function"),
                              true, true, 0, 1, "test4_2/varargout_function");
    expect_variadic_signature(require_function(ssa_module, "varargout_function2"),
                              true, true, 0, 0, "test4_2/varargout_function2");
    expect_variadic_signature(require_function(ssa_module, "varargout_function3"),
                              true, false, 0, 2, "test4_2/varargout_function3");
}

}  // namespace

int main() {
    return run_runtime_test("test_variadic_signature", [] {
        test_lowering_marks_variadic_signature();
        test_untyped_ssa_preserves_variadic_signature();
    });
}
