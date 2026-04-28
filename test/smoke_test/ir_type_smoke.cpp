#include "ir/ir_type.h"
#include "smoke_test_common.h"

#include <iostream>
#include <sstream>

namespace baltam {
namespace {

void verify_top_bottom() {
    smoke_test::require(bottom_type_set().empty(), "bottom 应为空集合");
    smoke_test::require(!any_type_set().empty(), "any 不应为空集合");
    smoke_test::require(bottom_type_set().is_subset_of(any_type_set()), "bottom 应是 any 的子集");
}

void verify_join_meet() {
    const TypeSet int64 = singleton_type_set(TypeAtom::Int64);
    const TypeSet float64 = singleton_type_set(TypeAtom::Float64);
    const TypeSet string = singleton_type_set(TypeAtom::String);
    const TypeSet numeric_union = join(int64, float64);

    smoke_test::require(numeric_union.contains(TypeAtom::Int64), "join 应包含 int64");
    smoke_test::require(numeric_union.contains(TypeAtom::Float64), "join 应包含 float64");
    smoke_test::require(!numeric_union.contains(TypeAtom::String), "join 不应包含 string");
    smoke_test::require(meet(numeric_union, int64) == int64, "meet union 和 int64 应得到 int64");
    smoke_test::require(meet(numeric_union, string).empty(), "不相交集合 meet 应得到 bottom");
}

void verify_categories() {
    const TypeSet int64 = singleton_type_set(TypeAtom::Int64);
    const TypeSet string = singleton_type_set(TypeAtom::String);
    const TypeSet mixed = join(int64, string);
    const TypeSet function_handle = singleton_type_set(TypeAtom::FunctionHandle);

    smoke_test::require(definitely(int64, numeric_type_set()), "int64 应必然属于 numeric");
    smoke_test::require(maybe(mixed, numeric_type_set()), "mixed 应可能属于 numeric");
    smoke_test::require(!definitely(mixed, numeric_type_set()), "mixed 不应必然属于 numeric");
    smoke_test::require(definitely(function_handle, callable_type_set()), "function handle 应必然 callable");
    smoke_test::require(!maybe(int64, callable_type_set()), "int64 不应可能 callable");
}

void verify_formatting() {
    std::ostringstream output;
    output << join(
        singleton_type_set(TypeAtom::Int64),
        singleton_type_set(TypeAtom::String));

    smoke_test::require(
        output.str() == "int64|string_scalar",
        "TypeSet 应按稳定顺序格式化 union");
}

} // namespace
} // namespace baltam

int main() {
    try {
        baltam::verify_top_bottom();
        baltam::verify_join_meet();
        baltam::verify_categories();
        baltam::verify_formatting();
        std::cout << "ir_type_smoke passed\n";
    } catch (const std::exception& ex) {
        std::cerr << "ir_type_smoke 失败: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
