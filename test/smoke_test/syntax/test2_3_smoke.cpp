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

std::size_t count_user_gotos_to_label(const CodeUnit& unit, std::string_view label) {
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
            if (go.attrs.is_synthetic == 0 &&
                go.target != nullptr &&
                go.target->label == label) {
                ++count;
            }
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
    smoke_test::require(result.mfile->is_script_file(), "test2_3 应构造成脚本文件");
    smoke_test::require(result.mfile->code_units.size() == 1, "test2_3 只应生成脚本单元");
    smoke_test::require(result.mfile->file_stem() == "test2_3", "文件 stem 应为 test2_3");
}

void verify_core_focus(const IRBuildResult& result) {
    const CodeUnit* entry = result.mfile->entry_unit;
    smoke_test::require(entry != nullptr && entry->is_script(), "入口应为脚本单元");

    smoke_test::require(count_blocks_by_label(*entry, "for.preheader") == 2,
                        "应生成两层 for.preheader");
    smoke_test::require(count_blocks_by_label(*entry, "for.latch") == 2,
                        "应生成两层 for.latch");
    smoke_test::require(count_blocks_by_label(*entry, "for.end") == 2,
                        "应生成两层 for.end");
    smoke_test::require(count_internal_calls(*entry, "foreach_init") == 2,
                        "两层 for 应各自初始化 foreach 状态");
    smoke_test::require(count_internal_calls(*entry, "foreach_iterate") == 2,
                        "两层 for 应各自迭代 foreach 状态");
    smoke_test::require(smoke_test::count_instructions(*entry, Instruction::Branch) == 4,
                        "test2_3 应包含两个 for header 分支和两个 if 分支");
    smoke_test::require(count_user_gotos_to_label(*entry, "for.end") == 1,
                        "外层 break 应生成唯一一条用户级跳转到 for.end");
    smoke_test::require(count_user_gotos_to_label(*entry, "for.latch") == 1,
                        "内层 continue 应生成唯一一条用户级跳转到 for.latch");
    smoke_test::require(
        has_synthetic_goto_between_labels(*entry, "for.end", "for.latch"),
        "内层 for 正常结束后应回到外层 for.latch");
}

} // namespace
} // namespace baltam

int main() {
    try {
        const baltam::IRBuildResult ir = baltam::smoke_test::build_ir(TEST2_3_MFILE_PATH);
        baltam::verify_complete_ir(ir);
        baltam::verify_core_focus(ir);
    } catch (const std::exception& ex) {
        std::cerr << "test2_3_smoke 失败: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
