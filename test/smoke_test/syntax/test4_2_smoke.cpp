#include "smoke_test_common.h"

#include <cstddef>
#include <iostream>
#include <string_view>
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

void verify_complete_ir(const IRBuildResult& result) {
    smoke_test::require_ir_is_complete(result);
    smoke_test::require(result.mfile->is_script_file(), "test4_2 应构造成脚本文件");
    smoke_test::require(result.mfile->code_units.size() == 1, "test4_2 只应生成脚本单元");
    smoke_test::require(result.mfile->file_stem() == "test4_2", "文件 stem 应为 test4_2");
}

void verify_switch_exits_to_for_latch(const CodeUnit& unit) {
    const std::vector<const BasicBlock*> switch_ends = find_blocks_by_label(unit, "switch.end");
    const std::vector<const BasicBlock*> for_latches = find_blocks_by_label(unit, "for.latch");
    smoke_test::require(switch_ends.size() == 2, "应有两个 switch.end");
    smoke_test::require(for_latches.size() == 1, "应有唯一 for.latch");

    bool found = false;
    for (const BasicBlock* block : switch_ends) {
        const Instruction* terminator = block != nullptr ? block->terminator() : nullptr;
        if (terminator == nullptr || terminator->type() != Instruction::Goto) {
            continue;
        }
        const auto& go = static_cast<const GotoInst&>(*terminator);
        found = found || go.target == for_latches.front();
    }
    smoke_test::require(found, "for body 中的 switch.end 应回到 for.latch");
}

void verify_switch_exits_to_while_latch(const CodeUnit& unit) {
    const std::vector<const BasicBlock*> switch_ends = find_blocks_by_label(unit, "switch.end");
    const std::vector<const BasicBlock*> while_latches =
        find_blocks_by_label(unit, "while.latch");
    smoke_test::require(switch_ends.size() == 2, "应有两个 switch.end");
    smoke_test::require(while_latches.size() == 1, "应有唯一 while.latch");

    bool found = false;
    for (const BasicBlock* block : switch_ends) {
        const Instruction* terminator = block != nullptr ? block->terminator() : nullptr;
        if (terminator == nullptr || terminator->type() != Instruction::Goto) {
            continue;
        }
        const auto& go = static_cast<const GotoInst&>(*terminator);
        found = found || go.target == while_latches.front();
    }
    smoke_test::require(found, "while body 中的 switch.end 应回到 while.latch");
}

void verify_core_focus(const IRBuildResult& result) {
    const CodeUnit* entry = result.mfile->entry_unit;
    smoke_test::require(entry != nullptr && entry->is_script(), "入口应为脚本单元");

    smoke_test::require(count_blocks_by_label(*entry, "for.preheader") == 1,
                        "应生成一层 for.preheader");
    smoke_test::require(count_blocks_by_label(*entry, "for.latch") == 1,
                        "应生成一层 for.latch");
    smoke_test::require(count_blocks_by_label(*entry, "while.header") == 1,
                        "应生成一层 while.header");
    smoke_test::require(count_blocks_by_label(*entry, "while.latch") == 1,
                        "应生成一层 while.latch");
    smoke_test::require(count_blocks_by_label(*entry, "switch.dispatch") == 2,
                        "应生成两个 switch.dispatch");
    smoke_test::require(count_blocks_by_label(*entry, "switch.case") == 4,
                        "应生成四个 switch.case body 块");
    smoke_test::require(count_blocks_by_label(*entry, "switch.otherwise") == 2,
                        "应生成两个 switch.otherwise");
    verify_switch_exits_to_for_latch(*entry);
    verify_switch_exits_to_while_latch(*entry);
}

void verify_printed_ir(const std::string& printed_ir) {
    smoke_test::require(
        printed_ir.find("script @test4_2 {") != std::string::npos,
        "应打印 test4_2 脚本头");
    smoke_test::require(
        printed_ir.find("switch.end:\n  br label %for.latch") != std::string::npos,
        "switch.end 应打印为跳回外层 for.latch");
    smoke_test::require(
        printed_ir.find("switch.end.1:\n  br label %while.latch") != std::string::npos,
        "第二个 switch.end 应打印为跳回外层 while.latch");
}

} // namespace
} // namespace baltam

int main() {
    try {
        const baltam::smoke_test::SmokeArtifacts artifacts =
            baltam::smoke_test::build_ir(TEST4_2_MFILE_PATH);
        baltam::verify_complete_ir(artifacts.result);
        baltam::verify_core_focus(artifacts.result);
        baltam::verify_printed_ir(artifacts.printed_ir);
        std::cout << artifacts.printed_ir << '\n';
    } catch (const std::exception& ex) {
        std::cerr << "test4_2_smoke 失败: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
