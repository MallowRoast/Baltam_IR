#include "ir/ir_type.h"
#include "smoke_test_common.h"

#include <iostream>
#include <sstream>

namespace baltam {
namespace {

void verify_top_bottom() {
    smoke_test::require(TypeSet::bottom().empty(), "bottom 应为空集合");
    smoke_test::require(!TypeSet::any().empty(), "any 不应为空集合");
    smoke_test::require(TypeSet::bottom().is_subset_of(TypeSet::any()), "bottom 应是 any 的子集");
}

void verify_join_meet() {
    const TypeSet int64 = TypeSet::int64();
    const TypeSet float64 = TypeSet::float64();
    const TypeSet string = TypeSet::string_scalar();
    const TypeSet numeric_union = int64.join(float64);

    smoke_test::require(numeric_union.is_superset_of(TypeSet::int64()), "join 应包含 int64");
    smoke_test::require(numeric_union.is_superset_of(TypeSet::float64()), "join 应包含 float64");
    smoke_test::require(!numeric_union.is_superset_of(TypeSet::string_scalar()), "join 不应包含 string");
    smoke_test::require(numeric_union.meet(int64) == int64, "meet union 和 int64 应得到 int64");
    smoke_test::require(numeric_union.meet(string).empty(), "不相交集合 meet 应得到 bottom");
}

void verify_categories() {
    const TypeSet int64 = TypeSet::int64();
    const TypeSet string = TypeSet::string_scalar();
    const TypeSet mixed = int64.join(string);
    const TypeSet function_handle = TypeSet::function_handle();
    const TypeSet external_object = TypeSet::external_object();

    smoke_test::require(int64.definitely(TypeSet::numeric()), "int64 应必然属于 numeric");
    smoke_test::require(mixed.maybe(TypeSet::numeric()), "mixed 应可能属于 numeric");
    smoke_test::require(!mixed.definitely(TypeSet::numeric()), "mixed 不应必然属于 numeric");
    smoke_test::require(function_handle.definitely(TypeSet::callable()), "function handle 应必然 callable");
    smoke_test::require(!int64.maybe(TypeSet::callable()), "int64 不应可能 callable");
    smoke_test::require(!external_object.maybe(TypeSet::numeric()), "extern 不应属于 numeric");
    smoke_test::require(!external_object.maybe(TypeSet::callable()), "extern 不应属于 callable");
}

void verify_formatting() {
    std::ostringstream output;
    output << TypeSet::int64().join(TypeSet::string_scalar());

    smoke_test::require(
        output.str() == "int64|string_scalar",
        "TypeSet 应按稳定顺序格式化 union");

    std::ostringstream double_output;
    double_output << TypeSet::float64();
    smoke_test::require(
        double_output.str() == "double",
        "float64 类型集合应按 IR 文本习惯打印为 double");

    std::ostringstream external_object_output;
    external_object_output << TypeSet::external_object();
    smoke_test::require(
        external_object_output.str() == "extern",
        "external object 应按 extern 打印");
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
