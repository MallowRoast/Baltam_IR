#include <array>
#include <iostream>
#include <string>

#include "ba_obj/matrix.h"
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

void expect_object_dims(const ba_obj& object, std::initializer_list<baSize> expected_dims,
                        const std::string& message) {
    const type_base* object_type = object.cget<type_base>();
    expect(object_type != nullptr, message + ": missing type metadata");

    const auto& actual_dims = object_type->dim_vec();
    expect(actual_dims.size() == expected_dims.size(), message + ": rank mismatch");

    std::size_t index = 0;
    for (baSize expected_dim : expected_dims) {
        expect(actual_dims[index] == expected_dim,
               message + ": dim " + std::to_string(index) + " mismatch");
        ++index;
    }
}

void expect_double_matrix_equals(const ba_obj& object, std::initializer_list<baSize> expected_dims,
                                 const std::array<double, 20>& expected_values,
                                 const std::string& message) {
    expect(object.type() == ba_double_mat, message + ": expected double matrix");
    expect_object_dims(object, expected_dims, message);

    const auto* mat = object.cget<matrix<double>>();
    expect(mat != nullptr, message + ": missing matrix payload");
    expect(mat->size() == expected_values.size(), message + ": size mismatch");

    for (std::size_t i = 0; i < expected_values.size(); ++i) {
        expect_near((*mat)[static_cast<baIndex>(i)], expected_values[i], 1e-12,
                    message + ": element mismatch at linear index " + std::to_string(i));
    }
}

void test_execute_test2() {
    // `test2.m` 同时覆盖两条关键语义：
    // 1) `d([2,4], 1:2:5) = 4` 应在 lowering 阶段唯一化成 `__ir_paren_set__`
    // 2) 表达式位置的 `c(i,j)` / `a(i,j)` / `b(i,j)` 不能在 lowering 阶段提前固定成
    //    `block get`，而要在解释器里根据 callee 的运行时动态类型再分派
    Module ssa_module = build_untyped_ssa_module_for_test_script("test2");
    Function& entry_function = entry_function_or_fail(ssa_module, "test2");

    const interpreter::ExecResult result = execute_function_with_test_trace(entry_function);
    expect(result.outputs.empty(), "test2 should not produce explicit outputs.");

    const ba_obj& d = require_binding_object(result, "d");
    expect_double_matrix_equals(
        d, {4, 5},
        {1.0, 4.0, 3.0, 4.0, 1.0, 1.0, 3.0, 3.0, 2.0, 4.0,
         3.0, 4.0, 2.0, 2.0, 3.0, 3.0, 2.0, 4.0, 3.0, 4.0},
        "test2 final d mismatch");

    expect_object_dims(require_binding_object(result, "X1"), {30, 100},
                       "test2 final X1 shape mismatch");
    expect_object_dims(require_binding_object(result, "D"), {30, 30},
                       "test2 final D shape mismatch");
    expect_object_dims(require_binding_object(result, "V"), {30, 30},
                       "test2 final V shape mismatch");

    std::cout << "test2 outputs: block_set(d)=ok, runtime_paren_get_dispatch=ok, "
                 "d=[4x5], X1=[30x100], D=[30x30], V=[30x30]\n";
}

}  // namespace

int main() {
    return run_runtime_test("test_test2", [] {
        test_execute_test2();
    });
}
