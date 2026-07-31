#include "smoke_test_common.h"

#include <cstdint>
#include <iostream>
#include <string_view>
#include <variant>
#include <vector>

namespace baltam {
namespace {

const FunctionUnit* find_function(const MFileUnit& mfile, std::string_view name) {
    for (const auto& unit_ptr : mfile.code_units) {
        if (unit_ptr != nullptr && unit_ptr->is_function() && unit_ptr->name == name) {
            return static_cast<const FunctionUnit*>(unit_ptr.get());
        }
    }
    return nullptr;
}

std::vector<const CallInst*> find_calls(const CodeUnit& unit) {
    std::vector<const CallInst*> calls;
    for (const auto& block_ptr : unit.basic_blocks) {
        for (const auto& inst_ptr : block_ptr->instructions) {
            if (inst_ptr != nullptr && inst_ptr->type() == Instruction::Call) {
                calls.push_back(static_cast<const CallInst*>(inst_ptr.get()));
            }
        }
    }
    return calls;
}

std::vector<const CallInst*> find_internal_calls(
    const CodeUnit& unit,
    std::string_view name) {
    std::vector<const CallInst*> calls;
    for (const CallInst* call : find_calls(unit)) {
        if (call->dispatch_type == Internal &&
            call->callee_kind == CallInst::Direct &&
            std::holds_alternative<InternedString>(call->callee) &&
            std::get<InternedString>(call->callee) == name) {
            calls.push_back(call);
        }
    }
    return calls;
}

void verify_signature(const FunctionUnit& function) {
    smoke_test::require(function.param_slots.size() == 2, "test8 应声明两个输入参数");
    smoke_test::require(function.return_slots.size() == 1, "test8 应声明一个返回值");
    const SlotInfo* param_a = function.slot_table.find_slot(function.param_slots[0]);
    const SlotInfo* param_fun = function.slot_table.find_slot(function.param_slots[1]);
    const SlotInfo* ret = function.slot_table.find_slot(function.return_slots[0]);
    smoke_test::require(param_a != nullptr && param_a->slot.tag == SlotTag::Arg && param_a->name == "A",
                        "test8 第一个参数应为 A");
    smoke_test::require(
        param_fun != nullptr && param_fun->slot.tag == SlotTag::Arg && param_fun->name == "fun",
        "test8 第二个参数应为 fun");
    smoke_test::require(ret != nullptr && ret->slot.tag == SlotTag::Ret && ret->name == "y",
                        "test8 返回值应为 y");
}

std::vector<const MagicEndInst*> find_magic_ends(const CodeUnit& unit) {
    std::vector<const MagicEndInst*> magic_ends;
    for (const auto& block_ptr : unit.basic_blocks) {
        for (const auto& inst_ptr : block_ptr->instructions) {
            if (inst_ptr != nullptr && inst_ptr->type() == Instruction::MagicEnd) {
                magic_ends.push_back(static_cast<const MagicEndInst*>(inst_ptr.get()));
            }
        }
    }
    return magic_ends;
}

void require_magic_end_result_type(const CodeUnit& unit, const MagicEndInst& inst) {
    const ValueInfo* result_info = unit.value_table.find(inst.result);
    smoke_test::require(result_info != nullptr, "magic_end 结果应进入 value_table");
    smoke_test::require(result_info->type_fact.is_unknown,
                        "magic_end 结果类型应保持 unknown");
}

void require_value_loads_slot(
    const FunctionUnit& function,
    const Operand& operand,
    Slot slot,
    const char* message) {
    const auto* value = std::get_if<ValueId>(&operand);
    smoke_test::require(value != nullptr && value->is_valid(), message);

    const ValueInfo* value_info = function.value_table.find(*value);
    smoke_test::require(value_info != nullptr && value_info->def != nullptr, message);
    smoke_test::require(value_info->def->type() == Instruction::LoadSlot, message);

    const auto& load = static_cast<const LoadSlotInst&>(*value_info->def);
    smoke_test::require(load.slot == slot, message);
}

void verify_context_loads_slot(
    const FunctionUnit& function,
    const MagicEndInst::MagicEndContext& context,
    Slot slot,
    std::uint32_t dim,
    std::uint32_t nindices,
    const char* message) {
    require_value_loads_slot(function, context.callee_or_base, slot, message);
    smoke_test::require(context.dim == dim, message);
    smoke_test::require(context.nindices == nindices, message);
}

void verify_magic_end_insts(const FunctionUnit& function) {
    const std::vector<const MagicEndInst*> magic_ends = find_magic_ends(function);
    smoke_test::require(magic_ends.size() == 7, "test8 应生成七次 magic_end");
    smoke_test::require(
        find_internal_calls(function, "end_index").empty(),
        "基础 lowering 不应直接生成 internal.end_index");

    std::size_t single_dim_count = 0;
    std::size_t row_dim_count = 0;
    std::size_t col_dim_count = 0;
    std::size_t nested_count = 0;
    for (const MagicEndInst* magic_end : magic_ends) {
        require_magic_end_result_type(function, *magic_end);
        smoke_test::require(!magic_end->candidate_contexts.empty(),
                            "magic_end 应至少保留一层候选上下文");

        if (magic_end->candidate_contexts.size() == 2U) {
            ++nested_count;
            verify_context_loads_slot(
                function,
                magic_end->candidate_contexts[0],
                function.param_slots[1],
                1,
                1,
                "A(fun(end)) 的第一候选应是内层 fun(end)");
            verify_context_loads_slot(
                function,
                magic_end->candidate_contexts[1],
                function.param_slots[0],
                1,
                1,
                "A(fun(end)) 的第二候选应是外层 A(...)");
            continue;
        }

        smoke_test::require(magic_end->candidate_contexts.size() == 1U,
                            "非嵌套 magic_end 应只保留单层候选上下文");
        const MagicEndInst::MagicEndContext& context = magic_end->candidate_contexts.front();
        require_value_loads_slot(
            function,
            context.callee_or_base,
            function.param_slots[0],
            "单层 magic_end 应绑定到参数 A 的索引上下文");

        if (context.dim == 1U && context.nindices == 1U) {
            ++single_dim_count;
        } else if (context.dim == 1U && context.nindices == 2U) {
            ++row_dim_count;
        } else if (context.dim == 2U && context.nindices == 2U) {
            ++col_dim_count;
        } else {
            smoke_test::fail("magic_end 维度参数组合不符合 test8 预期");
        }
    }

    smoke_test::require(single_dim_count == 4, "单维单候选 end 应出现四次");
    smoke_test::require(row_dim_count == 1, "二维第一维 end 应出现一次");
    smoke_test::require(col_dim_count == 1, "二维第二维 end 应出现一次");
    smoke_test::require(nested_count == 1, "A(fun(end)) 应生成一次双候选 magic_end");
}

void verify_index_shape(const FunctionUnit& function) {
    smoke_test::require(
        smoke_test::count_instructions(function, Instruction::ValueApply) == 7,
        "七个索引读取应 lower 成 value_apply");
    smoke_test::require(
        find_internal_calls(function, "paren_assign").size() == 1,
        "索引赋值应 lower 成一次 internal.paren_assign");
    smoke_test::require(
        find_internal_calls(function, "end").empty(),
        "magic end 不应 lower 成 direct internal.end 调用");
}

} // namespace
} // namespace baltam

int main() {
    try {
        const baltam::IRBuildResult ir = baltam::smoke_test::build_ir(TEST8_MFILE_PATH);
        baltam::smoke_test::require_ir_is_complete(ir);
        baltam::smoke_test::require(ir.mfile->is_function_file(), "test8 应为函数文件");
        baltam::smoke_test::require(
            ir.mfile->code_units.size() == 1,
            "test8 文件应只包含主函数单元");

        const baltam::FunctionUnit* function =
            baltam::find_function(*ir.mfile, "test8");
        baltam::smoke_test::require(function != nullptr, "应找到 test8 函数单元");

        baltam::verify_signature(*function);
        baltam::verify_magic_end_insts(*function);
        baltam::verify_index_shape(*function);
    } catch (const std::exception& ex) {
        std::cerr << "test8_smoke 失败: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
