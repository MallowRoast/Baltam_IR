#include "runtime_smoke_test_common.h"

#include <cmath>
#include <iostream>
#include <memory>

namespace baltam {
namespace {

const FunctionUnit* find_function_unit_by_name(
    const MFileUnit& mfile,
    std::string_view name) {
    for (const auto& unit_ptr : mfile.code_units) {
        if (unit_ptr != nullptr &&
            unit_ptr->is_function() &&
            unit_ptr->name == name) {
            return static_cast<const FunctionUnit*>(unit_ptr.get());
        }
    }
    return nullptr;
}

void verify_local_function_ir(const IRBuildResult& result) {
    smoke_test::require_ir_is_complete(result);
    smoke_test::require(result.mfile->is_script_file(), "test0.m 应保持为脚本文件");
    smoke_test::require(result.mfile->code_units.size() == 2,
                        "test0.m 应生成脚本和 local 函数两个单元");
    smoke_test::require(result.mfile->file_stem() == "test0", "文件 stem 应为 test0");

    const CodeUnit* entry = result.mfile->entry_unit;
    smoke_test::require(entry != nullptr && entry->is_script(), "入口应为脚本单元");

    const FunctionUnit* local = find_function_unit_by_name(*result.mfile, "test0_local");
    smoke_test::require(local != nullptr, "应生成 test0_local 函数单元");
    smoke_test::require(result.mfile->find_local_function("test0_local") == local,
                        "MFile local function map 应能找到 test0_local");
    smoke_test::require(local != entry, "local 函数不应成为入口单元");
    smoke_test::require(local->param_slots.size() == 1, "test0_local 应声明一个输入参数");
    smoke_test::require(local->return_slots.size() == 1, "test0_local 应声明一个返回值");

    const SlotInfo* param = local->slot_table.find_slot(local->param_slots[0]);
    const SlotInfo* ret = local->slot_table.find_slot(local->return_slots[0]);
    smoke_test::require(param != nullptr && param->slot.tag == SlotTag::Arg && param->name == "x",
                        "test0_local 参数应为 Arg slot x");
    smoke_test::require(ret != nullptr && ret->slot.tag == SlotTag::Ret && ret->name == "y",
                        "test0_local 返回值应为 Ret slot y");
}

void verify_output(const InterpreterContext& context) {
    const double b = std::sin(3.0);
    smoke_test::require_base_workspace_double(context, "a", 3.0);
    smoke_test::require_base_workspace_double(context, "b", b);
    smoke_test::require_base_workspace_double(context, "c", b * 2.0);
    smoke_test::require_base_workspace_double(context, "d", 3.0);
    smoke_test::require_base_workspace_missing(context, "x");
    smoke_test::require_base_workspace_missing(context, "y");
}

void verify_command_lookup_and_execute_test0_with_local_function() {
    const NormalizedPath expected_path = smoke_test::normalize_test_path(TEST0_1_MFILE_PATH);
    smoke_test::require(expected_path.filename() == "test0.m",
                        "test0_1 smoke 应复用 test0.m");

    smoke_test::ScopedWorkerPwd pwd(expected_path.parent_path());

    InterpreterContext context;
    context.command = std::make_unique<CommandUnit>();
    context.command->name = "__command";

    const RuntimeFunctionLookup command_lookup =
        smoke_test::lookup_from_command(context, "test0");
    smoke_test::require(command_lookup.found(),
                        "命令行输入 test0 应能找到当前文件夹下的 test0.m");
    smoke_test::require(command_lookup.kind == RuntimeFunctionKind::MFunctionFile,
                        "未解析缓存前，test0 应解析为 MFunctionFile");
    smoke_test::require(
        smoke_test::normalize_test_path(command_lookup.source_file) == expected_path,
        "命令行输入 test0 应解析到 test0.m");

    IRBuildResult ir = parse_and_lower_mfile_to_ir(command_lookup.source_file.string());
    verify_local_function_ir(ir);

    MFileUnit* mfile = smoke_test::install_mfile_ir(context, ir);
    smoke_test::require(mfile != nullptr, "Context MFile cache 应持有 test0.m IR");

    smoke_test::ScopedBuiltinDefinitions builtins;

    const RuntimeFunctionLookup cached_lookup =
        smoke_test::lookup_from_command(context, "test0");
    smoke_test::require(cached_lookup.found(), "缓存后仍应能找到 test0");
    smoke_test::require(cached_lookup.kind == RuntimeFunctionKind::ScriptFile,
                        "Context 缓存中已有 IR 后，test0 应解析为 ScriptFile");
    smoke_test::require(!cached_lookup.callable(),
                        "脚本文件本身不应作为函数直接调用");
    smoke_test::require(
        smoke_test::normalize_test_path(cached_lookup.source_file) == expected_path,
        "缓存后的 test0 lookup 仍应指向同一路径");

    smoke_test::require_default_runtime_builtins(context);
    smoke_test::execute_script(context, *mfile);
    verify_output(context);
    smoke_test::require_default_runtime_builtin_counts();
}

} // namespace
} // namespace baltam

int main() {
    try {
        baltam::verify_command_lookup_and_execute_test0_with_local_function();
    } catch (const std::exception& ex) {
        std::cerr << "test0_1_smoke 失败: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
