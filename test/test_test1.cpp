#include <cmath>
#include <cstdint>
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

std::int64_t require_binding_int(const interpreter::ExecResult& result, const std::string& name) {
    return require_binding_value(result, name).object->as_int();
}

double require_binding_double(const interpreter::ExecResult& result, const std::string& name) {
    return require_binding_value(result, name).object->as_double();
}

interpreter::Value::Object make_string_arg(const char* text) {
    return std::make_shared<ba_obj>(text, ba_char_mat);
}

void test_execute_test1() {
    Module ssa_module = build_optimized_untyped_ssa_module_for_test_script("test1");
    Function& entry_function = entry_function_or_fail(ssa_module, "test1");

    const interpreter::ExecResult result = execute_function_with_test_trace(
        entry_function, {make_string_arg("left"), make_string_arg("right")});

    expect(result.outputs.size() == 1, "test1 should expose a single return value.");
    expect(result.outputs.front().type == interpreter::Value::Concrete,
           "test1 return value should be concrete.");
    expect(result.outputs.front().object != nullptr, "test1 return object should not be null.");

    const std::int64_t ret = result.outputs.front().object->as_int();
    const std::int64_t arg2 = require_binding_int(result, "arg2");
    const std::int64_t a = require_binding_int(result, "a");
    const std::int64_t b = require_binding_int(result, "b");
    const std::int64_t c = require_binding_int(result, "c");
    const double d = require_binding_double(result, "d");
    const std::int64_t e = require_binding_int(result, "e");

    std::cout << "test1 outputs: ret=" << ret << ", arg2=" << arg2 << ", a=" << a
              << ", b=" << b << ", c=" << c << ", d=" << d << ", e=" << e << '\n';

    expect(ret == 0, "test1 return value mismatch.");
    expect(arg2 == 3, "test1 final arg2 mismatch.");
    expect(a == 1, "test1 final a mismatch.");
    expect(b == 2, "test1 final b mismatch.");
    expect(c == 18, "test1 final c mismatch.");
    expect_near(d, 1.0 + std::sqrt(3.0), 1e-12, "test1 final d mismatch.");
    expect(e == 5, "test1 final e mismatch.");
}

}  // namespace

int main() {
    return run_runtime_test("test_test1", [] {
        test_execute_test1();
    });
}
