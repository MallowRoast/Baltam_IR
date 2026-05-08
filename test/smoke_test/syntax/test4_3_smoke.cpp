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

bool block_has_synthetic_goto_to(const BasicBlock& block, const BasicBlock* target) {
    const Instruction* terminator = block.terminator();
    if (terminator == nullptr || terminator->type() != Instruction::Goto) {
        return false;
    }

    const auto& go = static_cast<const GotoInst&>(*terminator);
    return go.target == target && go.attrs.is_synthetic != 0;
}

void verify_complete_ir(const IRBuildResult& result) {
    smoke_test::require_ir_is_complete(result);
    smoke_test::require(result.mfile->is_script_file(), "test4_3 应构造成脚本文件");
    smoke_test::require(result.mfile->code_units.size() == 1, "test4_3 只应生成脚本单元");
    smoke_test::require(result.mfile->file_stem() == "test4_3", "文件 stem 应为 test4_3");
}

void verify_switch_and_loop_shape(const CodeUnit& unit) {
    smoke_test::require(count_blocks_by_label(unit, "switch.dispatch") == 1,
                        "应生成一个 switch.dispatch");
    smoke_test::require(count_blocks_by_label(unit, "switch.case") == 2,
                        "应生成两个 switch.case body 块");
    smoke_test::require(count_blocks_by_label(unit, "switch.next") == 2,
                        "应生成两个 switch.next 块");
    smoke_test::require(count_blocks_by_label(unit, "switch.otherwise") == 1,
                        "应生成一个 switch.otherwise");
    smoke_test::require(count_blocks_by_label(unit, "switch.end") == 1,
                        "应生成一个 switch.end");

    smoke_test::require(count_blocks_by_label(unit, "for.preheader") == 1,
                        "switch case 中应生成一层 for.preheader");
    smoke_test::require(count_blocks_by_label(unit, "for.header") == 1,
                        "switch case 中应生成一层 for.header");
    smoke_test::require(count_blocks_by_label(unit, "for.latch") == 1,
                        "switch case 中应生成一层 for.latch");
    smoke_test::require(count_blocks_by_label(unit, "for.end") == 1,
                        "switch case 中应生成一层 for.end");
    smoke_test::require(count_blocks_by_label(unit, "while.header") == 1,
                        "switch case 中应生成一层 while.header");
    smoke_test::require(count_blocks_by_label(unit, "while.latch") == 1,
                        "switch case 中应生成一层 while.latch");
    smoke_test::require(count_blocks_by_label(unit, "while.end") == 1,
                        "switch case 中应生成一层 while.end");
}

void verify_break_continue_targets(const CodeUnit& unit) {
    const std::vector<const BasicBlock*> for_latches = find_blocks_by_label(unit, "for.latch");
    const std::vector<const BasicBlock*> for_ends = find_blocks_by_label(unit, "for.end");
    const std::vector<const BasicBlock*> while_latches =
        find_blocks_by_label(unit, "while.latch");
    const std::vector<const BasicBlock*> while_ends = find_blocks_by_label(unit, "while.end");

    smoke_test::require(for_latches.size() == 1, "应有唯一 for.latch");
    smoke_test::require(for_ends.size() == 1, "应有唯一 for.end");
    smoke_test::require(while_latches.size() == 1, "应有唯一 while.latch");
    smoke_test::require(while_ends.size() == 1, "应有唯一 while.end");

    smoke_test::require(count_user_gotos_to(unit, for_latches.front()) == 1,
                        "for 内 continue 应跳到 for.latch，而不是 switch");
    smoke_test::require(count_user_gotos_to(unit, for_ends.front()) == 1,
                        "for 内 break 应跳到 for.end，而不是 switch");
    smoke_test::require(count_user_gotos_to(unit, while_latches.front()) == 1,
                        "while 内 continue 应跳到 while.latch，而不是 switch");
    smoke_test::require(count_user_gotos_to(unit, while_ends.front()) == 1,
                        "while 内 break 应跳到 while.end，而不是 switch");
}

void verify_loop_exits_to_switch_end(const CodeUnit& unit) {
    const std::vector<const BasicBlock*> switch_ends = find_blocks_by_label(unit, "switch.end");
    const std::vector<const BasicBlock*> for_ends = find_blocks_by_label(unit, "for.end");
    const std::vector<const BasicBlock*> while_ends = find_blocks_by_label(unit, "while.end");

    smoke_test::require(switch_ends.size() == 1, "应有唯一 switch.end");
    smoke_test::require(for_ends.size() == 1, "应有唯一 for.end");
    smoke_test::require(while_ends.size() == 1, "应有唯一 while.end");

    smoke_test::require(
        block_has_synthetic_goto_to(*for_ends.front(), switch_ends.front()),
        "for 正常结束后应 synthetic goto 到 switch.end");
    smoke_test::require(
        block_has_synthetic_goto_to(*while_ends.front(), switch_ends.front()),
        "while 正常结束后应 synthetic goto 到 switch.end");
}

void verify_core_focus(const IRBuildResult& result) {
    const CodeUnit* entry = result.mfile->entry_unit;
    smoke_test::require(entry != nullptr && entry->is_script(), "入口应为脚本单元");

    verify_switch_and_loop_shape(*entry);
    verify_break_continue_targets(*entry);
    verify_loop_exits_to_switch_end(*entry);
    smoke_test::require(count_internal_calls(*entry, "switch_match") == 2,
                        "两个普通 case 应生成两次 switch_match");
    smoke_test::require(count_internal_calls(*entry, "foreach_init") == 1,
                        "for case 应生成一次 foreach_init");
    smoke_test::require(count_internal_calls(*entry, "foreach_iterate") == 1,
                        "for case 应生成一次 foreach_iterate");
    smoke_test::require(smoke_test::count_instructions(*entry, Instruction::Branch) == 8,
                        "switch、for、while 与四个 if 应生成八条条件分支");
}

void verify_printed_ir(const std::string& printed_ir) {
    smoke_test::require(
        printed_ir.find("; mfile \"" TEST4_3_MFILE_PATH "\"") != std::string::npos,
        "应打印 test4_3 文件头");
    smoke_test::require(
        printed_ir.find("script @test4_3 {") != std::string::npos,
        "应打印 test4_3 脚本头");
    smoke_test::require(
        printed_ir.find("for.preheader:") != std::string::npos &&
            printed_ir.find("while.header:") != std::string::npos,
        "应打印 switch case 内的 for / while 块");
    smoke_test::require(
        printed_ir.find("continue;") != std::string::npos &&
            printed_ir.find("break;") != std::string::npos,
        "应保留 break / continue 的源码注释");
}

} // namespace
} // namespace baltam

int main() {
    try {
        const baltam::smoke_test::SmokeArtifacts artifacts =
            baltam::smoke_test::build_ir(TEST4_3_MFILE_PATH);
        baltam::verify_complete_ir(artifacts.result);
        baltam::verify_core_focus(artifacts.result);
        baltam::verify_printed_ir(artifacts.printed_ir);
        std::cout << artifacts.printed_ir << '\n';
    } catch (const std::exception& ex) {
        std::cerr << "test4_3_smoke 失败: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
