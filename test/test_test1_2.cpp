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

void test_execute_test1_2() {
    Module ssa_module = build_untyped_ssa_module_for_test_script("test1_2");
    Function& entry_function = entry_function_or_fail(ssa_module, "test1_2");

    const interpreter::ExecResult result = execute_function_with_test_trace(entry_function);
    expect(result.outputs.empty(), "test1_2 should not produce explicit outputs.");

    const std::int64_t s = require_binding_int(result, "s");
    const std::int64_t i = require_binding_int(result, "i");
    const std::int64_t j = require_binding_int(result, "j");

    std::cout << "test1_2 outputs: s=" << s << ", i=" << i << ", j=" << j << '\n';

    expect(s == 335, "test1_2 final s mismatch.");
    expect(i == 2, "test1_2 final i mismatch.");
    expect(j == 6, "test1_2 final j mismatch.");
}

}  // namespace

int main() {
    return run_runtime_test("test_test1_2", [] {
        test_execute_test1_2();
    });
}
