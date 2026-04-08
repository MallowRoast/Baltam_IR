#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "analysis/verifier.h"
#include "bt_ast_interface.h"
#include "interpreter/interpreter.h"
#include "lowering/lowering.h"
#include "optimizer/construct_untyped_ssa.h"

using namespace baltam;

namespace {

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

void expect(bool condition, const std::string& message) {
    if (!condition) {
        fail(message);
    }
}

void expect_near(double actual, double expected, double tolerance, const std::string& message) {
    if (std::abs(actual - expected) > tolerance) {
        fail(message + " actual = " + std::to_string(actual) +
             " expected = " + std::to_string(expected));
    }
}

std::string source_path_from_relative(std::string_view relative_path) {
    return std::string(BALTAM_IR_SOURCE_DIR) + std::string(relative_path);
}

void configure_runtime_library_path() {
    const std::string required_prefix =
        std::string("/opt/Baltamatica/lib:") + source_path_from_relative("/deps/core/lib");
    const char* existing = std::getenv("LD_LIBRARY_PATH");
    if (existing == nullptr || std::string(existing).empty()) {
        setenv("LD_LIBRARY_PATH", required_prefix.c_str(), 1);
        return;
    }

    const std::string current = existing;
    if (current.find(required_prefix) == 0) {
        return;
    }
    setenv("LD_LIBRARY_PATH", (required_prefix + ":" + current).c_str(), 1);
}

Module build_ssa_module_from_relative_path(std::string_view relative_path) {
    const std::string script_path = source_path_from_relative(relative_path);
    std::string parse_message;
    const auto parsed_units =
        bt_ast_interface::parse_mfile(script_path, ParserOpts{ParserOpts::DEFAULT}, parse_message);

    expect(!parsed_units.empty(), std::string(relative_path) +
                                     " parse should produce AST units: " + parse_message);

    Module non_ssa_module = lower_parsed_units_to_ir(parsed_units);
    analysis::verify_module_or_throw(non_ssa_module);

    Module ssa_module = optimizer::construct_untyped_ssa_module(non_ssa_module);
    analysis::verify_module_or_throw(ssa_module);
    return ssa_module;
}

void test_execute_simple_demo() {
    Module ssa_module = build_ssa_module_from_relative_path("/test/simple_demo.m");
    Function* entry_function = ssa_module.entry_function();
    expect(entry_function != nullptr, "simple_demo SSA module should have entry function.");

    const interpreter::ExecResult result = interpreter::execute_function(*entry_function);
    expect(result.outputs.size() == 3, "simple_demo script should expose a/b/c outputs.");

    const double a = result.outputs[0].object->as_double();
    const double b = result.outputs[1].object->as_double();
    const double c = result.outputs[2].object->as_double();

    const double expected_a = 3.0;
    const double expected_b = std::sin(expected_a);
    const double expected_c = expected_b > 0.0 ? expected_b * 2.0 : 0.0;

    expect_near(a, expected_a, 1e-12, "simple_demo a mismatch.");
    expect_near(b, expected_b, 1e-12, "simple_demo b mismatch.");
    expect_near(c, expected_c, 1e-12, "simple_demo c mismatch.");
}

void test_execute_test1_with_args() {
    Module ssa_module = build_ssa_module_from_relative_path("/test/test1/test1.m");
    Function* entry_function = ssa_module.entry_function();
    expect(entry_function != nullptr, "test1 SSA module should have entry function.");
    expect(entry_function->inputs().size() == 2, "test1 should require exactly 2 input args.");

    const interpreter::ExecResult result = interpreter::execute_function(
        *entry_function,
        {std::make_shared<ba_obj>("left", ba_char_mat),
         std::make_shared<ba_obj>("right", ba_char_mat)});

    expect(result.outputs.size() == 1, "test1 should expose ret output.");
    expect(result.outputs[0].type == interpreter::Value::Concrete,
           "test1 output should be concrete.");
    expect(result.outputs[0].object != nullptr, "test1 output object should not be null.");
    expect(result.outputs[0].object->as_int() == 0, "test1 ret should be 0.");
}

}  // namespace

int main() {
    int exit_code = 0;
    configure_runtime_library_path();
    const int init_ret = bt_ast_interface::initialize();
    if (init_ret != 0) {
        std::cerr << "simple_demo_test FAILED: bt_ast_interface::initialize returned "
                  << init_ret << '\n';
        return 1;
    }

    try {
        test_execute_simple_demo();
        test_execute_test1_with_args();
        std::cout << "simple_demo_test PASSED\n";
    } catch (const std::exception& ex) {
        std::cerr << "simple_demo_test FAILED: " << ex.what() << '\n';
        exit_code = 1;
    }

    std::cout << std::flush;
    std::cerr << std::flush;
    bt_ast_interface::finalize();
    std::_Exit(exit_code);
}
