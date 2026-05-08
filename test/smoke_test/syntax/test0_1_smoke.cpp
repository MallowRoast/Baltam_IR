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
    smoke_test::require(
        unit->find_hidden_slot(SlotAttrs::WorkspaceHandle) == nullptr,
        "函数 lowering 不应创建环境槽位");
    smoke_test::require(unit->slot_table.slots.size() == 3, "函数 lowering 应创建 c/a/b 三个槽位");

    const auto* function = static_cast<const FunctionUnit*>(unit);
    smoke_test::require(function->param_slots.empty(), "test0_1 不应声明输入参数");
    smoke_test::require(function->return_slots.size() == 1, "test0_1 应声明一个返回槽位");
    smoke_test::require(
        unit->slot_table.slots[0].type == Slot::Ret && unit->slot_table.slots[0].name == "c",
        "第一个槽位应为返回槽位 c");
    smoke_test::require(
        unit->slot_table.slots[1].type == Slot::Local && unit->slot_table.slots[1].name == "a",
        "第二个槽位应为局部槽位 a");
    smoke_test::require(
        unit->slot_table.slots[2].type == Slot::Local && unit->slot_table.slots[2].name == "b",
        "第三个槽位应为局部槽位 b");

    smoke_test::require(
        smoke_test::count_instructions(*unit, Instruction::LoadSlot) == 3 &&
            smoke_test::count_instructions(*unit, Instruction::StoreSlot) == 4,
        "函数 lowering 应生成 slot 读写");
    smoke_test::require(
        smoke_test::count_instructions(*unit, Instruction::LoadWorkspace) == 0 &&
            smoke_test::count_instructions(*unit, Instruction::StoreWorkspace) == 0,
        "函数 lowering 不应生成 workspace 读写");
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

void verify_printed_ir(const std::string& printed_ir) {
    smoke_test::require(!printed_ir.empty(), "打印结果不能为空");
    smoke_test::require(
        printed_ir.find("; mfile \"" TEST0_1_MFILE_PATH "\"") != std::string::npos,
        "应打印文件头");
    smoke_test::require(
        printed_ir.find("define @test0_1() -> (%slot0 @c) {") != std::string::npos,
        "应打印函数头");

    const std::string ret_slot_line =
        smoke_test::find_line_containing(printed_ir, "%slot0 = ret @c");
    smoke_test::require(!ret_slot_line.empty(), "应打印返回槽位 c");
    smoke_test::require(ret_slot_line.find("; ") == std::string::npos, "slot 行不应打印源码注释");

    const std::string call_line =
        smoke_test::find_line_containing(printed_ir, "[%3, unknown] = call @sin(%4)");
    smoke_test::require(!call_line.empty(), "应打印 sin 的 call");
    smoke_test::require(call_line.find("; ") == std::string::npos, "call 行不应打印源码注释");

    const std::string store_a_line =
        smoke_test::find_line_containing(printed_ir, "store_slot %slot1, %2");
    smoke_test::require(!store_a_line.empty(), "应打印 a 的槽位写回");
    smoke_test::require(
        store_a_line.find("; line ") != std::string::npos &&
            store_a_line.find("a = 1 + 2;") != std::string::npos,
        "a 的写回行应打印源码行号注释");

    smoke_test::require(
        printed_ir.find("apply @sin") == std::string::npos,
        "函数 IR 不应把 sin 打印成 apply");
    smoke_test::require(
        printed_ir.find("load_env") == std::string::npos &&
            printed_ir.find("store_env") == std::string::npos,
        "函数 IR 不应打印 workspace 读写");
}

void verify_other_important_checks(
    const IRBuildResult& result,
    const std::string& printed_ir) {
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
    smoke_test::require(
        std::holds_alternative<SlotId>(ret->values[0]) &&
            std::get<SlotId>(ret->values[0]) == unit->slot_table.slots[0].slot_id,
        "函数隐式 ret 应返回槽位 c");

    const std::string branch_line = smoke_test::find_line_containing(
        printed_ir,
        "br %7, label %if.then, label %if.else");
    smoke_test::require(!branch_line.empty(), "应打印 if 条件分支");
    smoke_test::require(
        branch_line.find("; line ") != std::string::npos &&
            branch_line.find("b > 0") != std::string::npos,
        "条件分支行应打印源码行号注释");
    smoke_test::require(
        printed_ir.find("ret %slot0") != std::string::npos,
        "函数 IR 应打印返回槽位 ret");
}

} // namespace
} // namespace baltam

int main() {
    try {
        const baltam::smoke_test::SmokeArtifacts artifacts =
            baltam::smoke_test::build_ir(TEST0_1_MFILE_PATH);
        baltam::verify_complete_ir(artifacts.result);
        baltam::verify_core_focus(artifacts.result);
        baltam::verify_printed_ir(artifacts.printed_ir);
        baltam::verify_other_important_checks(artifacts.result, artifacts.printed_ir);
        std::cout << artifacts.printed_ir << '\n';
    } catch (const std::exception& ex) {
        std::cerr << "test0_1_smoke 失败: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
