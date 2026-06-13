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
    smoke_test::require(unit->slot_table.slots.size() == 3, "脚本 lowering 应创建 a/b/c 三个脚本槽位");
    smoke_test::require(
        smoke_test::find_slot_by_name(*unit, "a") != nullptr &&
            smoke_test::find_slot_by_name(*unit, "b") != nullptr &&
            smoke_test::find_slot_by_name(*unit, "c") != nullptr,
        "脚本 lowering 应为静态出现的变量创建 slot");

    smoke_test::require(
        smoke_test::count_instructions(*unit, Instruction::LoadSlot) == 3,
        "脚本 lowering 应生成 3 条 load");
    smoke_test::require(
        smoke_test::count_instructions(*unit, Instruction::StoreSlot) == 4,
        "脚本 lowering 应生成 4 条 store");
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

void verify_other_important_checks(const IRBuildResult& result) {
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
        baltam::verify_other_important_checks(artifacts.result);
    } catch (const std::exception& ex) {
        std::cerr << "test0_smoke 失败: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
