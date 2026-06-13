#include "smoke_test_common.h"

#include <iostream>
#include <string_view>
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
            if (inst_ptr->type() == Instruction::Call) {
                calls.push_back(static_cast<const CallInst*>(inst_ptr.get()));
            }
        }
    }
    return calls;
}

const ReturnInst* final_return(const CodeUnit& unit) {
    for (const auto& block_ptr : unit.basic_blocks) {
        if (block_ptr == nullptr || block_ptr->terminator() == nullptr) {
            continue;
        }
        if (block_ptr->terminator()->type() == Instruction::Return) {
            return static_cast<const ReturnInst*>(block_ptr->terminator());
        }
    }
    return nullptr;
}

void require_slot(
    const CodeUnit& unit,
    Slot slot,
    SlotTag tag,
    std::string_view name,
    const char* message) {
    const SlotInfo* info = unit.slot_table.find_slot(slot);
    smoke_test::require(info != nullptr, message);
    smoke_test::require(info->slot.tag == tag, message);
    smoke_test::require(info->name == name, message);
}

void verify_signature(const FunctionUnit& function) {
    smoke_test::require(function.param_slots.size() == 2, "test7 应声明两个输入参数");
    smoke_test::require(function.return_slots.size() == 2, "test7 应声明两个返回值");
    require_slot(function, function.param_slots[0], SlotTag::Arg, "x", "第一个参数应为 x");
    require_slot(function, function.param_slots[1], SlotTag::Arg, "y", "第二个参数应为 y");
    require_slot(
        function,
        function.return_slots[0],
        SlotTag::Ret,
        "sum_value",
        "第一个返回值应为 sum_value");
    require_slot(
        function,
        function.return_slots[1],
        SlotTag::Ret,
        "diff_value",
        "第二个返回值应为 diff_value");
}

void verify_no_placeholder_slot(const FunctionUnit& function) {
    for (const SlotInfo& slot : function.slot_table.slots) {
        smoke_test::require(slot.name != "~", "占位符不应创建名为 ~ 的 slot");
    }
}

void verify_local_signature(const FunctionUnit& function) {
    smoke_test::require(function.param_slots.size() == 2, "pair_ops 应声明两个输入参数");
    smoke_test::require(function.return_slots.size() == 2, "pair_ops 应声明两个返回值");
    require_slot(function, function.param_slots[0], SlotTag::Arg, "a", "pair_ops 第一个参数应为 a");
    require_slot(function, function.param_slots[1], SlotTag::Arg, "b", "pair_ops 第二个参数应为 b");
    require_slot(function, function.return_slots[0], SlotTag::Ret, "s", "pair_ops 第一个返回值应为 s");
    require_slot(function, function.return_slots[1], SlotTag::Ret, "d", "pair_ops 第二个返回值应为 d");
}

void verify_multi_result_call(const FunctionUnit& function, const FunctionUnit& pair_ops) {
    const std::vector<const CallInst*> calls = find_calls(function);
    smoke_test::require(calls.size() == 2, "test7 主函数应包含两条 pair_ops call 指令");
    for (const CallInst* call : calls) {
        smoke_test::require(call->results.size() == 2, "pair_ops 调用应产生两个结果值");
        smoke_test::require(call->arguments.size() == 2, "pair_ops 调用应接收两个参数");
        smoke_test::require(call->dispatch_type == Dynamic, "pair_ops 应保留为动态 direct call");
        smoke_test::require(call->m_function_target == nullptr, "pair_ops call 不应绑定到 local function");
        smoke_test::require(
            call->callee_kind == CallInst::Direct &&
                std::holds_alternative<InternedString>(call->callee) &&
                std::get<InternedString>(call->callee) == "pair_ops",
            "pair_ops call 应保留直接 callee 名字");
    }

    std::size_t stores_to_returns = 0;
    bool placeholder_call_second_result_stored = false;
    smoke_test::require(
        !calls[1]->results[0].is_valid(),
        "占位符对应的 call 结果位应为空");
    smoke_test::require(
        calls[1]->results[1].is_valid(),
        "占位符调用的第二个结果位应有 ValueId");
    for (const auto& block_ptr : function.basic_blocks) {
        for (const auto& inst_ptr : block_ptr->instructions) {
            if (inst_ptr->type() != Instruction::StoreSlot) {
                continue;
            }
            const auto& store = static_cast<const StoreSlotInst&>(*inst_ptr);
            if ((store.slot == function.return_slots[0] &&
                 std::holds_alternative<ValueId>(store.value) &&
                 std::get<ValueId>(store.value) == calls[0]->results[0]) ||
                (store.slot == function.return_slots[1] &&
                 std::holds_alternative<ValueId>(store.value) &&
                 std::get<ValueId>(store.value) == calls[0]->results[1])) {
                ++stores_to_returns;
            }
            if (store.slot == function.return_slots[1] &&
                std::holds_alternative<ValueId>(store.value) &&
                std::get<ValueId>(store.value) == calls[1]->results[1]) {
                placeholder_call_second_result_stored = true;
            }
        }
    }
    smoke_test::require(stores_to_returns == 2, "两个 call 结果都应写回对应返回槽位");
    smoke_test::require(
        placeholder_call_second_result_stored,
        "占位符调用的第二个结果仍应写回 diff_value");
}

void verify_returns(const FunctionUnit& function, const FunctionUnit& pair_ops) {
    const ReturnInst* main_ret = final_return(function);
    smoke_test::require(main_ret != nullptr, "test7 应有返回指令");
    smoke_test::require(main_ret->values.size() == 2, "test7 ret 应返回两个值");
    smoke_test::require(main_ret->values[0].is_valid(), "test7 第一个 ret 值应为 ValueId");
    smoke_test::require(main_ret->values[1].is_valid(), "test7 第二个 ret 值应为 ValueId");

    const ReturnInst* local_ret = final_return(pair_ops);
    smoke_test::require(local_ret != nullptr, "pair_ops 应有返回指令");
    smoke_test::require(local_ret->values.size() == 2, "pair_ops ret 应返回两个值");
    smoke_test::require(local_ret->values[0].is_valid(), "pair_ops 第一个 ret 值应为 ValueId");
    smoke_test::require(local_ret->values[1].is_valid(), "pair_ops 第二个 ret 值应为 ValueId");
}

} // namespace
} // namespace baltam

int main() {
    try {
        const baltam::smoke_test::SmokeArtifacts artifacts =
            baltam::smoke_test::build_ir(TEST7_MFILE_PATH);
        baltam::smoke_test::require_ir_is_complete(artifacts.result);
        baltam::smoke_test::require(artifacts.result.mfile->is_function_file(), "test7 应为函数文件");
        baltam::smoke_test::require(
            artifacts.result.mfile->code_units.size() == 2,
            "test7 文件应包含主函数和一个 local function");

        const baltam::FunctionUnit* function = baltam::find_function(*artifacts.result.mfile, "test7");
        const baltam::FunctionUnit* pair_ops =
            baltam::find_function(*artifacts.result.mfile, "pair_ops");
        baltam::smoke_test::require(function != nullptr, "应找到 test7 函数单元");
        baltam::smoke_test::require(pair_ops != nullptr, "应找到 pair_ops local function");

        baltam::verify_signature(*function);
        baltam::verify_no_placeholder_slot(*function);
        baltam::verify_local_signature(*pair_ops);
        baltam::verify_multi_result_call(*function, *pair_ops);
        baltam::verify_returns(*function, *pair_ops);
    } catch (const std::exception& ex) {
        std::cerr << "test7_smoke 失败: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
