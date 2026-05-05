#include "ir/ir_builder.h"
#include "smoke_test_common.h"

#include <iostream>

namespace baltam {
namespace {

void verify_create_value_registers_placeholder() {
    IRBuilder builder;
    MFileUnit& mfile = builder.begin_file("value_table_smoke.m");
    (void)mfile;

    ScriptUnit& unit = builder.begin_script_unit("value_table_smoke", SourceSpan::invalid());
    BasicBlock* entry = unit.create_block("entry", SourceSpan::invalid());
    smoke_test::require(unit.set_entry_block(entry), "应成功设置入口块");
    builder.set_current_unit(&unit);
    builder.set_insert_point(entry);

    const ValueId value_id = builder.create_value();
    const ValueInfo* value_info = unit.value_table.find(value_id);
    smoke_test::require(value_info != nullptr, "create_value 后应创建占位 ValueInfo");
    smoke_test::require(value_info->value_id == value_id, "占位 ValueInfo 应记录对应 ValueId");
    smoke_test::require(value_info->def == nullptr, "占位 ValueInfo 初始 def 应为空");
    smoke_test::require(value_info->result_index == 0, "单值占位的 result_index 初始应为 0");
}

void verify_append_instruction_binds_definition() {
    IRBuilder builder;
    MFileUnit& mfile = builder.begin_file("value_table_smoke.m");
    (void)mfile;

    ScriptUnit& unit = builder.begin_script_unit("value_table_smoke", SourceSpan::invalid());
    BasicBlock* entry = unit.create_block("entry", SourceSpan::invalid());
    smoke_test::require(unit.set_entry_block(entry), "应成功设置入口块");
    builder.set_current_unit(&unit);
    builder.set_insert_point(entry);

    const ValueId const_result = builder.create_value();
    auto const_inst = std::make_unique<ConstInst>();
    const_inst->result = const_result;
    const_inst->value = Int64Constant{42};
    builder.append_instruction(std::move(const_inst));

    const ValueInfo* const_info = unit.value_table.find(const_result);
    smoke_test::require(const_info != nullptr, "const 结果应存在于 value_table");
    smoke_test::require(const_info->def != nullptr, "const 结果的 def 应已回填");
    smoke_test::require(const_info->def->type() == Instruction::Const, "def 应指向 const 指令");
    smoke_test::require(const_info->result_index == 0, "单结果指令的 result_index 应为 0");
    smoke_test::require(!const_info->type_fact.is_unknown, "const 结果应持有类型事实");
    smoke_test::require(const_info->type_fact.types == TypeSet::int64(), "int64 const 应记录 int64 类型");
    smoke_test::require(const_info->type_fact.is_scalar, "int64 const 应记录标量事实");

    const ValueId call_result0 = builder.create_value();
    const ValueId call_result1 = builder.create_value();
    auto call_inst = std::make_unique<ApplyInst>();
    call_inst->results = {call_result0, call_result1};
    call_inst->callee_or_base = InternedString("foo");
    builder.append_instruction(std::move(call_inst));

    const ValueInfo* call_info0 = unit.value_table.find(call_result0);
    const ValueInfo* call_info1 = unit.value_table.find(call_result1);
    smoke_test::require(call_info0 != nullptr && call_info1 != nullptr, "多结果值应存在于 value_table");
    smoke_test::require(call_info0->def == call_info1->def, "同一 apply 的多个结果应指向同一个 def");
    smoke_test::require(call_info0->def != nullptr, "apply 结果的 def 应已回填");
    smoke_test::require(call_info0->def->type() == Instruction::Apply, "多结果 def 应指向 apply 指令");
    smoke_test::require(call_info0->result_index == 0, "第一个结果应记录 result_index 0");
    smoke_test::require(call_info1->result_index == 1, "第二个结果应记录 result_index 1");
    smoke_test::require(call_info0->type_fact.is_unknown, "apply 结果在类型推导前应保持 unknown");
    smoke_test::require(!call_info0->type_fact.is_scalar, "apply 结果不应断言为标量");
    smoke_test::require(call_info1->type_fact.is_unknown, "apply 的全部结果在类型推导前都应保持 unknown");
}

void verify_append_instruction_records_simple_type_facts() {
    IRBuilder builder;
    MFileUnit& mfile = builder.begin_file("value_table_smoke.m");
    (void)mfile;

    ScriptUnit& unit = builder.begin_script_unit("value_table_smoke", SourceSpan::invalid());
    BasicBlock* entry = unit.create_block("entry", SourceSpan::invalid());
    smoke_test::require(unit.set_entry_block(entry), "应成功设置入口块");
    builder.set_current_unit(&unit);
    builder.set_insert_point(entry);

    const ValueId lhs = builder.create_value();
    auto lhs_inst = std::make_unique<ConstInst>();
    lhs_inst->result = lhs;
    lhs_inst->value = Int64Constant{1};
    builder.append_instruction(std::move(lhs_inst));

    const ValueId rhs = builder.create_value();
    auto rhs_inst = std::make_unique<ConstInst>();
    rhs_inst->result = rhs;
    rhs_inst->value = Int64Constant{2};
    builder.append_instruction(std::move(rhs_inst));

    const ValueId sum = builder.create_value();
    auto add_inst = std::make_unique<BinaryInst>();
    add_inst->result = sum;
    add_inst->op = Add;
    add_inst->lhs = lhs;
    add_inst->rhs = rhs;
    builder.append_instruction(std::move(add_inst));

    const ValueInfo* sum_info = unit.value_table.find(sum);
    smoke_test::require(sum_info != nullptr, "binary 结果应存在于 value_table");
    smoke_test::require(sum_info->type_fact.is_unknown, "未静态分派的 binary 结果在类型推导前应保持 unknown");
    smoke_test::require(!sum_info->type_fact.is_scalar, "未静态分派的 binary 结果不应断言为标量");

    const ValueId comparison = builder.create_value();
    auto cmp_inst = std::make_unique<BinaryInst>();
    cmp_inst->result = comparison;
    cmp_inst->op = Gt;
    cmp_inst->lhs = sum;
    cmp_inst->rhs = rhs;
    builder.append_instruction(std::move(cmp_inst));

    const ValueInfo* comparison_info = unit.value_table.find(comparison);
    smoke_test::require(comparison_info != nullptr, "comparison 结果应存在于 value_table");
    smoke_test::require(comparison_info->type_fact.is_unknown, "未静态分派的比较结果在类型推导前应保持 unknown");
    smoke_test::require(!comparison_info->type_fact.is_scalar, "未静态分派的比较结果不应断言为标量");

    const ValueId copied = builder.create_value();
    auto copy_inst = std::make_unique<CopyInst>();
    copy_inst->result = copied;
    copy_inst->value = comparison;
    builder.append_instruction(std::move(copy_inst));

    const ValueInfo* copied_info = unit.value_table.find(copied);
    smoke_test::require(copied_info != nullptr, "copy 结果应存在于 value_table");
    smoke_test::require(copied_info->type_fact.is_unknown, "copy 应复制输入 unknown 状态");
    smoke_test::require(!copied_info->type_fact.is_scalar, "copy 应复制输入标量事实");
}

} // namespace
} // namespace baltam

int main() {
    try {
        baltam::verify_create_value_registers_placeholder();
        baltam::verify_append_instruction_binds_definition();
        baltam::verify_append_instruction_records_simple_type_facts();
        std::cout << "value_table_smoke passed\n";
    } catch (const std::exception& ex) {
        std::cerr << "value_table_smoke 失败: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
