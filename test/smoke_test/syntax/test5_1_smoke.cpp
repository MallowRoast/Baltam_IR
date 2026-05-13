#include "smoke_test_common.h"

#include <iostream>

namespace baltam {
namespace {

void verify_complete_ir(const IRBuildResult& result) {
    smoke_test::require_ir_is_complete(result);
    smoke_test::require(result.mfile->is_function_file(), "test5_1 应构造成函数文件");
    smoke_test::require(result.mfile->code_units.size() == 1, "test5_1 应只生成主函数代码单元");
    smoke_test::require(
        result.mfile->anonymous_functions.functions.size() == 1,
        "test5_1 应生成一个匿名函数体");
}

void verify_outer_function(const IRBuildResult& result) {
    const CodeUnit* unit = result.mfile->entry_unit;
    smoke_test::require(unit != nullptr && unit->is_function(), "入口应为函数单元");

    smoke_test::require(
        smoke_test::count_instructions(*unit, Instruction::CreateAnonymousFunctionHandle) == 1,
        "外层函数应创建一个匿名函数句柄");
    smoke_test::require(
        smoke_test::count_instructions(*unit, Instruction::ValueApply) == 1,
        "f(x) 应 lowering 成 value_apply");

    const Instruction* create_inst = smoke_test::find_first_instruction(
        *unit,
        Instruction::CreateAnonymousFunctionHandle);
    smoke_test::require(create_inst != nullptr, "应存在 create_anon_func 指令");

    const auto* create =
        static_cast<const CreateAnonymousFunctionHandleInst*>(create_inst);
    smoke_test::require(create->function_id == AnonymousFunctionId(0),
                        "第一个匿名函数 ID 应为 #anon0");
    smoke_test::require(create->captures.size() == 1, "应捕获一个自由变量");
    smoke_test::require(create->captures[0].name == "y", "捕获变量应为 y");
    smoke_test::require(create->captures[0].source_slot.is_valid(),
                        "捕获记录应保存外层 source slot");
    smoke_test::require(create->captures[0].captured_value.is_valid(),
                        "捕获记录应保存外层 captured ValueId");
}

void verify_anonymous_body(const IRBuildResult& result) {
    const auto& anon_functions = result.mfile->anonymous_functions.functions;
    smoke_test::require(!anon_functions.empty() && anon_functions.front() != nullptr,
                        "匿名函数体不能为空");

    const AnonymousFunctionUnit& anon = *anon_functions.front();
    smoke_test::require(anon.id == AnonymousFunctionId(0), "匿名函数体 ID 应为 #anon0");
    smoke_test::require(anon.param_slots.size() == 1, "匿名函数体应有一个参数 slot");
    smoke_test::require(anon.capture_slots.size() == 1, "匿名函数体应有一个 capture slot");
    smoke_test::require(anon.slot_table.find_slot(anon.param_slots[0])->name == "t",
                        "匿名函数参数应为 t");
    smoke_test::require(anon.slot_table.find_slot(anon.capture_slots[0])->name == "y",
                        "匿名函数捕获 slot 应为 y");
    smoke_test::require(
        anon.slot_table.find_slot(anon.capture_slots[0])->type == Slot::Capture,
        "匿名函数捕获 y 应使用 Capture slot");
    smoke_test::require(
        smoke_test::count_instructions(anon, Instruction::LoadSlot) == 2,
        "匿名函数体应读取参数 t 和捕获 y");
    smoke_test::require(
        smoke_test::count_instructions(anon, Instruction::Binary) == 1,
        "匿名函数体应生成 t + y 的 binary");

    const BasicBlock* entry = anon.entry_block;
    smoke_test::require(entry != nullptr && entry->terminator() != nullptr,
                        "匿名函数体应有 terminator");
    smoke_test::require(entry->terminator()->type() == Instruction::Return,
                        "匿名函数体应直接 ret 表达式结果");
    const auto* ret = static_cast<const ReturnInst*>(entry->terminator());
    smoke_test::require(ret->values.size() == 1, "匿名函数体 ret 应返回一个表达式值");
    smoke_test::require(std::holds_alternative<ValueId>(ret->values[0]),
                        "匿名函数体 ret 应返回 ValueId 而不是返回 slot");
}

void verify_printed_ir(const std::string& printed_ir) {
    smoke_test::require(
        printed_ir.find("create_anon_func #anon0 captures { %slot2 }") !=
            std::string::npos,
        "外层 IR 应只打印 create_anon_func 捕获了哪些 slot");
    smoke_test::require(
        printed_ir.find(" from %slot") == std::string::npos,
        "create_anon_func 打印不应展示 capture value 映射细节");
    smoke_test::require(
        printed_ir.find("anon #anon0(%slot0 @t) captures (%slot1 @y) {") !=
            std::string::npos,
        "应打印匿名函数体头部");
    smoke_test::require(
        printed_ir.find("value_apply %") != std::string::npos,
        "应打印 f(x) 的 value_apply");
}

} // namespace
} // namespace baltam

int main() {
    try {
        const baltam::smoke_test::SmokeArtifacts artifacts =
            baltam::smoke_test::build_ir(TEST5_1_MFILE_PATH);
        baltam::verify_complete_ir(artifacts.result);
        baltam::verify_outer_function(artifacts.result);
        baltam::verify_anonymous_body(artifacts.result);
        baltam::verify_printed_ir(artifacts.printed_ir);
        std::cout << artifacts.printed_ir << '\n';
    } catch (const std::exception& ex) {
        std::cerr << "test5_1_smoke 失败: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
