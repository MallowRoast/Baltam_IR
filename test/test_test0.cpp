#include <cmath>
#include <iostream>

#include "interpreter/interpreter.h"
#include "m_script_test_support.h"

using namespace baltam;
using namespace baltam::test_support;

namespace {

void test_execute_test0() {
    Module ssa_module = build_untyped_ssa_module_for_test_script("test0");
    Function& entry_function = entry_function_or_fail(ssa_module, "test0");

    const interpreter::ExecResult result = execute_function_with_test_trace(entry_function);
    expect(result.outputs.size() == 3, "test0 script should expose a/b/c outputs.");

    const double a = result.outputs[0].object->as_double();
    const double b = result.outputs[1].object->as_double();
    const double c = result.outputs[2].object->as_double();

    std::cout << "test0 outputs: a=" << a << ", b=" << b << ", c=" << c << '\n';

    const double expected_a = 3.0;
    const double expected_b = std::sin(expected_a);
    const double expected_c = expected_b > 0.0 ? expected_b * 2.0 : 0.0;

    expect_near(a, expected_a, 1e-12, "test0 a mismatch.");
    expect_near(b, expected_b, 1e-12, "test0 b mismatch.");
    expect_near(c, expected_c, 1e-12, "test0 c mismatch.");
}

}  // namespace

int main() {
    return run_runtime_test("test_test0", [] {
        test_execute_test0();
    });
}
