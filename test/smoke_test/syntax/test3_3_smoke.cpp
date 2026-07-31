#include "smoke_test_common.h"

#include <cstddef>
#include <iostream>
#include <string_view>
#include <variant>
#include <vector>

namespace baltam {
namespace {

std::vector<const BasicBlock*> find_blocks_by_label(
    const CodeUnit& unit,
    std::string_view label) {
    std::vector<const BasicBlock*> blocks;
    for (const auto& block_ptr : unit.basic_blocks) {
        if (block_ptr != nullptr && block_ptr->label == label) {
            blocks.push_back(block_ptr.get());
        }
    }
    return blocks;
}

std::size_t count_blocks_by_label(const CodeUnit& unit, std::string_view label) {
    return find_blocks_by_label(unit, label).size();
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

std::size_t count_user_gotos_to(const CodeUnit& unit, const BasicBlock* target) {
    std::size_t count = 0;
    for (const auto& block_ptr : unit.basic_blocks) {
        if (block_ptr == nullptr) {
            continue;
        }
        for (const auto& inst_ptr : block_ptr->instructions) {
            if (inst_ptr == nullptr || inst_ptr->type() != Instruction::Goto) {
                continue;
            }
            const auto& go = static_cast<const GotoInst&>(*inst_ptr);
            if (go.target == target && go.attrs.is_synthetic == 0) {
                ++count;
            }
        }
    }
    return count;
}

void verify_complete_ir(const IRBuildResult& result) {
    smoke_test::require_ir_is_complete(result);
    smoke_test::require(result.mfile->is_script_file(), "test3_3 应构造成脚本文件");
    smoke_test::require(result.mfile->code_units.size() == 1, "test3_3 只应生成脚本单元");
    smoke_test::require(result.mfile->file_stem() == "test3_3", "文件 stem 应为 test3_3");
}

void verify_loop_shape(const CodeUnit& unit) {
    smoke_test::require(count_blocks_by_label(unit, "for.preheader") == 2,
                        "应生成两套 for.preheader");
    smoke_test::require(count_blocks_by_label(unit, "for.header") == 2,
                        "应生成两套 for.header");
    smoke_test::require(count_blocks_by_label(unit, "for.body") == 2,
                        "应生成两套 for.body");
    smoke_test::require(count_blocks_by_label(unit, "for.latch") == 2,
                        "应生成两套 for.latch");
    smoke_test::require(count_blocks_by_label(unit, "for.end") == 2,
                        "应生成两套 for.end");
    smoke_test::require(count_blocks_by_label(unit, "while.header") == 2,
                        "应生成两套 while.header");
    smoke_test::require(count_blocks_by_label(unit, "while.body") == 2,
                        "应生成两套 while.body");
    smoke_test::require(count_blocks_by_label(unit, "while.end") == 2,
                        "应生成两套 while.end");
}

void verify_break_continue_targets(const CodeUnit& unit) {
    const std::vector<const BasicBlock*> for_ends = find_blocks_by_label(unit, "for.end");
    const std::vector<const BasicBlock*> while_headers =
        find_blocks_by_label(unit, "while.header");
    smoke_test::require(for_ends.size() == 2, "应能区分外层和内层 for.end");
    smoke_test::require(while_headers.size() == 2, "应能区分内外两层 while.header");

    smoke_test::require(count_user_gotos_to(unit, for_ends[0]) == 1,
                        "外层 for 的 break 应跳到第一套 for.end");
    smoke_test::require(count_user_gotos_to(unit, while_headers[0]) == 1,
                        "内层 while 的 continue 应跳到第一套 while.header");
    smoke_test::require(count_user_gotos_to(unit, while_headers[1]) == 1,
                        "外层 while 的 continue 应跳到第二套 while.header");
    smoke_test::require(count_user_gotos_to(unit, for_ends[1]) == 1,
                        "内层 for 的 break 应跳到第二套 for.end");
}

void verify_core_focus(const IRBuildResult& result) {
    const CodeUnit* entry = result.mfile->entry_unit;
    smoke_test::require(entry != nullptr && entry->is_script(), "入口应为脚本单元");

    verify_loop_shape(*entry);
    verify_break_continue_targets(*entry);

    smoke_test::require(count_direct_calls(*entry, "colon") == 2,
                        "两层 for 的范围表达式应 lower 成两次 colon call");
    smoke_test::require(count_internal_calls(*entry, "foreach_init") == 2,
                        "两层 for 应 lower 出两次 internal.foreach_init");
    smoke_test::require(count_internal_calls(*entry, "foreach_iterate") == 2,
                        "两层 for 应 lower 出两次 internal.foreach_iterate");
    smoke_test::require(count_internal_iter_index_slots(*entry) == 2,
                        "两层 for 应创建两个独立的 internal iter_index slot");
    smoke_test::require(smoke_test::count_instructions(*entry, Instruction::Branch) == 8,
                        "test3_3 应包含两层 for、两层 while 和四个 if 的条件分支");
}

} // namespace
} // namespace baltam

int main() {
    try {
        const baltam::IRBuildResult ir = baltam::smoke_test::build_ir(TEST3_3_MFILE_PATH);
        baltam::verify_complete_ir(ir);
        baltam::verify_core_focus(ir);
    } catch (const std::exception& ex) {
        std::cerr << "test3_3_smoke 失败: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
