#include <complex>
#include <cstdint>
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

double require_binding_double(const interpreter::ExecResult& result, const std::string& name) {
    return require_binding_value(result, name).object->as_double();
}

std::complex<double> require_binding_complex(const interpreter::ExecResult& result,
                                             const std::string& name) {
    const ba_obj& object = *require_binding_value(result, name).object;
    expect(object.is_scalar(), "binding should be scalar: " + name);

    if (object.type() == ba_complex_double_mat) {
        return object.cget<matrix<std::complex<double>>>()->operator[](0);
    }
    if (object.type() == ba_complex_single_mat) {
        const std::complex<float> value = object.cget<matrix<std::complex<float>>>()->operator[](0);
        return std::complex<double>{value.real(), value.imag()};
    }

    fail("binding should be complex: " + name);
}

void expect_complex_near(const std::complex<double>& actual, const std::complex<double>& expected,
                         double tolerance, const std::string& message) {
    expect_near(actual.real(), expected.real(), tolerance, message + " (real)");
    expect_near(actual.imag(), expected.imag(), tolerance, message + " (imag)");
}

void test_execute_test1_3() {
    Module ssa_module = build_untyped_ssa_module_for_test_script("test1_3");
    Function& entry_function = entry_function_or_fail(ssa_module, "test1_3");

    const interpreter::ExecResult result = execute_function_with_test_trace(entry_function);
    expect(result.outputs.empty(), "test1_3 should not produce explicit outputs.");

    const double a = require_binding_double(result, "a");
    const double b = require_binding_double(result, "b");
    const std::complex<double> c = require_binding_complex(result, "c");
    const std::complex<double> d = require_binding_complex(result, "d");
    const double i = require_binding_double(result, "i");
    const double j = require_binding_double(result, "j");

    std::cout << "test1_3 outputs: a=" << a << ", b=" << b << ", c=" << c << ", d=" << d
              << ", i=" << i << ", j=" << j << '\n';

    expect_near(a, 4.3, 1e-12, "test1_3 final a mismatch.");
    expect_near(b, 2.3, 1e-12, "test1_3 final b mismatch.");
    expect_complex_near(c, {0.0, 2.3}, 1e-12, "test1_3 final c mismatch.");
    expect_complex_near(d, {2.0, 2.7}, 1e-12, "test1_3 final d mismatch.");
    expect_near(i, 3.3, 1e-12, "test1_3 final i mismatch.");
    expect_near(j, 3.3, 1e-12, "test1_3 final j mismatch.");
}

}  // namespace

int main() {
    return run_runtime_test("test_test1_3", [] {
        test_execute_test1_3();
    });
}
