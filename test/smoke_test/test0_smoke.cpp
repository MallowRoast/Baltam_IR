#include "ir/ir_lowering.h"
#include "ir/ir_print.h"

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace baltam {
namespace {

[[noreturn]] void fail(const char* message) {
    throw std::runtime_error(message);
}

void require(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

struct SmokeArtifacts {
    IRBuildResult result;
    std::string printed_ir;
};

SmokeArtifacts build_test0_ir() {
    SmokeArtifacts artifacts;
    artifacts.result = parse_and_lower_mfile_to_ir(TEST0_MFILE_PATH);

    require(artifacts.result.mfile != nullptr, "结果文件单元不能为空");

    IRPrintOptions print_options;
    print_options.load_source_from_path = true;
    artifacts.printed_ir = format_ir(*artifacts.result.mfile, print_options);
    return artifacts;
}

void verify_test0_ir(const IRBuildResult& result) {
    require(result.mfile != nullptr, "结果文件单元不能为空");
    require(result.mfile->entry_unit != nullptr, "入口代码单元不能为空");
    require(result.mfile->is_script_file(), "test0 应构造成脚本文件");
    require(result.mfile->code_units.size() == 1, "应只生成一个代码单元");
    require(result.mfile->file_stem() == "test0", "文件 stem 应为 test0");

    const CodeUnit* unit = result.mfile->entry_unit;
    require(unit->name == "test0", "脚本单元名字应回退到文件 stem");
    require(unit->slot_table.slots.size() == 1, "脚本 lowering 应只创建一个工作区句柄槽位");
    require(unit->basic_blocks.size() == 4, "应创建 4 个基本块");
    require(unit->entry_block == unit->basic_blocks.front().get(), "入口基本块应为第一个基本块");

    const Slot& workspace_handle = unit->slot_table.slots.front();
    require(workspace_handle.type == Slot::Hidden, "唯一槽位应为隐藏槽位");
    require(
        workspace_handle.attrs.hidden_role == SlotAttrs::WorkspaceHandle,
        "唯一槽位应为工作区句柄槽位");

    const BasicBlock* entry_block = unit->basic_blocks[0].get();
    const BasicBlock* then_block = unit->basic_blocks[1].get();
    const BasicBlock* else_block = unit->basic_blocks[2].get();
    const BasicBlock* exit_block = unit->basic_blocks[3].get();

    require(entry_block->parent == unit, "入口基本块 parent 应指向所属代码单元");
    require(exit_block->parent == unit, "exit 基本块 parent 应指向所属代码单元");
    require(
        !entry_block->instructions.empty() && entry_block->instructions.front()->parent == entry_block,
        "入口基本块中的指令 parent 应指向所属基本块");
    require(
        !exit_block->instructions.empty() && exit_block->instructions.front()->parent == exit_block,
        "exit 基本块中的指令 parent 应指向所属基本块");

    require(entry_block->has_terminator(), "入口基本块必须有终结指令");
    require(then_block->has_terminator(), "then 基本块必须有终结指令");
    require(else_block->has_terminator(), "else 基本块必须有终结指令");
    require(exit_block->has_terminator(), "exit 基本块必须有终结指令");

    require(entry_block->terminator()->type() == Instruction::Branch, "入口基本块应以分支结束");
    require(then_block->terminator()->type() == Instruction::Goto, "then 基本块应以跳转结束");
    require(else_block->terminator()->type() == Instruction::Goto, "else 基本块应以跳转结束");
    require(exit_block->terminator()->type() == Instruction::Return, "exit 基本块应以返回结束");

    require(entry_block->successors.size() == 2, "入口基本块应有两个后继");
    require(then_block->successors.size() == 1, "then 基本块应有一个后继");
    require(else_block->successors.size() == 1, "else 基本块应有一个后继");
    require(exit_block->successors.empty(), "exit 基本块不应有后继");

    require(then_block->predecessors.size() == 1, "then 基本块应只有一个前驱");
    require(else_block->predecessors.size() == 1, "else 基本块应只有一个前驱");
    require(exit_block->predecessors.size() == 2, "exit 基本块应有两个前驱");

    require(entry_block->instructions.size() == 11, "入口基本块指令数不符合预期");
    require(then_block->instructions.size() == 5, "then 基本块指令数不符合预期");
    require(else_block->instructions.size() == 3, "else 基本块指令数不符合预期");
    require(exit_block->instructions.size() == 1, "exit 基本块指令数不符合预期");

    const Instruction* maybe_apply = entry_block->instructions[5].get();
    require(
        maybe_apply != nullptr && maybe_apply->type() == Instruction::Apply,
        "入口基本块第 6 条指令应为圆括号应用");
}

void verify_printed_ir(const std::string& printed_ir) {
    require(!printed_ir.empty(), "打印结果不能为空");
    require(
        printed_ir.find("; mfile \"" TEST0_MFILE_PATH "\"") != std::string::npos,
        "应打印文件头");
    require(printed_ir.find("script @test0 {") != std::string::npos, "应打印 script 头");
    require(
        printed_ir.find("%workspace_handle = hidden(workspace_handle) @__workspace_handle__") !=
            std::string::npos,
        "应打印工作区句柄槽位");
    require(
        printed_ir.find("store_workspace %workspace_handle, @a, %2") != std::string::npos,
        "应打印 a 的工作区写回");
    require(
        printed_ir.find("%3 = apply @sin(%4)") != std::string::npos,
        "应打印 sin 的圆括号应用");
    require(
        printed_ir.find("br %7, label %if.then, label %if.else") != std::string::npos,
        "应打印条件分支");
    require(
        printed_ir.find("store_workspace %workspace_handle, @c, %10") != std::string::npos,
        "应打印 then 分支写回");
    require(
        printed_ir.find("store_workspace %workspace_handle, @c, %11") != std::string::npos,
        "应打印 else 分支写回");
    require(
        printed_ir.find("if.exit:") != std::string::npos,
        "应打印 exit 块标签");
    require(
        printed_ir.find("\n  ret") != std::string::npos,
        "应打印 exit 块返回");
    require(
        printed_ir.find("; a = 1 + 2") != std::string::npos,
        "应打印第一条源码注释");
    require(
        printed_ir.find("; b = sin(a)") != std::string::npos,
        "应打印第二条源码注释");
    require(
        printed_ir.find("; if b > 0") != std::string::npos,
        "应打印 if 条件源码注释");

    std::size_t comment_column = std::string::npos;
    std::istringstream input(printed_ir);
    std::string line;
    while (std::getline(input, line)) {
        const std::size_t current_column = line.find("; ");
        if (current_column == std::string::npos) {
            continue;
        }

        if (line.rfind("; mfile ", 0) == 0 || line == "  ; slots:") {
            continue;
        }

        if (comment_column == std::string::npos) {
            comment_column = current_column;
        } else {
            require(current_column == comment_column, "源码注释列必须对齐");
        }
    }

    require(comment_column != std::string::npos, "应至少存在一行源码注释");
}

} // namespace
} // namespace baltam

int main() {
    try {
        const baltam::SmokeArtifacts artifacts = baltam::build_test0_ir();
        baltam::verify_test0_ir(artifacts.result);
        baltam::verify_printed_ir(artifacts.printed_ir);
        std::cout << artifacts.printed_ir << '\n';
    } catch (const std::exception& ex) {
        std::cerr << "test0_smoke 失败: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
