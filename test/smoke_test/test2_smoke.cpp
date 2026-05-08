#include "smoke_test_common.h"

#include <iostream>
#include <string_view>
#include <variant>

namespace baltam {
namespace {

const BasicBlock* find_block_by_label(const CodeUnit& unit, std::string_view label) {
    for (const auto& block_ptr : unit.basic_blocks) {
        if (block_ptr != nullptr && block_ptr->label == label) {
            return block_ptr.get();
        }
    }
    return nullptr;
}

std::size_t count_direct_calls(const CodeUnit& unit, std::string_view callee) {
    std::size_t count = 0;
    for (const auto& block_ptr : unit.basic_blocks) {
        if (block_ptr == nullptr) {
            continue;
        }
        for (const auto& inst_ptr : block_ptr->instructions) {
            if (inst_ptr == nullptr || inst_ptr->type() != Instruction::Call) {
                continue;
            }
            const auto& call = static_cast<const CallInst&>(*inst_ptr);
            if (call.callee_kind == CallInst::Direct &&
                std::holds_alternative<InternedString>(call.callee) &&
                std::get<InternedString>(call.callee) == callee) {
                ++count;
            }
        }
    }
    return count;
}

std::size_t count_internal_calls(const CodeUnit& unit, std::string_view callee) {
    std::size_t count = 0;
    for (const auto& block_ptr : unit.basic_blocks) {
        if (block_ptr == nullptr) {
            continue;
        }
        for (const auto& inst_ptr : block_ptr->instructions) {
            if (inst_ptr == nullptr || inst_ptr->type() != Instruction::Call) {
                continue;
            }
            const auto& call = static_cast<const CallInst&>(*inst_ptr);
            if (call.dispatch_type == Internal &&
                call.callee_kind == CallInst::Direct &&
                std::holds_alternative<InternedString>(call.callee) &&
                std::get<InternedString>(call.callee) == callee) {
                ++count;
            }
        }
    }
    return count;
}

void verify_complete_ir(const IRBuildResult& result) {
    smoke_test::require_ir_is_complete(result);
    smoke_test::require(result.mfile->is_script_file(), "test2 应构造成脚本文件");
    smoke_test::require(result.mfile->code_units.size() == 1, "test2 只应生成脚本单元");
    smoke_test::require(result.mfile->file_stem() == "test2", "文件 stem 应为 test2");
}

void verify_for_cfg(const CodeUnit& unit) {
    const BasicBlock* preheader = find_block_by_label(unit, "for.preheader");
    const BasicBlock* header = find_block_by_label(unit, "for.header");
    const BasicBlock* body = find_block_by_label(unit, "for.body");
    const BasicBlock* latch = find_block_by_label(unit, "for.latch");
    const BasicBlock* end = find_block_by_label(unit, "for.end");

    smoke_test::require(preheader != nullptr, "for 应生成 preheader 块");
    smoke_test::require(header != nullptr, "for 应生成 header 块");
    smoke_test::require(body != nullptr, "for 应生成 body 块");
    smoke_test::require(latch != nullptr, "for 应生成 latch 块");
    smoke_test::require(end != nullptr, "for 应生成 end 块");

    smoke_test::require(unit.basic_blocks.size() == 6, "test2 应生成 entry + 5 个 for 基本块");
    smoke_test::require(unit.entry_block->terminator()->type() == Instruction::Goto,
                        "entry 应跳到 for.preheader");
    smoke_test::require(preheader->terminator()->type() == Instruction::Goto,
                        "for.preheader 应跳到 for.header");
    smoke_test::require(header->terminator()->type() == Instruction::Branch,
                        "for.header 应以条件分支结束");
    smoke_test::require(body->terminator()->type() == Instruction::Goto,
                        "for.body 应跳到 for.latch");
    smoke_test::require(latch->terminator()->type() == Instruction::Goto,
                        "for.latch 应回跳 for.header");
    smoke_test::require(end->terminator()->type() == Instruction::Return,
                        "for.end 应接隐式 return");
}

void verify_for_slots(const CodeUnit& unit) {
    const Slot* iter_index_slot = nullptr;
    for (const Slot& slot : unit.slot_table.slots) {
        if (slot.name == "__foreach_iter_index") {
            iter_index_slot = &slot;
            break;
        }
    }

    smoke_test::require(iter_index_slot != nullptr,
                        "for 应创建 __foreach_iter_index slot");
    smoke_test::require(iter_index_slot->type == Slot::InternalLocal,
                        "__foreach_iter_index 应是 internal_local slot");
    smoke_test::require(iter_index_slot->attrs.fixed_type == SlotAttrs::Int64Scalar,
                        "__foreach_iter_index 类型应固定为 int64 scalar");
    smoke_test::require(iter_index_slot->attrs.hidden_role == SlotAttrs::None,
                        "__foreach_iter_index 不应占用 hidden_role");
}

void verify_core_focus(const IRBuildResult& result) {
    const CodeUnit* entry = result.mfile->entry_unit;
    smoke_test::require(entry != nullptr && entry->is_script(), "入口应为脚本单元");

    verify_for_cfg(*entry);
    verify_for_slots(*entry);

    smoke_test::require(count_direct_calls(*entry, "colon") == 1,
                        "1:10 应 lower 成一次 colon call");
    smoke_test::require(count_internal_calls(*entry, "foreach_init") == 1,
                        "for 应 lower 出一次 internal.foreach_init");
    smoke_test::require(count_internal_calls(*entry, "foreach_iterate") == 1,
                        "for 应 lower 出一次 internal.foreach_iterate");
    smoke_test::require(smoke_test::count_instructions(*entry, Instruction::Binary) == 3,
                        "test2 应包含 header 内部比较、用户循环体加法和 latch 内部自增");
}

void verify_printed_ir(const std::string& printed_ir) {
    smoke_test::require(
        printed_ir.find("; mfile \"" TEST2_MFILE_PATH "\"") != std::string::npos,
        "应打印 test2 文件头");
    smoke_test::require(
        printed_ir.find("script @test2 {") != std::string::npos,
        "应打印 test2 脚本头");
    smoke_test::require(
        printed_ir.find("for.preheader:") != std::string::npos &&
            printed_ir.find("for.header:") != std::string::npos &&
            printed_ir.find("for.body:") != std::string::npos &&
            printed_ir.find("for.latch:") != std::string::npos &&
            printed_ir.find("for.end:") != std::string::npos,
        "应打印 for 的五个基本块");
    smoke_test::require(
        printed_ir.find("call @colon") != std::string::npos,
        "应打印 colon call");
    smoke_test::require(
        printed_ir.find("@internal.colon") == std::string::npos,
        "colon 不应打印为 internal 静态分派目标");
    smoke_test::require(
        printed_ir.find("call @internal.foreach_init") != std::string::npos,
        "应打印 internal.foreach_init 调用");
    smoke_test::require(
        printed_ir.find("([%4, extern], [%5, int64]) = call @internal.foreach_init") != std::string::npos,
        "foreach_init 应打印静态内部返回类型");
    smoke_test::require(
        printed_ir.find("internal_local @__foreach_iter_index : int64") != std::string::npos,
        "iter_index slot 应打印为 internal_local fixed int64");
    smoke_test::require(
        printed_ir.find("call @internal.foreach_iterate") != std::string::npos,
        "应打印 internal.foreach_iterate 调用");
    smoke_test::require(
        printed_ir.find("internal.cmp_gt") != std::string::npos &&
            printed_ir.find("internal.add") != std::string::npos,
        "应打印内部静态分派运算");
    smoke_test::require(
        printed_ir.find("@__foreach_state") == std::string::npos &&
            printed_ir.find("@__foreach_max_iter") == std::string::npos,
        "state/max_iter 不应再落到 slot");
    smoke_test::require(
        printed_ir.find("store_env %test2_env, @s") != std::string::npos,
        "循环体应写回 workspace 变量 s");
}

} // namespace
} // namespace baltam

int main() {
    try {
        const baltam::smoke_test::SmokeArtifacts artifacts =
            baltam::smoke_test::build_ir(TEST2_MFILE_PATH);
        baltam::verify_complete_ir(artifacts.result);
        baltam::verify_core_focus(artifacts.result);
        baltam::verify_printed_ir(artifacts.printed_ir);
        std::cout << artifacts.printed_ir << '\n';
    } catch (const std::exception& ex) {
        std::cerr << "test2_smoke 失败: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
