#include <cmath>
#include <iostream>
#include <string>

#include "interpreter/interpreter.h"
#include "m_script_test_support.h"

using namespace baltam;
using namespace baltam::test_support;

namespace {

const interpreter::NamedBindingSnapshot* find_named_binding(const interpreter::ExecResult& result,
                                                            const std::string& name) {
    for (const interpreter::NamedBindingSnapshot& binding : result.final_named_bindings) {
        if (binding.name == name) {
            return &binding;
        }
    }
    return nullptr;
}

const interpreter::Value* find_value(const interpreter::ExecResult& result, ValueId id) {
    auto it = result.values.find(id);
    if (it == result.values.end()) {
        return nullptr;
    }
    return &it->second;
}

const interpreter::Value& require_binding_value(const interpreter::ExecResult& result,
                                                const std::string& name) {
    const interpreter::NamedBindingSnapshot* binding = find_named_binding(result, name);
    expect(binding != nullptr, "missing final binding: " + name);

    const interpreter::Value* value = find_value(result, binding->value_id);
    expect(value != nullptr, "binding should resolve to a concrete SSA value: " + name);
    expect(value->type == interpreter::Value::Concrete, "binding should be concrete: " + name);
    expect(value->object != nullptr, "binding object should not be null: " + name);
    return *value;
}

double require_binding_double(const interpreter::ExecResult& result, const std::string& name) {
    return require_binding_value(result, name).object->as_double();
}

interpreter::Value::Object make_double_arg(double value) {
    return std::make_shared<ba_obj>(value);
}

void test_execute_test9() {
    Module ssa_module = build_untyped_ssa_module_for_test_script("test9");
    Function& entry_function = entry_function_or_fail(ssa_module, "test9");

    const interpreter::ExecResult result = execute_function_with_test_trace(
        entry_function, {make_double_arg(1.0), make_double_arg(2.0)});

    expect(result.outputs.size() == 1, "test9 should expose a single return value.");
    expect(result.outputs.front().type == interpreter::Value::Concrete,
           "test9 return value should be concrete.");
    expect(result.outputs.front().object != nullptr, "test9 return object should not be null.");

    const double ret = result.outputs.front().object->as_double();
    const double arg2 = require_binding_double(result, "arg2");
    const double c = require_binding_double(result, "c");
    const double d = require_binding_double(result, "d");
    const double e = require_binding_double(result, "e");

    std::cout << "test9 outputs: ret=" << ret << ", arg2=" << arg2 << ", c=" << c
              << ", d=" << d << ", e=" << e << '\n';

    expect_near(ret, 0.0, 1e-12, "test9 return value mismatch.");
    expect_near(arg2, 3.0, 1e-12, "test9 final arg2 mismatch.");
    expect_near(c, 18.0, 1e-12, "test9 final c mismatch.");
    expect_near(d, 1.0 + std::sqrt(3.0), 1e-12, "test9 final d mismatch.");
    expect_near(e, 5.0, 1e-12, "test9 final e mismatch.");
}

}  // namespace

int main() {
    return run_runtime_test("test_test9", [] {
        test_execute_test9();
    });
}
