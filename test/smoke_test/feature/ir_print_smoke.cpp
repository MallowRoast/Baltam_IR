#include "ir/ir_builder.h"
#include "ir/ir_print.h"
#include "smoke_test_common.h"

#include <iostream>
#include <memory>
#include <string>

namespace baltam {
namespace {

MFileUnit& build_two_block_script(IRBuilder& builder) {
    MFileUnit& mfile = builder.begin_file("ir_print_smoke.m");
    ScriptUnit& unit = builder.begin_script_unit("ir_print_smoke", SourceSpan::invalid());
    mfile.entry_unit = &unit;

    BasicBlock* entry = unit.create_block("entry", SourceSpan::invalid());
    BasicBlock* next = unit.create_block("next", SourceSpan::invalid());
    smoke_test::require(entry != nullptr && next != nullptr, "应成功创建两个基本块");
    smoke_test::require(unit.set_entry_block(entry), "应成功设置入口基本块");

    builder.set_current_unit(&unit);
    builder.set_insert_point(entry);
    auto go = std::make_unique<GotoInst>();
    go->target = next;
    builder.append_instruction(std::move(go));

    builder.set_insert_point(next);
    auto ret = std::make_unique<ReturnInst>();
    builder.append_instruction(std::move(ret));

    return mfile;
}

IRPrintOptions compact_print_options() {
    IRPrintOptions options;
    options.print_file_header = false;
    options.print_slot_table = false;
    options.print_source_comments = false;
    options.print_type_facts = false;
    return options;
}

void verify_default_prints_all_block_predecessors() {
    IRBuilder builder;
    const MFileUnit& mfile = build_two_block_script(builder);

    const std::string text = format_ir(mfile, compact_print_options());
    smoke_test::require(
        text.find("\nL0: ; Type = entry, preds = []") != std::string::npos,
        "默认打印 IR 时，入口基本块也应显示类别注释和空前驱列表");
    smoke_test::require(
        text.find("\nL1: ; Type = next, preds = [L0]") != std::string::npos,
        "默认打印 IR 时，普通基本块应显示类别注释和前驱列表");
    smoke_test::require(
        text.find("br label L1") != std::string::npos,
        "打印 IR 分支目标时 block label 不应带 % 前缀");
}

void verify_cfg_comments_can_be_disabled() {
    IRBuilder builder;
    const MFileUnit& mfile = build_two_block_script(builder);

    IRPrintOptions options = compact_print_options();
    options.print_block_predecessors = false;
    const std::string text = format_ir(mfile, options);

    smoke_test::require(
        text.find("\nL0: ; Type = entry") != std::string::npos &&
            text.find("\nL1: ; Type = next") != std::string::npos,
        "关闭 CFG 前驱注释后仍应打印基本块数字标签和类别注释");
    smoke_test::require(
        text.find("preds =") == std::string::npos,
        "print_block_predecessors=false 时不应打印前驱注释");
}

} // namespace
} // namespace baltam

int main() {
    try {
        baltam::verify_default_prints_all_block_predecessors();
        baltam::verify_cfg_comments_can_be_disabled();
    } catch (const std::exception& ex) {
        std::cerr << "ir_print_smoke 失败: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
