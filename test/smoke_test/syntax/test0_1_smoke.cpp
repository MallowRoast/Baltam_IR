#include "smoke_test_common.h"

#include <iostream>

namespace baltam {
namespace {

void verify_complete_ir(const IRBuildResult& result) {
    smoke_test::require_ir_is_complete(result);
    smoke_test::require(result.mfile->is_function_file(), "test0_1 应构造成函数文件");
    smoke_test::require(result.mfile->code_units.size() == 1, "应只生成一个代码单元");
    smoke_test::require(result.mfile->file_stem() == "test0_1", "文件 stem 应为 test0_1");
}

void verify_core_focus(const IRBuildResult& result) {
    const CodeUnit* unit = result.mfile->entry_unit;
    smoke_test::require(unit != nullptr, "入口代码单元不能为空");
    smoke_test::require(unit->slot_table.slots.size() == 3, "函数 lowering 应创建 c/a/b 三个槽位");

    const auto* function = static_cast<const FunctionUnit*>(unit);
    smoke_test::require(function->param_slots.empty(), "test0_1 不应声明输入参数");
    smoke_test::require(function->return_slots.size() == 1, "test0_1 应声明一个返回槽位");
    smoke_test::require(
        unit->slot_table.slots[0].slot.tag == SlotTag::Ret && unit->slot_table.slots[0].name == "c",
        "第一个槽位应为返回槽位 c");
    smoke_test::require(
        unit->slot_table.slots[1].slot.tag == SlotTag::Local && unit->slot_table.slots[1].name == "a",
        "第二个槽位应为局部槽位 a");
    smoke_test::require(
        unit->slot_table.slots[2].slot.tag == SlotTag::Local && unit->slot_table.slots[2].name == "b",
        "第三个槽位应为局部槽位 b");

    smoke_test::require(
        smoke_test::count_instructions(*unit, Instruction::LoadSlot) == 4 &&
            smoke_test::count_instructions(*unit, Instruction::StoreSlot) == 4,
        "函数 lowering 应生成 slot 读写");
    smoke_test::require(
        smoke_test::find_slot_by_tag(*unit, SlotTag::ScriptVar) == nullptr,
        "函数 lowering 不应创建脚本变量槽位");
    smoke_test::require(
        smoke_test::count_instructions(*unit, Instruction::Apply) == 0,
        "函数 lowering 不应生成 apply");
    smoke_test::require(
        smoke_test::count_instructions(*unit, Instruction::Call) == 1,
        "函数 lowering 应生成一条 call");

    const Instruction* call_inst =
        smoke_test::find_first_instruction(*unit, Instruction::Call);
    smoke_test::require(call_inst != nullptr, "函数中应存在 call 指令");

    const auto* call = static_cast<const CallInst*>(call_inst);
    smoke_test::require(call->results.size() == 1, "函数中的 call 应产生一个结果");
    smoke_test::require(call->arguments.size() == 1, "函数中的 call 应只有一个参数");
    smoke_test::require(
        call->callee_kind == CallInst::Direct &&
            std::holds_alternative<InternedString>(call->callee) &&
            std::get<InternedString>(call->callee) == "sin",
        "函数中的 sin(a) 应 lowering 成 direct call");
}

void verify_other_important_checks(const IRBuildResult& result) {
    const CodeUnit* unit = result.mfile->entry_unit;
    smoke_test::require(unit->basic_blocks.size() == 4, "if/else 函数应生成 4 个基本块");
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

    const auto* ret =
        static_cast<const ReturnInst*>(unit->basic_blocks[3]->terminator());
    smoke_test::require(ret->values.size() == 1, "函数 ret 应返回一个值");
    smoke_test::require(ret->values[0].is_valid(), "函数隐式 ret 应返回读取返回槽位后的 ValueId");
}

} // namespace
} // namespace baltam

int main() {
    try {
        const baltam::smoke_test::SmokeArtifacts artifacts =
            baltam::smoke_test::build_ir(TEST0_1_MFILE_PATH);
        baltam::verify_complete_ir(artifacts.result);
        baltam::verify_core_focus(artifacts.result);
        baltam::verify_other_important_checks(artifacts.result);
    } catch (const std::exception& ex) {
        std::cerr << "test0_1_smoke 失败: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
