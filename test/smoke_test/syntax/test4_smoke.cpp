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
    smoke_test::require(result.mfile->is_script_file(), "test4 应构造成脚本文件");
    smoke_test::require(result.mfile->code_units.size() == 1, "test4 只应生成脚本单元");
    smoke_test::require(result.mfile->file_stem() == "test4", "文件 stem 应为 test4");
}

void verify_switch_shape(const CodeUnit& unit) {
    smoke_test::require(count_blocks_by_label(unit, "switch.case") == 3,
                        "应生成三个 switch.case 判断块");
    smoke_test::require(count_blocks_by_label(unit, "switch.body") == 3,
                        "应生成三个 switch.body 块");
    smoke_test::require(count_blocks_by_label(unit, "switch.otherwise") == 1,
                        "只有第一个 switch 应生成 switch.otherwise");
    smoke_test::require(count_blocks_by_label(unit, "switch.end") == 2,
                        "应生成两个 switch.end");
}

void verify_case_bodies_exit_to_switch_end(const CodeUnit& unit) {
    const std::vector<const BasicBlock*> case_blocks = find_blocks_by_label(unit, "switch.body");
    smoke_test::require(case_blocks.size() == 3, "应有三个 case body");
    for (const BasicBlock* block : case_blocks) {
        const Instruction* terminator = block != nullptr ? block->terminator() : nullptr;
        smoke_test::require(
            terminator != nullptr && terminator->type() == Instruction::Goto,
            "case body 正常结束后应 goto switch.end");
        const auto& go = static_cast<const GotoInst&>(*terminator);
        smoke_test::require(
            go.target != nullptr && label_matches_query(go.target->label, "switch.end"),
                            "case body 不能 fallthrough 到下一个 case");
        smoke_test::require(go.attrs.is_synthetic != 0, "case body 出口应为 synthetic goto");
    }
}

void verify_no_otherwise_falls_through_to_switch_end(const CodeUnit& unit) {
    const std::vector<const BasicBlock*> switch_cases = find_blocks_by_label(unit, "switch.case");
    smoke_test::require(switch_cases.size() == 3, "应有三个 switch.case");

    bool found_no_otherwise_exit = false;
    for (const BasicBlock* block : switch_cases) {
        const Instruction* terminator = block != nullptr ? block->terminator() : nullptr;
        if (terminator == nullptr || terminator->type() != Instruction::Branch) {
            continue;
        }
        const auto& branch = static_cast<const BranchInst&>(*terminator);
        if (branch.false_target != nullptr &&
            label_matches_query(branch.false_target->label, "switch.end")) {
            found_no_otherwise_exit = true;
        }
    }

    smoke_test::require(found_no_otherwise_exit,
                        "无 otherwise 的 switch 应在所有 case 失败后直接进入 switch.end");
}

void verify_core_focus(const IRBuildResult& result) {
    const CodeUnit* entry = result.mfile->entry_unit;
    smoke_test::require(entry != nullptr && entry->is_script(), "入口应为脚本单元");

    verify_switch_shape(*entry);
    verify_case_bodies_exit_to_switch_end(*entry);
    verify_no_otherwise_falls_through_to_switch_end(*entry);
    smoke_test::require(count_internal_calls(*entry, "switch_match") == 3,
                        "第一个 switch 一个 case、第二个 switch 两个 case");
    smoke_test::require(smoke_test::count_instructions(*entry, Instruction::Branch) == 3,
                        "三个 case 应生成三条条件分支");
}

} // namespace
} // namespace baltam

int main() {
    try {
        const baltam::smoke_test::SmokeArtifacts artifacts =
            baltam::smoke_test::build_ir(TEST4_MFILE_PATH);
        baltam::verify_complete_ir(artifacts.result);
        baltam::verify_core_focus(artifacts.result);
    } catch (const std::exception& ex) {
        std::cerr << "test4_smoke 失败: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
