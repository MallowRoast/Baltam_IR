#ifndef BALTAM_IR_TEST_M_SCRIPT_TEST_SUPPORT_H
#define BALTAM_IR_TEST_M_SCRIPT_TEST_SUPPORT_H

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "analysis/verifier.h"
#include "bt_ast_interface.h"
#include "interpreter/interpreter.h"
#include "ir/ir_printer.h"
#include "lowering/lowering.h"
#include "optimizer/construct_untyped_ssa.h"

namespace baltam::test_support {

namespace fs = std::filesystem;

#ifndef BALTAM_IR_TEST_SOURCE_DIR
#error "BALTAM_IR_TEST_SOURCE_DIR must be defined for m script tests."
#endif

[[noreturn]] inline void fail(const std::string& message) {
    throw std::runtime_error(message);
}

inline void expect(bool condition, const std::string& message) {
    if (!condition) {
        fail(message);
    }
}

inline void expect_near(double actual, double expected, double tolerance,
                        const std::string& message) {
    if (std::abs(actual - expected) > tolerance) {
        std::ostringstream oss;
        oss << message << " actual = " << actual << " expected = " << expected;
        fail(oss.str());
    }
}

inline bool path_exists(const fs::path& path) {
    std::error_code error;
    return fs::exists(path, error);
}

inline fs::path resolve_test_script_path(const std::string& script_name) {
    const fs::path script_path =
        fs::path(BALTAM_IR_TEST_SOURCE_DIR) / "m" / script_name / (script_name + ".m");
    expect(path_exists(script_path), "failed to locate " + script_name + ".m: " +
                                         script_path.string());
    return script_path.lexically_normal();
}

inline void print_ir_dump(const std::string& script_name, const char* stage, const Module& module) {
    std::cout << "===== " << script_name << " " << stage << " =====\n";
    print_ir(std::cout, module);
    std::cout << '\n';
}

inline Module build_untyped_ssa_module_for_test_script(const std::string& script_name) {
    const std::string script_path = resolve_test_script_path(script_name).string();

    std::string msg;
    const auto parsed_units =
        bt_ast_interface::parse_mfile(script_path, ParserOpts{ParserOpts::DEFAULT}, msg);
    expect(!parsed_units.empty(), "failed to parse " + script_name + ".m: " + msg);

    Module non_ssa_module = lower_parsed_units_to_ir(parsed_units);
    analysis::verify_module_or_throw(non_ssa_module);
    print_ir_dump(script_name, "non-SSA", non_ssa_module);

    Module ssa_module = optimizer::construct_untyped_ssa_module(non_ssa_module);
    analysis::verify_module_or_throw(ssa_module);
    print_ir_dump(script_name, "untyped SSA", ssa_module);
    return ssa_module;
}

inline Function& entry_function_or_fail(Module& ssa_module, const std::string& script_name) {
    Function* entry_function = ssa_module.entry_function();
    expect(entry_function != nullptr, script_name + " SSA module should have entry function.");
    return *entry_function;
}

inline interpreter::ExecResult execute_function_with_test_trace(
    Function& function, const std::vector<interpreter::Value::Object>& args = {}) {
    interpreter::ExecutionOptions options;
    options.trace_stream = &std::cout;
    options.print_final_named_bindings = true;
    return interpreter::execute_function(function, args, options);
}

inline void execute_no_output_test_script(const std::string& script_name) {
    Module ssa_module = build_untyped_ssa_module_for_test_script(script_name);
    Function& entry_function = entry_function_or_fail(ssa_module, script_name);

    const interpreter::ExecResult result = execute_function_with_test_trace(entry_function);
    expect(result.outputs.empty(), script_name + " should not expose explicit outputs.");

    std::cout << script_name << " outputs: none; script completed without runtime error\n";
}

template <typename TestBody>
int run_runtime_test(const char* test_name, TestBody&& test_body) {
    int exit_code = 0;
    const int init_ret = bt_ast_interface::initialize();
    if (init_ret != 0) {
        std::cerr << test_name << " FAILED: bt_ast_interface::initialize returned " << init_ret
                  << '\n';
        return 1;
    }

    try {
        std::forward<TestBody>(test_body)();
        std::cout << test_name << " PASSED\n";
    } catch (const std::exception& ex) {
        std::cerr << test_name << " FAILED: " << ex.what() << '\n';
        exit_code = 1;
    }

    std::cout << std::flush;
    std::cerr << std::flush;
    bt_ast_interface::finalize();
    std::_Exit(exit_code);
}

}  // namespace baltam::test_support

#endif
