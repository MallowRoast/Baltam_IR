#include "smoke_test_common.h"

#include <cstddef>
#include <iostream>
#include <string_view>

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

void verify_complete_ir(const IRBuildResult& result) {
    smoke_test::require_ir_is_complete(result);
    smoke_test::require(result.mfile->is_script_file(), "test3_1 应构造成脚本文件");
    smoke_test::require(result.mfile->code_units.size() == 1, "test3_1 只应生成脚本单元");
    smoke_test::require(result.mfile->file_stem() == "test3_1", "文件 stem 应为 test3_1");
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

void verify_core_focus(const IRBuildResult& result) {
    const CodeUnit* entry = result.mfile->entry_unit;
    smoke_test::require(entry != nullptr && entry->is_script(), "入口应为脚本单元");

    const BasicBlock* header = find_block_by_label(*entry, "while.header");
    const BasicBlock* end = find_block_by_label(*entry, "while.end");
    smoke_test::require(header != nullptr, "while 应生成 header 块");
    smoke_test::require(end != nullptr, "while 应生成 end 块");

    smoke_test::require(
        smoke_test::count_instructions(*entry, Instruction::Branch) == 3,
        "test3_1 应包含 while header 分支和两个 if 分支");
    smoke_test::require(
        smoke_test::count_instructions(*entry, Instruction::Binary) == 5,
        "test3_1 应包含 while 条件比较、自增、两个 if 比较和循环体加法");

    smoke_test::require(
        count_user_gotos_to(*entry, header) == 1,
        "continue 应生成唯一一条用户级跳转到 while.header");
    smoke_test::require(
        count_user_gotos_to(*entry, end) == 1,
        "break 应生成唯一一条用户级跳转到 while.end");
}

} // namespace
} // namespace baltam

int main() {
    try {
        const baltam::smoke_test::SmokeArtifacts artifacts =
            baltam::smoke_test::build_ir(TEST3_1_MFILE_PATH);
        baltam::verify_complete_ir(artifacts.result);
        baltam::verify_core_focus(artifacts.result);
    } catch (const std::exception& ex) {
        std::cerr << "test3_1_smoke 失败: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
