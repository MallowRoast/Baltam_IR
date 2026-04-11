#include <iostream>

#include "interpreter/interpreter.h"
#include "m_script_test_support.h"

using namespace baltam;
using namespace baltam::test_support;

namespace {

void test_execute_test2_1() {
    // `test2_1.m` 自身会校验：
    // 1) 索引读写与转置结果
    // 2) 多返回值对切片赋值
    // 3) `for c = cc` 的列迭代语义
    Module ssa_module = build_untyped_ssa_module_for_test_script("test2_1");
    Function& entry_function = entry_function_or_fail(ssa_module, "test2_1");

    const interpreter::ExecResult result = execute_function_with_test_trace(entry_function);
    expect(result.outputs.empty(), "test2_1 should not expose explicit outputs.");

    std::cout << "test2_1 outputs: none; script completed without runtime error\n";
}

}  // namespace

int main() {
    return run_runtime_test("test_test2_1", [] {
        test_execute_test2_1();
    });
}
