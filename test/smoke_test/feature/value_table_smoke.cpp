#include "ir/ir_builder.h"
#include "smoke_test_common.h"

#include <iostream>
#include <sstream>

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

void verify_value_apply_binds_definition_and_verifies_base_value() {
    IRBuilder builder;
    MFileUnit& mfile = builder.begin_file("value_table_smoke.m");

    ScriptUnit& unit = builder.begin_script_unit("value_table_smoke", SourceSpan::invalid());
    mfile.entry_unit = &unit;
    BasicBlock* entry = unit.create_block("entry", SourceSpan::invalid());
    smoke_test::require(unit.set_entry_block(entry), "应成功设置入口块");
    builder.set_current_unit(&unit);
    builder.set_insert_point(entry);

    const ValueId base = builder.create_value();
    auto base_inst = std::make_unique<ConstInst>();
    base_inst->result = base;
    base_inst->value = Int64Constant{7};
    builder.append_instruction(std::move(base_inst));

    const ValueId argument = builder.create_value();
    auto argument_inst = std::make_unique<ConstInst>();
    argument_inst->result = argument;
    argument_inst->value = Int64Constant{1};
    builder.append_instruction(std::move(argument_inst));

    const ValueId result0 = builder.create_value();
    const ValueId result1 = builder.create_value();
    auto value_apply = std::make_unique<ValueApplyInst>();
    value_apply->base = base;
    value_apply->results = {result0, result1};
    value_apply->arguments.push_back(argument);
    builder.append_instruction(std::move(value_apply));

    auto ret = std::make_unique<ReturnInst>();
    builder.append_instruction(std::move(ret));

    const ValueInfo* result_info0 = unit.value_table.find(result0);
    const ValueInfo* result_info1 = unit.value_table.find(result1);
    smoke_test::require(result_info0 != nullptr && result_info1 != nullptr,
                        "value_apply 多结果应存在于 value_table");
    smoke_test::require(result_info0->def == result_info1->def,
                        "同一 value_apply 的多个结果应指向同一个 def");
    smoke_test::require(result_info0->def != nullptr &&
                            result_info0->def->type() == Instruction::ValueApply,
                        "多结果 def 应指向 value_apply 指令");
    smoke_test::require(result_info0->result_index == 0,
                        "value_apply 第一个结果应记录 result_index 0");
    smoke_test::require(result_info1->result_index == 1,
                        "value_apply 第二个结果应记录 result_index 1");
    smoke_test::require(result_info0->type_fact.is_unknown &&
                            result_info1->type_fact.is_unknown,
                        "value_apply 结果在类型推导前应保持 unknown");

    const IRVerifyResult verify_result = verify_ir(mfile);
    if (!verify_result.ok()) {
        std::ostringstream message;
        message << "value_apply IR 应通过 verifier";
        for (const IRVerifyDiagnostic& diagnostic : verify_result.diagnostics) {
            message << "\n  - " << diagnostic.message;
        }
        smoke_test::fail(message.str());
    }
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

void verify_load_slot_uses_fixed_slot_type() {
    IRBuilder builder;
    MFileUnit& mfile = builder.begin_file("value_table_smoke.m");
    (void)mfile;

    ScriptUnit& unit = builder.begin_script_unit("value_table_smoke", SourceSpan::invalid());
    BasicBlock* entry = unit.create_block("entry", SourceSpan::invalid());
    smoke_test::require(unit.set_entry_block(entry), "应成功设置入口块");
    builder.set_current_unit(&unit);
    builder.set_insert_point(entry);

    const Slot slot = builder.create_slot(
        SlotTag::InternalLocal,
        "__fixed_int64_slot",
        SourceSpan::invalid(),
        SlotValueType::Int64Scalar);
    smoke_test::require(slot.is_valid(), "应成功创建 fixed int64 slot");

    const ValueId loaded = builder.create_value();
    auto load_inst = std::make_unique<LoadSlotInst>();
    load_inst->result = loaded;
    load_inst->slot = slot;
    builder.append_instruction(std::move(load_inst));

    const ValueInfo* loaded_info = unit.value_table.find(loaded);
    smoke_test::require(loaded_info != nullptr, "load_slot 结果应存在于 value_table");
    smoke_test::require(!loaded_info->type_fact.is_unknown,
                        "fixed slot 的 load_slot 结果不应是 unknown");
    smoke_test::require(loaded_info->type_fact.types == TypeSet::int64(),
                        "fixed int64 slot 的 load_slot 结果应为 int64");
    smoke_test::require(loaded_info->type_fact.is_scalar,
                        "fixed int64 slot 的 load_slot 结果应为 scalar");
}

void verify_internal_add_uses_matching_operand_type() {
    IRBuilder builder;
    MFileUnit& mfile = builder.begin_file("value_table_smoke.m");
    (void)mfile;

    ScriptUnit& unit = builder.begin_script_unit("value_table_smoke", SourceSpan::invalid());
    BasicBlock* entry = unit.create_block("entry", SourceSpan::invalid());
    smoke_test::require(unit.set_entry_block(entry), "应成功设置入口块");
    builder.set_current_unit(&unit);
    builder.set_insert_point(entry);

    const ValueId int_lhs = builder.create_value();
    auto int_lhs_inst = std::make_unique<ConstInst>();
    int_lhs_inst->result = int_lhs;
    int_lhs_inst->value = Int64Constant{1};
    builder.append_instruction(std::move(int_lhs_inst));

    const ValueId int_rhs = builder.create_value();
    auto int_rhs_inst = std::make_unique<ConstInst>();
    int_rhs_inst->result = int_rhs;
    int_rhs_inst->value = Int64Constant{2};
    builder.append_instruction(std::move(int_rhs_inst));

    const ValueId int_sum = builder.create_value();
    auto int_add_inst = std::make_unique<BinaryInst>();
    int_add_inst->result = int_sum;
    int_add_inst->op = Add;
    int_add_inst->dispatch_type = Internal;
    int_add_inst->lhs = int_lhs;
    int_add_inst->rhs = int_rhs;
    builder.append_instruction(std::move(int_add_inst));

    const ValueInfo* int_sum_info = unit.value_table.find(int_sum);
    smoke_test::require(int_sum_info != nullptr, "internal add 结果应存在于 value_table");
    smoke_test::require(!int_sum_info->type_fact.is_unknown,
                        "同类型 internal add 结果不应是 unknown");
    smoke_test::require(int_sum_info->type_fact.types == TypeSet::int64(),
                        "int64 + int64 的 internal add 结果应为 int64");
    smoke_test::require(int_sum_info->type_fact.is_scalar,
                        "标量 internal add 结果应保持 scalar");

    const ValueId double_rhs = builder.create_value();
    auto double_rhs_inst = std::make_unique<ConstInst>();
    double_rhs_inst->result = double_rhs;
    double_rhs_inst->value = Float64Constant{3.0};
    builder.append_instruction(std::move(double_rhs_inst));

    const ValueId mixed_sum = builder.create_value();
    auto mixed_add_inst = std::make_unique<BinaryInst>();
    mixed_add_inst->result = mixed_sum;
    mixed_add_inst->op = Add;
    mixed_add_inst->dispatch_type = Internal;
    mixed_add_inst->lhs = int_lhs;
    mixed_add_inst->rhs = double_rhs;
    builder.append_instruction(std::move(mixed_add_inst));

    const ValueInfo* mixed_sum_info = unit.value_table.find(mixed_sum);
    smoke_test::require(mixed_sum_info != nullptr, "mixed internal add 结果应存在于 value_table");
    smoke_test::require(mixed_sum_info->type_fact.is_unknown,
                        "不同类型 internal add 结果应保持 unknown");
}

void verify_internal_switch_match_returns_logical() {
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
    lhs_inst->value = Float64Constant{1.0};
    builder.append_instruction(std::move(lhs_inst));

    const ValueId rhs = builder.create_value();
    auto rhs_inst = std::make_unique<ConstInst>();
    rhs_inst->result = rhs;
    rhs_inst->value = Float64Constant{1.0};
    builder.append_instruction(std::move(rhs_inst));

    const ValueId match = builder.create_value();
    auto match_inst = std::make_unique<CallInst>();
    match_inst->callee_kind = CallInst::Direct;
    match_inst->dispatch_type = Internal;
    match_inst->callee = InternedString("switch_match");
    match_inst->results.push_back(match);
    match_inst->arguments.push_back(lhs);
    match_inst->arguments.push_back(rhs);
    builder.append_instruction(std::move(match_inst));

    const ValueInfo* match_info = unit.value_table.find(match);
    smoke_test::require(match_info != nullptr, "internal switch_match 结果应存在于 value_table");
    smoke_test::require(!match_info->type_fact.is_unknown,
                        "internal switch_match 结果不应是 unknown");
    smoke_test::require(match_info->type_fact.types == TypeSet::logical(),
                        "internal switch_match 结果应为 logical");
    smoke_test::require(match_info->type_fact.is_scalar,
                        "internal switch_match 结果应为 scalar");
}

void verify_create_named_function_handle_type_fact() {
    IRBuilder builder;
    MFileUnit& mfile = builder.begin_file("value_table_smoke.m");

    ScriptUnit& unit = builder.begin_script_unit("value_table_smoke", SourceSpan::invalid());
    mfile.entry_unit = &unit;
    BasicBlock* entry = unit.create_block("entry", SourceSpan::invalid());
    smoke_test::require(unit.set_entry_block(entry), "应成功设置入口块");
    builder.set_current_unit(&unit);
    builder.set_insert_point(entry);

    const ValueId handle = builder.create_value();
    auto handle_inst = std::make_unique<CreateNamedFunctionHandleInst>();
    handle_inst->result = handle;
    handle_inst->name = "sin";
    handle_inst->resolution_mode = CreateNamedFunctionHandleInst::Runtime;
    builder.append_instruction(std::move(handle_inst));

    auto ret = std::make_unique<ReturnInst>();
    builder.append_instruction(std::move(ret));

    const ValueInfo* handle_info = unit.value_table.find(handle);
    smoke_test::require(handle_info != nullptr,
                        "create_named_func_handle 结果应存在于 value_table");
    smoke_test::require(!handle_info->type_fact.is_unknown,
                        "create_named_func_handle 结果不应是 unknown");
    smoke_test::require(handle_info->type_fact.types == TypeSet::function_handle(),
                        "create_named_func_handle 结果应为 function_handle");
    smoke_test::require(handle_info->type_fact.is_scalar,
                        "create_named_func_handle 结果应为 scalar");

    const IRVerifyResult verify_result = verify_ir(mfile);
    if (!verify_result.ok()) {
        std::ostringstream message;
        message << "runtime function handle IR 应通过 verifier";
        for (const IRVerifyDiagnostic& diagnostic : verify_result.diagnostics) {
            message << "\n  - " << diagnostic.message;
        }
        smoke_test::fail(message.str());
    }
}

} // namespace
} // namespace baltam

int main() {
    try {
        baltam::verify_create_value_registers_placeholder();
        baltam::verify_append_instruction_binds_definition();
        baltam::verify_value_apply_binds_definition_and_verifies_base_value();
        baltam::verify_append_instruction_records_simple_type_facts();
        baltam::verify_load_slot_uses_fixed_slot_type();
        baltam::verify_internal_add_uses_matching_operand_type();
        baltam::verify_internal_switch_match_returns_logical();
        baltam::verify_create_named_function_handle_type_fact();
        std::cout << "value_table_smoke passed\n";
    } catch (const std::exception& ex) {
        std::cerr << "value_table_smoke 失败: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
