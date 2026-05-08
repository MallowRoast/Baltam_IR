#include "smoke_test_common.h"

#include <cstddef>
#include <iostream>
#include <string_view>
#include <sstream>
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
}

void verify_printed_ir(const std::string& printed_ir) {
    smoke_test::require(
        printed_ir.find("; mfile \"" TEST2_3_MFILE_PATH "\"") != std::string::npos,
        "应打印 test2_3 文件头");
    smoke_test::require(
        printed_ir.find("script @test2_3 {") != std::string::npos,
        "应打印 test2_3 脚本头");
    smoke_test::require(
        printed_ir.find("for.latch:") != std::string::npos &&
            printed_ir.find("for.latch.1:") != std::string::npos,
        "内外两层 latch 应通过标签后缀区分");
    smoke_test::require(
        printed_ir.find("for.end:") != std::string::npos &&
            printed_ir.find("for.end.1:") != std::string::npos,
        "内外两层 end 应通过标签后缀区分");

    smoke_test::require(
        !find_line_containing_all(printed_ir, "br label %for.end", "break;").empty(),
        "外层 break 应跳到外层 for.end 并打印源码注释");
    smoke_test::require(
        !find_line_containing_all(printed_ir, "br label %for.latch.1", "continue;").empty(),
        "内层 continue 应跳到内层 for.latch.1 并打印源码注释");
    smoke_test::require(
        printed_ir.find("for.end.1:\n  br label %for.latch") != std::string::npos,
        "内层循环正常结束后应回到外层 for.latch");
}

} // namespace
} // namespace baltam

int main() {
    try {
        const baltam::smoke_test::SmokeArtifacts artifacts =
            baltam::smoke_test::build_ir(TEST2_3_MFILE_PATH);
        baltam::verify_complete_ir(artifacts.result);
        baltam::verify_core_focus(artifacts.result);
        baltam::verify_printed_ir(artifacts.printed_ir);
        std::cout << artifacts.printed_ir << '\n';
    } catch (const std::exception& ex) {
        std::cerr << "test2_3_smoke 失败: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
