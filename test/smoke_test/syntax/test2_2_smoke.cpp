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
    for (const Slot& slot : unit.slot_table.slots) {
        if (slot.name == "__foreach_iter_index" &&
            slot.type == Slot::InternalLocal &&
            slot.attrs.fixed_type == SlotAttrs::Int64Scalar) {
            ++count;
        }
    }
    return count;
}

void verify_complete_ir(const IRBuildResult& result) {
    smoke_test::require_ir_is_complete(result);
    smoke_test::require(result.mfile->is_script_file(), "test2_2 应构造成脚本文件");
    smoke_test::require(result.mfile->code_units.size() == 1, "test2_2 只应生成脚本单元");
    smoke_test::require(result.mfile->file_stem() == "test2_2", "文件 stem 应为 test2_2");
}

void verify_core_focus(const IRBuildResult& result) {
    const CodeUnit* entry = result.mfile->entry_unit;
    smoke_test::require(entry != nullptr && entry->is_script(), "入口应为脚本单元");

    smoke_test::require(entry->basic_blocks.size() == 11,
                        "嵌套 for 应生成 entry + 两套 for 五块 CFG");
    smoke_test::require(count_blocks_by_label(*entry, "for.preheader") == 2,
                        "应生成两个 for.preheader 块");
    smoke_test::require(count_blocks_by_label(*entry, "for.header") == 2,
                        "应生成两个 for.header 块");
    smoke_test::require(count_blocks_by_label(*entry, "for.body") == 2,
                        "应生成两个 for.body 块");
    smoke_test::require(count_blocks_by_label(*entry, "for.latch") == 2,
                        "应生成两个 for.latch 块");
    smoke_test::require(count_blocks_by_label(*entry, "for.end") == 2,
                        "应生成两个 for.end 块");

    smoke_test::require(count_direct_calls(*entry, "colon") == 2,
                        "两层 for 的范围表达式应 lower 成两次 colon call");
    smoke_test::require(count_internal_calls(*entry, "foreach_init") == 2,
                        "两层 for 应 lower 出两次 internal.foreach_init");
    smoke_test::require(count_internal_calls(*entry, "foreach_iterate") == 2,
                        "两层 for 应 lower 出两次 internal.foreach_iterate");
    smoke_test::require(count_internal_iter_index_slots(*entry) == 2,
                        "两层 for 应创建两个独立的 internal iter_index slot");
    smoke_test::require(smoke_test::count_instructions(*entry, Instruction::Binary) == 6,
                        "test2_2 应包含两套内部比较、自增以及 i*j 和 s+...");
}

void verify_printed_ir(const std::string& printed_ir) {
    smoke_test::require(
        printed_ir.find("; mfile \"" TEST2_2_MFILE_PATH "\"") != std::string::npos,
        "应打印 test2_2 文件头");
    smoke_test::require(
        printed_ir.find("script @test2_2 {") != std::string::npos,
        "应打印 test2_2 脚本头");
    smoke_test::require(
        printed_ir.find("for.preheader:") != std::string::npos &&
            printed_ir.find("for.preheader.1:") != std::string::npos,
        "内外两层 preheader 应通过标签后缀区分");
    smoke_test::require(
        printed_ir.find("for.latch:") != std::string::npos &&
            printed_ir.find("for.latch.1:") != std::string::npos,
        "内外两层 latch 应通过标签后缀区分");
    smoke_test::require(
        printed_ir.find("for.end:") != std::string::npos &&
            printed_ir.find("for.end.1:") != std::string::npos,
        "内外两层 end 应通过标签后缀区分");
    smoke_test::require(
        printed_ir.find("br label %for.latch.1") != std::string::npos &&
            printed_ir.find("br label %for.latch") != std::string::npos,
        "内层循环体应跳内层 latch，内层结束后应回到外层 latch");
    smoke_test::require(
        printed_ir.find("store_env %test2_2_env, @s") != std::string::npos,
        "内层循环体应写回 workspace 变量 s");
}

} // namespace
} // namespace baltam

int main() {
    try {
        const baltam::smoke_test::SmokeArtifacts artifacts =
            baltam::smoke_test::build_ir(TEST2_2_MFILE_PATH);
        baltam::verify_complete_ir(artifacts.result);
        baltam::verify_core_focus(artifacts.result);
        baltam::verify_printed_ir(artifacts.printed_ir);
        std::cout << artifacts.printed_ir << '\n';
    } catch (const std::exception& ex) {
        std::cerr << "test2_2_smoke 失败: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
