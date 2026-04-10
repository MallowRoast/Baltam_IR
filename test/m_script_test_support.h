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

#include "baltam_ir.h"
#include "bt_ast_interface.h"

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

inline Module build_untyped_ssa_module_for_test_script(const std::string& script_name) {
    MFileIRPipeline pipeline = build_mfile_ir_pipeline(resolve_test_script_path(script_name).string());
    return std::move(pipeline.untyped_ssa_module);
}

inline Function& entry_function_or_fail(Module& ssa_module, const std::string& script_name) {
    Function* entry_function = ssa_module.entry_function();
    expect(entry_function != nullptr, script_name + " SSA module should have entry function.");
    return *entry_function;
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
