#include "smoke_test_common.h"

#include <iostream>

namespace baltam {
namespace {

void require_value_type(
    const CodeUnit& unit,
    ValueId value_id,
    TypeSet expected_types,
    bool expected_scalar,
    const char* message) {
    const ValueInfo* value_info = unit.value_table.find(value_id);
    smoke_test::require(value_info != nullptr, message);
    smoke_test::require(value_info->value_id == value_id, message);
    smoke_test::require(value_info->def != nullptr, message);
    smoke_test::require(!value_info->type_fact.is_unknown, message);
    smoke_test::require(value_info->type_fact.types == expected_types, message);
    smoke_test::require(value_info->type_fact.is_scalar == expected_scalar, message);
}

void require_value_unknown(
    const CodeUnit& unit,
    ValueId value_id,
    const char* message) {
    const ValueInfo* value_info = unit.value_table.find(value_id);
    smoke_test::require(value_info != nullptr, message);
    smoke_test::require(value_info->value_id == value_id, message);
    smoke_test::require(value_info->def != nullptr, message);
    smoke_test::require(value_info->type_fact.is_unknown, message);
    smoke_test::require(!value_info->type_fact.is_scalar, message);
}

void verify_complete_ir(const IRBuildResult& result) {
    smoke_test::require_ir_is_complete(result);
    smoke_test::require(result.mfile->is_script_file(), "test0 应构造成脚本文件");
    smoke_test::require(result.mfile->code_units.size() == 1, "应只生成一个代码单元");
    smoke_test::require(result.mfile->file_stem() == "test0", "文件 stem 应为 test0");
}

void verify_core_focus(const IRBuildResult& result) {
    const CodeUnit* unit = result.mfile->entry_unit;
    smoke_test::require(unit != nullptr, "入口代码单元不能为空");
    smoke_test::require(unit->find_hidden_slot(SlotAttrs::WorkspaceHandle) != nullptr, "脚本应创建环境槽位");
    smoke_test::require(unit->slot_table.slots.size() == 1, "脚本 lowering 应只创建一个环境槽位");

    smoke_test::require(
        smoke_test::count_instructions(*unit, Instruction::LoadWorkspace) == 3,
        "脚本 lowering 应生成 3 条 load_env");
    smoke_test::require(
        smoke_test::count_instructions(*unit, Instruction::StoreWorkspace) == 4,
        "脚本 lowering 应生成 4 条 store_env");
    smoke_test::require(
        smoke_test::count_instructions(*unit, Instruction::LoadSlot) == 0 &&
            smoke_test::count_instructions(*unit, Instruction::StoreSlot) == 0,
        "脚本 lowering 不应生成 slot 读写");
    smoke_test::require(
        smoke_test::count_instructions(*unit, Instruction::Apply) == 1,
        "脚本 lowering 应保留一条 apply");
    smoke_test::require(
        smoke_test::count_instructions(*unit, Instruction::Call) == 0,
        "脚本 lowering 不应生成 call");

    const Instruction* apply_inst =
        smoke_test::find_first_instruction(*unit, Instruction::Apply);
    smoke_test::require(apply_inst != nullptr, "脚本中应存在 apply 指令");

    const auto* apply = static_cast<const ApplyInst*>(apply_inst);
    smoke_test::require(apply->results.size() == 1, "脚本中的 apply 应产生一个结果");
    smoke_test::require(apply->arguments.size() == 1, "脚本中的 apply 应只有一个参数");
    smoke_test::require(
        std::holds_alternative<InternedString>(apply->callee_or_base) &&
            std::get<InternedString>(apply->callee_or_base) == "sin",
        "脚本中的 sin(a) 应保持为按名字的 apply");
}

void verify_value_types(const IRBuildResult& result) {
    const CodeUnit* unit = result.mfile->entry_unit;
    smoke_test::require(unit != nullptr, "入口代码单元不能为空");
    smoke_test::require(unit->value_table.values.size() == 12, "test0 应为 12 个 ValueId 持有类型信息");

    require_value_type(*unit, ValueId(0), TypeSet::float64(), true, "%0 应是 float64 标量常量");
    require_value_type(*unit, ValueId(1), TypeSet::float64(), true, "%1 应是 float64 标量常量");
    require_value_unknown(*unit, ValueId(2), "动态 add 结果在类型推导前应保持 unknown");

    require_value_unknown(*unit, ValueId(3), "脚本 apply 结果在类型推导前应保持 unknown");
    require_value_unknown(*unit, ValueId(4), "脚本 workspace 读取在类型推导前应保持 unknown");
    require_value_unknown(*unit, ValueId(5), "b 的 workspace 读取在类型推导前应保持 unknown");

    require_value_type(*unit, ValueId(6), TypeSet::float64(), true, "if 比较中的 0 应是 float64 标量");
    require_value_unknown(*unit, ValueId(7), "动态比较结果在类型推导前应保持 unknown");

    require_value_unknown(*unit, ValueId(8), "then 分支读取 b 在类型推导前应保持 unknown");
    require_value_type(*unit, ValueId(9), TypeSet::float64(), true, "then 分支中的 2 应是 float64 标量");
    require_value_unknown(*unit, ValueId(10), "b * 2 的结果在类型推导前应保持 unknown");
    require_value_type(*unit, ValueId(11), TypeSet::float64(), true, "else 分支中的 0 应是 float64 标量");
}

void verify_printed_ir(const std::string& printed_ir) {
    smoke_test::require(!printed_ir.empty(), "打印结果不能为空");
    smoke_test::require(
        printed_ir.find("; mfile \"" TEST0_MFILE_PATH "\"") != std::string::npos,
        "应打印文件头");
    smoke_test::require(
        printed_ir.find("script @test0 {") != std::string::npos,
        "应打印 script 头");

    const std::string env_line =
        smoke_test::find_line_containing(printed_ir, "%test0_env = hidden(env) @test0_env");
    smoke_test::require(!env_line.empty(), "应打印脚本环境槽位");
    smoke_test::require(env_line.find("; ") == std::string::npos, "环境槽位行不应打印源码注释");

    const std::string apply_line =
        smoke_test::find_line_containing(printed_ir, "[%3, unknown] = apply @sin(%4)");
    smoke_test::require(!apply_line.empty(), "应打印 sin 的 apply");
    smoke_test::require(apply_line.find("; ") == std::string::npos, "apply 行不应打印源码注释");
    smoke_test::require(
        printed_ir.find("[%2, unknown] = add %0, %1") != std::string::npos,
        "动态 add 结果在类型推导前应打印为 unknown");
    smoke_test::require(
        printed_ir.find("[%4, unknown] = load_env %test0_env, @a") != std::string::npos,
        "workspace 读取在类型推导前应打印为 unknown");
    smoke_test::require(
        printed_ir.find("[%7, unknown] = cmp.gt %5, %6") != std::string::npos,
        "动态比较结果在类型推导前应打印为 unknown");

    const std::string store_a_line =
        smoke_test::find_line_containing(printed_ir, "store_env %test0_env, @a, %2");
    smoke_test::require(!store_a_line.empty(), "应打印 a 的环境写回");
    smoke_test::require(
        store_a_line.find("; line ") != std::string::npos &&
            store_a_line.find("a = 1 + 2;") != std::string::npos,
        "a 的写回行应打印源码行号注释");

    smoke_test::require(
        printed_ir.find("call @sin") == std::string::npos,
        "脚本 IR 不应把 sin 打印成 call");
    smoke_test::require(
        printed_ir.find("load_slot") == std::string::npos &&
            printed_ir.find("store_slot") == std::string::npos,
        "脚本 IR 不应打印 slot 读写");
}

void verify_other_important_checks(
    const IRBuildResult& result,
    const std::string& printed_ir) {
    const CodeUnit* unit = result.mfile->entry_unit;
    smoke_test::require(unit->basic_blocks.size() == 4, "if/else 脚本应生成 4 个基本块");
    smoke_test::require(
        unit->basic_blocks[0]->terminator()->type() == Instruction::Branch,
        "入口基本块应以条件分支结束");
    smoke_test::require(
        unit->basic_blocks[1]->terminator()->type() == Instruction::Goto &&
            unit->basic_blocks[2]->terminator()->type() == Instruction::Goto,
        "then/else 基本块应以跳转结束");
    smoke_test::require(
        unit->basic_blocks[3]->terminator()->type() == Instruction::Return,
        "exit 基本块应以返回结束");

    const std::string branch_line = smoke_test::find_line_containing(
        printed_ir,
        "br %7, label %if.then, label %if.else");
    smoke_test::require(!branch_line.empty(), "应打印 if 条件分支");
    smoke_test::require(
        branch_line.find("; line ") != std::string::npos &&
            branch_line.find("b > 0") != std::string::npos,
        "条件分支行应打印源码行号注释");
    smoke_test::require(
        printed_ir.find("\n  ret") != std::string::npos,
        "脚本 IR 应打印隐式 ret");
}

} // namespace
} // namespace baltam

int main() {
    try {
        const baltam::smoke_test::SmokeArtifacts artifacts =
            baltam::smoke_test::build_ir(TEST0_MFILE_PATH);
        baltam::verify_complete_ir(artifacts.result);
        baltam::verify_core_focus(artifacts.result);
        baltam::verify_value_types(artifacts.result);
        baltam::verify_printed_ir(artifacts.printed_ir);
        baltam::verify_other_important_checks(artifacts.result, artifacts.printed_ir);
        std::cout << artifacts.printed_ir << '\n';
    } catch (const std::exception& ex) {
        std::cerr << "test0_smoke 失败: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
