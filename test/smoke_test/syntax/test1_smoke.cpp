#include "smoke_test_common.h"

#include <iostream>

namespace baltam {
namespace {

const FunctionUnit* find_function_unit_by_name(const MFileUnit& mfile, std::string_view name) {
    for (const auto& unit_ptr : mfile.code_units) {
        if (unit_ptr != nullptr &&
            unit_ptr->is_function() &&
            unit_ptr->name == name) {
            return static_cast<const FunctionUnit*>(unit_ptr.get());
        }
    }
    return nullptr;
}

void verify_complete_ir(const IRBuildResult& result) {
    smoke_test::require_ir_is_complete(result);
    smoke_test::require(result.mfile->is_script_file(), "test1 应构造成脚本文件");
    smoke_test::require(result.mfile->code_units.size() == 2, "test1 应生成脚本和 local 函数两个单元");
    smoke_test::require(result.mfile->file_stem() == "test1", "文件 stem 应为 test1");
}

void verify_core_focus(const IRBuildResult& result) {
    const CodeUnit* entry = result.mfile->entry_unit;
    smoke_test::require(entry != nullptr && entry->is_script(), "入口应为脚本单元");
    smoke_test::require(
        result.mfile->find_local_function("sin") != nullptr,
        "应能在 mfile 上查到 local 函数 sin");

    const FunctionUnit* local_sin = find_function_unit_by_name(*result.mfile, "sin");
    smoke_test::require(local_sin != nullptr, "应生成名为 sin 的 local 函数单元");
    smoke_test::require(local_sin != entry, "local 函数不应成为入口单元");
    smoke_test::require(local_sin->param_slots.size() == 1, "local sin 应声明一个输入参数");
    smoke_test::require(local_sin->return_slots.size() == 1, "local sin 应声明一个返回值");

    smoke_test::require(
        smoke_test::count_instructions(*entry, Instruction::Apply) == 1,
        "脚本中的 sin(a) 仍应保留为 apply");
    smoke_test::require(
        smoke_test::count_instructions(*entry, Instruction::Call) == 0,
        "脚本主体不应生成 call");
}

} // namespace
} // namespace baltam

int main() {
    try {
        const baltam::smoke_test::SmokeArtifacts artifacts =
            baltam::smoke_test::build_ir(TEST1_MFILE_PATH);
        baltam::verify_complete_ir(artifacts.result);
        baltam::verify_core_focus(artifacts.result);
    } catch (const std::exception& ex) {
        std::cerr << "test1_smoke 失败: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
