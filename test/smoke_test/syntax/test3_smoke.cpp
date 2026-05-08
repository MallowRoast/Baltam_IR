#include "smoke_test_common.h"

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
    smoke_test::require(result.mfile->is_script_file(), "test3 应构造成脚本文件");
    smoke_test::require(result.mfile->code_units.size() == 1, "test3 只应生成脚本单元");
    smoke_test::require(result.mfile->file_stem() == "test3", "文件 stem 应为 test3");
}

void verify_while_cfg(const CodeUnit& unit) {
    const BasicBlock* header = find_block_by_label(unit, "while.header");
    const BasicBlock* body = find_block_by_label(unit, "while.body");
    const BasicBlock* latch = find_block_by_label(unit, "while.latch");
    const BasicBlock* end = find_block_by_label(unit, "while.end");

    smoke_test::require(header != nullptr, "while 应生成 header 块");
    smoke_test::require(body != nullptr, "while 应生成 body 块");
    smoke_test::require(latch != nullptr, "while 应生成 latch 块");
    smoke_test::require(end != nullptr, "while 应生成 end 块");

    smoke_test::require(unit.basic_blocks.size() == 5, "test3 应生成 entry + 4 个 while 基本块");
    smoke_test::require(unit.entry_block->terminator()->type() == Instruction::Goto,
                        "entry 应跳到 while.header");
    smoke_test::require(header->terminator()->type() == Instruction::Branch,
                        "while.header 应以条件分支结束");
    smoke_test::require(body->terminator()->type() == Instruction::Goto,
                        "while.body 应跳到 while.latch");
    smoke_test::require(latch->terminator()->type() == Instruction::Goto,
                        "while.latch 应回跳 while.header");
    smoke_test::require(end->terminator()->type() == Instruction::Return,
                        "while.end 应接隐式 return");
}

void verify_core_focus(const IRBuildResult& result) {
    const CodeUnit* entry = result.mfile->entry_unit;
    smoke_test::require(entry != nullptr && entry->is_script(), "入口应为脚本单元");

    verify_while_cfg(*entry);

    smoke_test::require(
        smoke_test::count_instructions(*entry, Instruction::Branch) == 1,
        "test3 应包含一个 while header 条件分支");
    smoke_test::require(
        smoke_test::count_instructions(*entry, Instruction::Binary) == 3,
        "test3 应包含 while 条件比较、i 自增和 s 自增");
    smoke_test::require(
        smoke_test::count_instructions(*entry, Instruction::StoreWorkspace) == 4,
        "test3 应写入 i/s 初值以及循环体内 i/s 更新");
}

void verify_printed_ir(const std::string& printed_ir) {
    smoke_test::require(
        printed_ir.find("; mfile \"" TEST3_MFILE_PATH "\"") != std::string::npos,
        "应打印 test3 文件头");
    smoke_test::require(
        printed_ir.find("script @test3 {") != std::string::npos,
        "应打印 test3 脚本头");
    smoke_test::require(
        printed_ir.find("while.header:") != std::string::npos &&
            printed_ir.find("while.body:") != std::string::npos &&
            printed_ir.find("while.latch:") != std::string::npos &&
            printed_ir.find("while.end:") != std::string::npos,
        "应打印 while 的四个基本块");
    smoke_test::require(
        printed_ir.find("br label %while.header") != std::string::npos,
        "应打印回跳 while.header");
    smoke_test::require(
        printed_ir.find("br label %while.latch") != std::string::npos,
        "应打印跳转 while.latch");
    smoke_test::require(
        printed_ir.find("store_env %test3_env, @s") != std::string::npos,
        "循环体应写回 workspace 变量 s");
}

} // namespace
} // namespace baltam

int main() {
    try {
        const baltam::smoke_test::SmokeArtifacts artifacts =
            baltam::smoke_test::build_ir(TEST3_MFILE_PATH);
        baltam::verify_complete_ir(artifacts.result);
        baltam::verify_core_focus(artifacts.result);
        baltam::verify_printed_ir(artifacts.printed_ir);
        std::cout << artifacts.printed_ir << '\n';
    } catch (const std::exception& ex) {
        std::cerr << "test3_smoke 失败: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
