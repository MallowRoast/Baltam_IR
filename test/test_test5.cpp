#include <iostream>

#include "interpreter/interpreter.h"
#include "m_script_test_support.h"

using namespace baltam;
using namespace baltam::test_support;

namespace {

void test_execute_test5() {
    Module ssa_module = build_untyped_ssa_module_for_test_script("test5");
    Function& entry_function = entry_function_or_fail(ssa_module, "test5");

    const interpreter::ExecResult result = execute_function_with_test_trace(entry_function);
    expect(result.outputs.empty(), "test5 should not expose explicit outputs.");

    std::cout << "test5 outputs: none; script completed without runtime error\n";
}

}  // namespace

int main() {
    return run_runtime_test("test_test5", [] {
        test_execute_test5();
    });
}
