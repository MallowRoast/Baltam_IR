#include <iostream>

#include "interpreter/interpreter.h"
#include "m_script_test_support.h"

using namespace baltam;
using namespace baltam::test_support;

namespace {

void test_execute_test7_short() {
    Module ssa_module = build_untyped_ssa_module_for_test_script("test7_short");
    Function& entry_function = entry_function_or_fail(ssa_module, "test7_short");

    const interpreter::ExecResult result = execute_function_with_test_trace(entry_function);
    expect(result.outputs.empty(), "test7_short should not expose explicit outputs.");

    std::cout << "test7_short outputs: none; script completed without runtime error\n";
}

}  // namespace

int main() {
    return run_runtime_test("test_test7_short", [] {
        test_execute_test7_short();
    });
}
