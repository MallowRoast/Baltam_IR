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

const ba_obj& require_binding_object(const interpreter::ExecResult& result, const std::string& name) {
    return *require_binding_value(result, name).object;
}

void test_execute_test1_4() {
    Module ssa_module = build_untyped_ssa_module_for_test_script("test1_4");
    Function& entry_function = entry_function_or_fail(ssa_module, "test1_4");

    const interpreter::ExecResult result = execute_function_with_test_trace(entry_function);
    expect(result.outputs.empty(), "test1_4 should not produce explicit outputs.");

    const ba_obj& a = require_binding_object(result, "a");
    expect(a.is_numeric(), "test1_4 final a should be numeric.");
    expect(!a.is_scalar(), "test1_4 final a should remain a non-scalar matrix.");
    expect(!a.is_empty(), "test1_4 final a should not be empty.");

    const type_base* a_type = a.cget<type_base>();
    expect(a_type != nullptr, "test1_4 final a type metadata should exist.");

    const auto& dims = a_type->dim_vec();
    expect(dims.size() == 4, "test1_4 final a should be a 4-D array.");
    expect(dims[0] == 2 && dims[1] == 2 && dims[2] == 3 && dims[3] == 4,
           "test1_4 final a shape mismatch.");

    std::cout << "test1_4 outputs: final a dims = [" << dims[0] << ", " << dims[1] << ", "
              << dims[2] << ", " << dims[3] << "]\n";
}

}  // namespace

int main() {
    return run_runtime_test("test_test1_4", [] {
        test_execute_test1_4();
    });
}
