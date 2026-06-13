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
    const SlotInfo* iter_index_slot =
        smoke_test::find_slot_by_name(unit, "__for_idx");

    smoke_test::require(iter_index_slot != nullptr,
                        "for 应创建 __for_idx slot");
    smoke_test::require(iter_index_slot->slot.tag == SlotTag::InternalLocal,
                        "__for_idx 应是 internal_local slot");
    smoke_test::require(iter_index_slot->value_type == SlotValueType::Int64Scalar,
                        "__for_idx 类型应固定为 int64 scalar");
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

} // namespace
} // namespace baltam

int main() {
    try {
        const baltam::smoke_test::SmokeArtifacts artifacts =
            baltam::smoke_test::build_ir(TEST2_MFILE_PATH);
        baltam::verify_complete_ir(artifacts.result);
        baltam::verify_core_focus(artifacts.result);
    } catch (const std::exception& ex) {
        std::cerr << "test2_smoke 失败: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
