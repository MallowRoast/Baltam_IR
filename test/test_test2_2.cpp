#include <iostream>

#include "interpreter/interpreter.h"
#include "m_script_test_support.h"

using namespace baltam;
using namespace baltam::test_support;

namespace {

void test_execute_test2_2() {
    Module ssa_module = build_untyped_ssa_module_for_test_script("test2_2");
    Function& entry_function = entry_function_or_fail(ssa_module, "test2_2");

    const interpreter::ExecResult result = execute_function_with_test_trace(entry_function);
    expect(result.outputs.size() == 1, "test2_2 should expose exactly one output.");

    const double ret = result.outputs[0].object->as_double();
    std::cout << "test2_2 outputs: ret=" << ret << '\n';

    expect_near(ret, 0.0, 1e-12, "test2_2 ret mismatch.");
}

}  // namespace

int main() {
    return run_runtime_test("test_test2_2", [] {
        test_execute_test2_2();
    });
}
