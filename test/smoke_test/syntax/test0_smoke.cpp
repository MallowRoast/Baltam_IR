#include "runtime_smoke_test_common.h"

#include <cmath>
#include <iostream>
#include <memory>

namespace baltam {
namespace {

void verify_command_lookup_and_execute_test0() {
    const NormalizedPath expected_path = smoke_test::normalize_test_path(TEST0_MFILE_PATH);
    smoke_test::require(expected_path.filename() == "test0.m",
                        "test0 smoke 应使用 test0.m");

    smoke_test::ScopedWorkerPwd pwd(expected_path.parent_path());

    InterpreterContext context;
    context.command = std::make_unique<CommandUnit>();
    context.command->name = "__command";

    const RuntimeFunctionLookup command_lookup =
        smoke_test::lookup_from_command(context, "test0");
    smoke_test::require(command_lookup.found(),
                        "命令行输入 test0 应能找到当前文件夹下的 test0.m");
    smoke_test::require(command_lookup.kind == RuntimeFunctionKind::MFunctionFile,
                        "未解析缓存前，M 文件路径 lookup 应保持为 MFunctionFile");
    smoke_test::require(
        smoke_test::normalize_test_path(command_lookup.source_file) == expected_path,
        "命令行输入 test0 应解析到 test0.m");

    IRBuildResult ir = parse_and_lower_mfile_to_ir(command_lookup.source_file.string());
    smoke_test::require_ir_is_complete(ir);
    smoke_test::require(ir.mfile->is_script_file(), "test0.m 应 lower 成脚本文件");

    MFileUnit* mfile = smoke_test::install_mfile_ir(context, ir);
    smoke_test::require(mfile != nullptr, "Context MFile cache 应持有 test0.m IR");

    smoke_test::ScopedBuiltinDefinitions builtins;

    const RuntimeFunctionLookup cached_lookup =
        smoke_test::lookup_from_command(context, "test0");
    smoke_test::require(cached_lookup.kind == RuntimeFunctionKind::ScriptFile,
                        "Context 缓存中已有 IR 后，test0 应解析为 ScriptFile");
    smoke_test::require(
        smoke_test::normalize_test_path(cached_lookup.source_file) == expected_path,
        "缓存后的 test0 lookup 仍应指向同一路径");

    smoke_test::require_default_runtime_builtins(context);
    smoke_test::execute_script(context, *mfile);

    const double b = std::sin(3.0);
    smoke_test::require_base_workspace_double(context, "a", 3.0);
    smoke_test::require_base_workspace_double(context, "b", b);
    smoke_test::require_base_workspace_double(context, "c", b * 2.0);
    smoke_test::require_base_workspace_double(context, "d", 3.0);
    smoke_test::require_default_runtime_builtin_counts();
}

} // namespace
} // namespace baltam

int main() {
    try {
        baltam::verify_command_lookup_and_execute_test0();
    } catch (const std::exception& ex) {
        std::cerr << "test0_smoke 失败: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
