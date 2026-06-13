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
    smoke_test::require(result.mfile->is_script_file(), "test2_1 应构造成脚本文件");
    smoke_test::require(result.mfile->code_units.size() == 1, "test2_1 只应生成脚本单元");
    smoke_test::require(result.mfile->file_stem() == "test2_1", "文件 stem 应为 test2_1");
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

    const BasicBlock* latch = find_block_by_label(*entry, "for.latch");
    const BasicBlock* end = find_block_by_label(*entry, "for.end");
    smoke_test::require(latch != nullptr, "for 应生成 latch 块");
    smoke_test::require(end != nullptr, "for 应生成 end 块");

    smoke_test::require(
        smoke_test::count_instructions(*entry, Instruction::Branch) == 3,
        "test2_1 应包含 for header 分支和两个 if 分支");
    smoke_test::require(
        smoke_test::count_instructions(*entry, Instruction::Binary) == 5,
        "test2_1 应包含 for 内部比较、自增、两个 if 比较和循环体加法");

    smoke_test::require(
        count_user_gotos_to(*entry, latch) == 1,
        "continue 应生成唯一一条用户级跳转到 for.latch");
    smoke_test::require(
        count_user_gotos_to(*entry, end) == 1,
        "break 应生成唯一一条用户级跳转到 for.end");
}

} // namespace
} // namespace baltam

int main() {
    try {
        const baltam::smoke_test::SmokeArtifacts artifacts =
            baltam::smoke_test::build_ir(TEST2_1_MFILE_PATH);
        baltam::verify_complete_ir(artifacts.result);
        baltam::verify_core_focus(artifacts.result);
    } catch (const std::exception& ex) {
        std::cerr << "test2_1_smoke 失败: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
