#include "smoke_test_common.h"

#include <iostream>
#include <string_view>

namespace baltam {
namespace {

const FunctionUnit* find_function(const MFileUnit& mfile, std::string_view name) {
    for (const auto& unit_ptr : mfile.code_units) {
        if (unit_ptr != nullptr && unit_ptr->is_function() && unit_ptr->name == name) {
            return static_cast<const FunctionUnit*>(unit_ptr.get());
        }
    }
    return nullptr;
}

std::size_t count_binary_op(const CodeUnit& unit, BinaryOp op) {
    std::size_t count = 0;
    for (const auto& block_ptr : unit.basic_blocks) {
        for (const auto& inst_ptr : block_ptr->instructions) {
            if (inst_ptr != nullptr && inst_ptr->type() == Instruction::Binary) {
                const auto& binary = static_cast<const BinaryInst&>(*inst_ptr);
                if (binary.op == op) {
                    ++count;
                }
            }
        }
    }
    return count;
}

std::size_t count_internal_short_circuit_slots(const CodeUnit& unit) {
    std::size_t count = 0;
    for (const SlotInfo& slot : unit.slot_table.slots) {
        if (slot.slot.tag == SlotTag::InternalLocal &&
            (slot.name == "__sc_and" || slot.name == "__sc_or")) {
            ++count;
        }
    }
    return count;
}

bool has_block_label(const CodeUnit& unit, std::string_view label) {
    for (const auto& block_ptr : unit.basic_blocks) {
        if (block_ptr != nullptr && block_ptr->label == label) {
            return true;
        }
    }
    return false;
}

void verify_short_circuit_cfg(const FunctionUnit& function) {
    smoke_test::require(
        count_binary_op(function, And) == 0,
        "&& 不应 lower 成普通 BinaryInst And");
    smoke_test::require(
        count_binary_op(function, Or) == 0,
        "|| 不应 lower 成普通 BinaryInst Or");
    smoke_test::require(
        smoke_test::count_instructions(function, Instruction::Branch) >= 4,
        "test6 应为短路表达式和 if 生成分支 CFG");
    smoke_test::require(
        count_internal_short_circuit_slots(function) == 3,
        "test6 中三个短路表达式应各创建一个内部结果 slot");
    smoke_test::require(
        has_block_label(function, "sc.and.rhs") &&
            has_block_label(function, "sc.and.false") &&
            has_block_label(function, "sc.and.end"),
        "&& 应生成 rhs / false / end 短路基本块");
    smoke_test::require(
        has_block_label(function, "sc.or.rhs") &&
            has_block_label(function, "sc.or.true") &&
            has_block_label(function, "sc.or.end"),
        "|| 应生成 rhs / true / end 短路基本块");
}

} // namespace
} // namespace baltam

int main() {
    try {
        const baltam::smoke_test::SmokeArtifacts artifacts =
            baltam::smoke_test::build_ir(TEST6_MFILE_PATH);
        baltam::smoke_test::require_ir_is_complete(artifacts.result);
        baltam::smoke_test::require(artifacts.result.mfile->is_function_file(), "test6 应为函数文件");
        baltam::smoke_test::require(
            artifacts.result.mfile->code_units.size() == 1,
            "test6 文件应只包含主函数单元");

        const baltam::FunctionUnit* function =
            baltam::find_function(*artifacts.result.mfile, "test6");
        baltam::smoke_test::require(function != nullptr, "应找到 test6 函数单元");

        baltam::verify_short_circuit_cfg(*function);
    } catch (const std::exception& ex) {
        std::cerr << "test6_smoke 失败: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
