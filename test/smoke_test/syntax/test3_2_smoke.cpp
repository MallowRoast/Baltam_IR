#include "smoke_test_common.h"

#include <cstddef>
#include <iostream>
#include <string_view>
#include <variant>

namespace baltam {
namespace {

std::size_t count_blocks_by_label(const CodeUnit& unit, std::string_view label) {
    std::size_t count = 0;
    for (const auto& block_ptr : unit.basic_blocks) {
        if (block_ptr != nullptr && block_ptr->label == label) {
            ++count;
        }
    }
    return count;
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

std::size_t count_internal_iter_index_slots(const CodeUnit& unit) {
    std::size_t count = 0;
    for (const SlotInfo& slot : unit.slot_table.slots) {
        if (slot.name == "__for_idx" &&
            slot.slot.tag == SlotTag::InternalLocal &&
            slot.value_type == SlotValueType::Int64Scalar) {
            ++count;
        }
    }
    return count;
}

bool has_synthetic_goto_between_labels(
    const CodeUnit& unit,
    std::string_view source_label,
    std::string_view target_label) {
    for (const auto& block_ptr : unit.basic_blocks) {
        if (block_ptr == nullptr || block_ptr->label != source_label) {
            continue;
        }
        const Instruction* terminator = block_ptr->terminator();
        if (terminator == nullptr || terminator->type() != Instruction::Goto) {
            continue;
        }
        const auto& go = static_cast<const GotoInst&>(*terminator);
        if (go.attrs.is_synthetic != 0 &&
            go.target != nullptr &&
            go.target->label == target_label) {
            return true;
        }
    }
    return false;
}

void verify_complete_ir(const IRBuildResult& result) {
    smoke_test::require_ir_is_complete(result);
    smoke_test::require(result.mfile->is_script_file(), "test3_2 应构造成脚本文件");
    smoke_test::require(result.mfile->code_units.size() == 1, "test3_2 只应生成脚本单元");
    smoke_test::require(result.mfile->file_stem() == "test3_2", "文件 stem 应为 test3_2");
}

void verify_core_focus(const IRBuildResult& result) {
    const CodeUnit* entry = result.mfile->entry_unit;
    smoke_test::require(entry != nullptr && entry->is_script(), "入口应为脚本单元");

    smoke_test::require(entry->basic_blocks.size() == 17,
                        "test3_2 应生成 entry + 两套 for 五块 CFG + 两套 while 三块 CFG");
    smoke_test::require(count_blocks_by_label(*entry, "for.preheader") == 2,
                        "应生成两套 for.preheader");
    smoke_test::require(count_blocks_by_label(*entry, "for.header") == 2,
                        "应生成两套 for.header");
    smoke_test::require(count_blocks_by_label(*entry, "for.body") == 2,
                        "应生成两套 for.body");
    smoke_test::require(count_blocks_by_label(*entry, "for.latch") == 2,
                        "应生成两套 for.latch");
    smoke_test::require(count_blocks_by_label(*entry, "for.end") == 2,
                        "应生成两套 for.end");
    smoke_test::require(count_blocks_by_label(*entry, "while.header") == 2,
                        "应生成两套 while.header");
    smoke_test::require(count_blocks_by_label(*entry, "while.body") == 2,
                        "应生成两套 while.body");
    smoke_test::require(count_blocks_by_label(*entry, "while.end") == 2,
                        "应生成两套 while.end");

    smoke_test::require(count_direct_calls(*entry, "colon") == 2,
                        "两层 for 的范围表达式应 lower 成两次 colon call");
    smoke_test::require(count_internal_calls(*entry, "foreach_init") == 2,
                        "两层 for 应 lower 出两次 internal.foreach_init");
    smoke_test::require(count_internal_calls(*entry, "foreach_iterate") == 2,
                        "两层 for 应 lower 出两次 internal.foreach_iterate");
    smoke_test::require(count_internal_iter_index_slots(*entry) == 2,
                        "两层 for 应创建两个独立的 internal iter_index slot");
    smoke_test::require(smoke_test::count_instructions(*entry, Instruction::Branch) == 4,
                        "test3_2 应包含两个 for header 分支和两个 while header 分支");
    smoke_test::require(
        has_synthetic_goto_between_labels(*entry, "while.end", "for.latch"),
        "内层 while 正常结束后应回到外层 for.latch");
    smoke_test::require(
        has_synthetic_goto_between_labels(*entry, "for.end", "while.header"),
        "内层 for 正常结束后应回到外层 while.header");
}

} // namespace
} // namespace baltam

int main() {
    try {
        const baltam::smoke_test::SmokeArtifacts artifacts =
            baltam::smoke_test::build_ir(TEST3_2_MFILE_PATH);
        baltam::verify_complete_ir(artifacts.result);
        baltam::verify_core_focus(artifacts.result);
    } catch (const std::exception& ex) {
        std::cerr << "test3_2_smoke 失败: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
