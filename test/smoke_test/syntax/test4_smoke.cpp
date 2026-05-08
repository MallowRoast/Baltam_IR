#include "smoke_test_common.h"

#include <cstddef>
#include <iostream>
#include <sstream>
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

std::string find_line_containing_all(
    std::string_view text,
    std::string_view first,
    std::string_view second) {
    std::istringstream input{std::string(text)};
    std::string line;
    while (std::getline(input, line)) {
        if (line.find(first) != std::string::npos &&
            line.find(second) != std::string::npos) {
            return line;
        }
    }

    return {};
}

void verify_complete_ir(const IRBuildResult& result) {
    smoke_test::require_ir_is_complete(result);
    smoke_test::require(result.mfile->is_script_file(), "test4 应构造成脚本文件");
    smoke_test::require(result.mfile->code_units.size() == 1, "test4 只应生成脚本单元");
    smoke_test::require(result.mfile->file_stem() == "test4", "文件 stem 应为 test4");
}

void verify_switch_shape(const CodeUnit& unit) {
    smoke_test::require(count_blocks_by_label(unit, "switch.dispatch") == 2,
                        "应生成两个 switch.dispatch");
    smoke_test::require(count_blocks_by_label(unit, "switch.case") == 4,
                        "应生成四个 switch.case body 块");
    smoke_test::require(count_blocks_by_label(unit, "switch.next") == 4,
                        "应生成四个 switch.next 判断延续块");
    smoke_test::require(count_blocks_by_label(unit, "switch.otherwise") == 1,
                        "只有第一个 switch 应生成 switch.otherwise");
    smoke_test::require(count_blocks_by_label(unit, "switch.end") == 2,
                        "应生成两个 switch.end");
}

void verify_case_bodies_exit_to_switch_end(const CodeUnit& unit) {
    const std::vector<const BasicBlock*> case_blocks = find_blocks_by_label(unit, "switch.case");
    smoke_test::require(case_blocks.size() == 4, "应有四个 case body");
    for (const BasicBlock* block : case_blocks) {
        const Instruction* terminator = block != nullptr ? block->terminator() : nullptr;
        smoke_test::require(
            terminator != nullptr && terminator->type() == Instruction::Goto,
            "case body 正常结束后应 goto switch.end");
        const auto& go = static_cast<const GotoInst&>(*terminator);
        smoke_test::require(go.target != nullptr && go.target->label == "switch.end",
                            "case body 不能 fallthrough 到下一个 case");
        smoke_test::require(go.attrs.is_synthetic != 0, "case body 出口应为 synthetic goto");
    }
}

void verify_no_otherwise_falls_through_to_switch_end(const CodeUnit& unit) {
    const std::vector<const BasicBlock*> switch_nexts = find_blocks_by_label(unit, "switch.next");
    smoke_test::require(switch_nexts.size() == 4, "应有四个 switch.next");

    bool found_no_otherwise_exit = false;
    for (const BasicBlock* block : switch_nexts) {
        const Instruction* terminator = block != nullptr ? block->terminator() : nullptr;
        if (terminator == nullptr || terminator->type() != Instruction::Goto) {
            continue;
        }
        const auto& go = static_cast<const GotoInst&>(*terminator);
        if (go.target != nullptr &&
            go.target->label == "switch.end" &&
            go.attrs.is_synthetic != 0) {
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
    smoke_test::require(count_internal_calls(*entry, "switch_match") == 5,
                        "第一个 switch 三个匹配值、第二个 switch 两个匹配值");
    smoke_test::require(count_internal_binary_ops(*entry, Or) == 1,
                        "case {2, 3} 应用 internal.or 合并匹配条件");
    smoke_test::require(smoke_test::count_instructions(*entry, Instruction::Branch) == 4,
                        "四个 case 应生成四条条件分支");
}

void verify_printed_ir(const std::string& printed_ir) {
    smoke_test::require(
        printed_ir.find("; mfile \"" TEST4_MFILE_PATH "\"") != std::string::npos,
        "应打印 test4 文件头");
    smoke_test::require(
        printed_ir.find("script @test4 {") != std::string::npos,
        "应打印 test4 脚本头");
    smoke_test::require(
        printed_ir.find("switch.dispatch:") != std::string::npos &&
            printed_ir.find("switch.dispatch.1:") != std::string::npos,
        "两个 switch.dispatch 应通过标签后缀区分");
    smoke_test::require(
        printed_ir.find("call @internal.switch_match") != std::string::npos,
        "应打印 internal.switch_match");
    smoke_test::require(
        printed_ir.find("internal.or") != std::string::npos,
        "应打印 internal.or");
    smoke_test::require(
        !find_line_containing_all(printed_ir, "br %", "{2, 3}").empty(),
        "cell case 分支应带源码注释");
    smoke_test::require(
        printed_ir.find("switch.next.3:\n  br label %switch.end.1") != std::string::npos,
        "无 otherwise 的 switch 最后一个 next 应直接跳到自身 switch.end");
}

} // namespace
} // namespace baltam

int main() {
    try {
        const baltam::smoke_test::SmokeArtifacts artifacts =
            baltam::smoke_test::build_ir(TEST4_MFILE_PATH);
        baltam::verify_complete_ir(artifacts.result);
        baltam::verify_core_focus(artifacts.result);
        baltam::verify_printed_ir(artifacts.printed_ir);
        std::cout << artifacts.printed_ir << '\n';
    } catch (const std::exception& ex) {
        std::cerr << "test4_smoke 失败: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
