#include "smoke_test_common.h"

#include <cstddef>
#include <iostream>
#include <string_view>
#include <variant>
#include <vector>

namespace baltam {
namespace {

bool label_matches_query(std::string_view label, std::string_view query) {
    if (label == query) {
        return true;
    }
    constexpr std::string_view switch_prefix = "switch.";
    if (query.rfind(switch_prefix, 0) != 0 ||
        label.rfind(switch_prefix, 0) != 0) {
        return false;
    }

    const std::string_view kind = query.substr(switch_prefix.size());
    const std::string suffix = "." + std::string(kind);
    return label.size() > switch_prefix.size() + suffix.size() &&
        label.compare(label.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::vector<const BasicBlock*> find_blocks_by_label(
    const CodeUnit& unit,
    std::string_view label) {
    std::vector<const BasicBlock*> blocks;
    for (const auto& block_ptr : unit.basic_blocks) {
        if (block_ptr != nullptr && label_matches_query(block_ptr->label, label)) {
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

std::size_t count_internal_binary_ops(const CodeUnit& unit, BinaryOp op) {
    std::size_t count = 0;
    for (const auto& block_ptr : unit.basic_blocks) {
        if (block_ptr == nullptr) {
            continue;
        }
        for (const auto& inst_ptr : block_ptr->instructions) {
            if (inst_ptr == nullptr || inst_ptr->type() != Instruction::Binary) {
                continue;
            }
            const auto& binary = static_cast<const BinaryInst&>(*inst_ptr);
            if (binary.dispatch_type == Internal && binary.op == op) {
                ++count;
            }
        }
    }
    return count;
}

void verify_complete_ir(const IRBuildResult& result) {
    smoke_test::require_ir_is_complete(result);
    smoke_test::require(result.mfile->is_script_file(), "test4_1 应构造成脚本文件");
    smoke_test::require(result.mfile->code_units.size() == 1, "test4_1 只应生成脚本单元");
    smoke_test::require(result.mfile->file_stem() == "test4_1", "文件 stem 应为 test4_1");
}

void verify_nested_switch_shape(const CodeUnit& unit) {
    smoke_test::require(count_blocks_by_label(unit, "switch.case") == 3,
                        "内外两层 switch 应生成三个 switch.case 判断块");
    smoke_test::require(count_blocks_by_label(unit, "switch.body") == 3,
                        "内外两层 switch 应生成三个 switch.body 块");
    smoke_test::require(count_blocks_by_label(unit, "switch.otherwise") == 2,
                        "内外两层 switch 应各生成一个 switch.otherwise");
    smoke_test::require(count_blocks_by_label(unit, "switch.end") == 2,
                        "嵌套 switch 应生成两个 switch.end");
}

void verify_inner_switch_returns_to_outer_case_continuation(const CodeUnit& unit) {
    const std::vector<const BasicBlock*> switch_ends = find_blocks_by_label(unit, "switch.end");
    smoke_test::require(switch_ends.size() == 2, "应有两个 switch.end");

    bool found_inner_end_continuation = false;
    bool found_outer_end_return = false;
    for (const BasicBlock* block : switch_ends) {
        const Instruction* terminator = block != nullptr ? block->terminator() : nullptr;
        if (terminator == nullptr) {
            continue;
        }

        if (terminator->type() == Instruction::Return) {
            found_outer_end_return = true;
            continue;
        }

        if (terminator->type() != Instruction::Goto) {
            continue;
        }

        const auto& go = static_cast<const GotoInst&>(*terminator);
        if (go.target != block &&
            go.target != nullptr &&
            go.target->label == "switch.end" &&
            smoke_test::block_stores_slot_name(*block, unit, "s")) {
            found_inner_end_continuation = true;
        }
    }

    smoke_test::require(found_inner_end_continuation,
                        "内层 switch.end 应承载后续 s = s + 1 并跳到外层 switch.end");
    smoke_test::require(found_outer_end_return, "外层 switch.end 应作为脚本出口返回");
}

void verify_core_focus(const IRBuildResult& result) {
    const CodeUnit* entry = result.mfile->entry_unit;
    smoke_test::require(entry != nullptr && entry->is_script(), "入口应为脚本单元");

    verify_nested_switch_shape(*entry);
    verify_inner_switch_returns_to_outer_case_continuation(*entry);
    smoke_test::require(count_internal_calls(*entry, "switch_match") == 3,
                        "外层两个 case 与内层一个 case 应生成三次 switch_match");
    smoke_test::require(smoke_test::count_instructions(*entry, Instruction::Branch) == 3,
                        "内外两层 switch 的三个 case 应生成三条条件分支");
}

} // namespace
} // namespace baltam

int main() {
    try {
        const baltam::IRBuildResult ir = baltam::smoke_test::build_ir(TEST4_1_MFILE_PATH);
        baltam::verify_complete_ir(ir);
        baltam::verify_core_focus(ir);
    } catch (const std::exception& ex) {
        std::cerr << "test4_1_smoke 失败: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
