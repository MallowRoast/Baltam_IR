#include "pass/cfg_simplification_pass.h"
#include "pass/constant_deduplication_pass.h"
#include "pass/dead_branch_elimination_pass.h"
#include "pass/dead_code_elimination_pass.h"
#include "pass/ir_pass_manager.h"
#include "pass/load_forwarding_pass.h"
#include "pass/unreachable_block_elimination_pass.h"
#include "smoke_test_common.h"

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace baltam {
namespace {

std::unique_ptr<MFileUnit> make_mfile() {
    auto mfile = std::make_unique<MFileUnit>();
    mfile->path = NormalizedPath("test.m");

    auto script = std::make_unique<ScriptUnit>();
    script->name = "script";
    script->file = mfile.get();
    ScriptUnit* script_ptr = script.get();

    auto function = std::make_unique<FunctionUnit>();
    function->name = "f";
    function->file = mfile.get();

    mfile->entry_unit = script_ptr;
    mfile->code_units.push_back(std::move(script));
    mfile->code_units.push_back(std::move(function));
    return mfile;
}

class RecordingFilePass final : public IRFilePass {
public:
    explicit RecordingFilePass(std::vector<std::string>& log) : log_(log) {}

    [[nodiscard]] std::string_view name() const noexcept override {
        return "record.file";
    }

    IRPassResult run(MFileUnit& mfile, IRPassContext& context) override {
        smoke_test::require(context.file == &mfile, "file context should point to mfile");
        smoke_test::require(context.unit == nullptr, "file context should not have unit");
        log_.push_back("file:" + mfile.path.string());
        return {};
    }

private:
    std::vector<std::string>& log_;
};

class RecordingCodeUnitPass final : public IRCodeUnitPass {
public:
    explicit RecordingCodeUnitPass(std::vector<std::string>& log) : log_(log) {}

    [[nodiscard]] std::string_view name() const noexcept override {
        return "record.unit";
    }

    IRPassResult run(CodeUnit& unit, IRPassContext& context) override {
        smoke_test::require(context.file != nullptr, "unit context should have file");
        smoke_test::require(context.unit == &unit, "unit context should point to unit");

        IRPassResult result;
        result.changed = unit.is_function();
        log_.push_back("unit:" + unit.name);
        return result;
    }

private:
    std::vector<std::string>& log_;
};

class FailingCodeUnitPass final : public IRCodeUnitPass {
public:
    explicit FailingCodeUnitPass(std::vector<std::string>& log) : log_(log) {}

    [[nodiscard]] std::string_view name() const noexcept override {
        return "fail.unit";
    }

    IRPassResult run(CodeUnit& unit, IRPassContext&) override {
        log_.push_back("fail:" + unit.name);

        IRPassResult result;
        result.report(IRPassDiagnostic::Error, "intentional pass failure", unit.source_span);
        return result;
    }

private:
    std::vector<std::string>& log_;
};

class UnreachedFilePass final : public IRFilePass {
public:
    explicit UnreachedFilePass(std::vector<std::string>& log) : log_(log) {}

    [[nodiscard]] std::string_view name() const noexcept override {
        return "unreached.file";
    }

    IRPassResult run(MFileUnit&, IRPassContext&) override {
        log_.push_back("unreached");
        return {};
    }

private:
    std::vector<std::string>& log_;
};

void verify_successful_pipeline() {
    std::vector<std::string> log;
    std::unique_ptr<MFileUnit> mfile = make_mfile();

    IRPassManager manager;
    manager.add_pass<RecordingFilePass>(log);
    manager.add_pass<RecordingCodeUnitPass>(log);

    const IRPassManagerResult result = manager.run(*mfile);
    smoke_test::require(result.ok(), "pass manager result should be ok");
    smoke_test::require(result.changed, "function invocation should mark pipeline changed");
    smoke_test::require(result.diagnostics.empty(), "successful pipeline should not emit diagnostics");
    smoke_test::require(result.pass_runs.size() == 2, "pipeline should record two pass summaries");

    smoke_test::require(result.pass_runs[0].pass_name == "record.file", "file pass name mismatch");
    smoke_test::require(result.pass_runs[0].scope == IRPassScope::File, "file pass scope mismatch");
    smoke_test::require(result.pass_runs[0].invocations == 1, "file pass should run once");

    smoke_test::require(result.pass_runs[1].pass_name == "record.unit", "unit pass name mismatch");
    smoke_test::require(result.pass_runs[1].scope == IRPassScope::CodeUnit, "unit pass scope mismatch");
    smoke_test::require(result.pass_runs[1].invocations == 2, "unit pass should visit file code units");
    smoke_test::require(result.pass_runs[1].changed, "unit pass summary should be changed");
    smoke_test::require(
        result.pass_runs[1].changed_invocations == 1,
        "only the function unit invocation should be changed");

    const std::vector<std::string> expected_log = {
        "file:test.m",
        "unit:script",
        "unit:f",
    };
    smoke_test::require(log == expected_log, "pass invocation order mismatch");
    smoke_test::require(ir_pass_scope_name(IRPassScope::CodeUnit) == "code-unit",
                        "scope name should be stable");
}

void verify_error_stops_pipeline() {
    std::vector<std::string> log;
    std::unique_ptr<MFileUnit> mfile = make_mfile();

    IRPassManager manager;
    manager.add_pass<FailingCodeUnitPass>(log);
    manager.add_pass<UnreachedFilePass>(log);

    const IRPassManagerResult result = manager.run(*mfile);
    smoke_test::require(!result.ok(), "failing pass should make result not ok");
    smoke_test::require(!result.changed, "failing pass did not report changes");
    smoke_test::require(result.diagnostics.size() == 1, "failing pass should emit one diagnostic");
    smoke_test::require(result.diagnostics[0].pass_name == "fail.unit",
                        "diagnostic should be attributed to pass");
    smoke_test::require(result.pass_runs.size() == 1, "pipeline should stop after failing pass");
    smoke_test::require(result.pass_runs[0].invocations == 1,
                        "failing unit pass should stop after first invocation");

    const std::vector<std::string> expected_log = {"fail:script"};
    smoke_test::require(log == expected_log, "stop_on_error should skip remaining invocations");
}

void append_return(BasicBlock& block) {
    auto ret = std::make_unique<ReturnInst>();
    ret->parent = &block;
    block.instructions.push_back(std::move(ret));
}

void append_goto(BasicBlock& block, BasicBlock& target) {
    auto go = std::make_unique<GotoInst>();
    go->target = &target;
    go->parent = &block;
    block.instructions.push_back(std::move(go));

    block.successors.push_back(&target);
    target.predecessors.push_back(&block);
}

void append_branch(
    BasicBlock& block,
    ValueId condition,
    BasicBlock& true_target,
    BasicBlock& false_target) {
    auto branch = std::make_unique<BranchInst>();
    branch->condition = condition;
    branch->true_target = &true_target;
    branch->false_target = &false_target;
    branch->parent = &block;
    block.instructions.push_back(std::move(branch));

    block.successors.push_back(&true_target);
    true_target.predecessors.push_back(&block);
    if (&false_target != &true_target) {
        block.successors.push_back(&false_target);
        false_target.predecessors.push_back(&block);
    }
}

void append_store(BasicBlock& block, Slot slot, ValueId value) {
    auto inst = std::make_unique<StoreSlotInst>();
    inst->slot = slot;
    inst->value = value;
    inst->parent = &block;
    block.instructions.push_back(std::move(inst));
}

LoadSlotInst* append_load(BasicBlock& block, Slot slot, ValueId result) {
    auto inst = std::make_unique<LoadSlotInst>();
    inst->slot = slot;
    inst->result = result;
    inst->parent = &block;
    LoadSlotInst* raw = inst.get();
    block.instructions.push_back(std::move(inst));
    return raw;
}

void append_call(BasicBlock& block, std::string callee, const std::vector<ValueId>& arguments) {
    auto inst = std::make_unique<CallInst>();
    inst->callee_kind = CallInst::Direct;
    inst->dispatch_type = Dynamic;
    inst->callee = InternedString(std::move(callee));
    inst->arguments.reserve(arguments.size());
    for (ValueId value : arguments) {
        inst->arguments.push_back(value);
    }
    inst->parent = &block;
    block.instructions.push_back(std::move(inst));
}

void verify_ube_removes_unreachable_blocks() {
    auto mfile = std::make_unique<MFileUnit>();
    mfile->path = NormalizedPath("ube.m");

    auto script = std::make_unique<ScriptUnit>();
    script->name = "ube";
    script->file = mfile.get();

    BasicBlock* entry = script->create_block("entry", SourceSpan::invalid());
    BasicBlock* exit = script->create_block("exit", SourceSpan::invalid());
    BasicBlock* dead = script->create_block("dead", SourceSpan::invalid());
    BasicBlock* dead_exit = script->create_block("dead.exit", SourceSpan::invalid());
    smoke_test::require(entry != nullptr && exit != nullptr && dead != nullptr && dead_exit != nullptr,
                        "UBE test blocks should be created");
    smoke_test::require(script->set_entry_block(entry), "UBE test should set entry block");

    append_goto(*entry, *exit);
    append_return(*exit);
    append_goto(*dead, *dead_exit);
    append_return(*dead_exit);

    mfile->entry_unit = script.get();
    mfile->code_units.push_back(std::move(script));

    IRPassManagerOptions options;
    options.verify_after_pipeline = true;

    IRPassManager manager(options);
    manager.add_pass<UnreachableBlockEliminationPass>();

    IRPassManagerResult result = manager.run(*mfile);
    smoke_test::require(result.ok(), "UBE should keep IR verifier-clean");
    smoke_test::require(result.changed, "UBE should report changed when blocks are removed");
    smoke_test::require(result.pass_runs.size() == 1, "UBE should record one summary");
    smoke_test::require(result.pass_runs[0].pass_name == "unreachable-block-elimination",
                        "UBE pass name mismatch");
    smoke_test::require(result.pass_runs[0].changed_invocations == 1,
                        "UBE changed invocation count mismatch");

    const CodeUnit& unit = *mfile->entry_unit;
    smoke_test::require(unit.basic_blocks.size() == 2, "UBE should keep only reachable blocks");
    smoke_test::require(unit.basic_blocks[0].get() == entry, "UBE should preserve entry block object");
    smoke_test::require(unit.basic_blocks[1].get() == exit, "UBE should preserve reachable exit block");
    smoke_test::require(entry->successors.size() == 1 && entry->successors[0] == exit,
                        "UBE should preserve reachable successor edge");
    smoke_test::require(exit->predecessors.size() == 1 && exit->predecessors[0] == entry,
                        "UBE should rebuild reachable predecessor edge");
}

void verify_ube_skips_value_escape() {
    ScriptUnit unit;
    unit.name = "ube_escape";
    BasicBlock* entry = unit.create_block("entry", SourceSpan::invalid());
    BasicBlock* dead = unit.create_block("dead", SourceSpan::invalid());
    smoke_test::require(entry != nullptr && dead != nullptr, "UBE escape blocks should be created");
    smoke_test::require(unit.set_entry_block(entry), "UBE escape should set entry block");

    unit.value_table.values.push_back({
        ValueId(0),
        0,
        {},
        nullptr,
    });

    auto ret = std::make_unique<ReturnInst>();
    ret->values.push_back(ValueId(0));
    ret->parent = entry;
    entry->instructions.push_back(std::move(ret));

    auto constant = std::make_unique<ConstInst>();
    constant->result = ValueId(0);
    constant->parent = dead;
    unit.value_table.values[0].def = constant.get();
    dead->instructions.push_back(std::move(constant));

    auto dead_ret = std::make_unique<ReturnInst>();
    dead_ret->parent = dead;
    dead->instructions.push_back(std::move(dead_ret));

    IRPassContext context;
    context.unit = &unit;

    UnreachableBlockEliminationPass pass;
    IRPassResult result = pass.run(unit, context);
    smoke_test::require(result.ok(), "UBE value escape warning should not be an error");
    smoke_test::require(!result.changed, "UBE should skip unsafe value escape case");
    smoke_test::require(result.diagnostics.size() == 1, "UBE skip should emit one warning");
    smoke_test::require(result.diagnostics[0].severity == IRPassDiagnostic::Warning,
                        "UBE skip diagnostic should be warning");
    smoke_test::require(unit.basic_blocks.size() == 2, "UBE skip should preserve all blocks");
    smoke_test::require(unit.value_table.values[0].def != nullptr,
                        "UBE skip should preserve value table def");
}

void verify_cfg_simplification_removes_empty_goto_blocks() {
    auto mfile = std::make_unique<MFileUnit>();
    mfile->path = NormalizedPath("cfg_simplify.m");

    auto script = std::make_unique<ScriptUnit>();
    script->name = "cfg_simplify";
    script->file = mfile.get();

    BasicBlock* entry = script->create_block("entry", SourceSpan::invalid());
    BasicBlock* bridge = script->create_block("bridge", SourceSpan::invalid());
    BasicBlock* exit = script->create_block("exit", SourceSpan::invalid());
    smoke_test::require(entry != nullptr && bridge != nullptr && exit != nullptr,
                        "CFG simplify test blocks should be created");
    smoke_test::require(script->set_entry_block(entry), "CFG simplify test should set entry block");

    append_goto(*entry, *bridge);
    append_goto(*bridge, *exit);
    append_return(*exit);

    mfile->entry_unit = script.get();
    mfile->code_units.push_back(std::move(script));

    IRPassManagerOptions options;
    options.verify_after_pipeline = true;

    IRPassManager manager(options);
    manager.add_pass<CFGSimplificationPass>();

    IRPassManagerResult result = manager.run(*mfile);
    smoke_test::require(result.ok(), "CFG simplify should keep IR verifier-clean");
    smoke_test::require(result.changed, "CFG simplify should remove bridge block");
    smoke_test::require(result.pass_runs.size() == 1, "CFG simplify should record one summary");
    smoke_test::require(result.pass_runs[0].pass_name == "cfg-simplification",
                        "CFG simplify pass name mismatch");

    const CodeUnit& unit = *mfile->entry_unit;
    smoke_test::require(unit.basic_blocks.size() == 2,
                        "CFG simplify should keep entry and exit only");
    smoke_test::require(unit.basic_blocks[0].get() == entry, "CFG simplify should keep entry");
    smoke_test::require(unit.basic_blocks[1].get() == exit, "CFG simplify should keep exit");
    smoke_test::require(entry->successors.size() == 1 && entry->successors[0] == exit,
                        "CFG simplify should redirect successor edge");
    smoke_test::require(exit->predecessors.size() == 1 && exit->predecessors[0] == entry,
                        "CFG simplify should rebuild predecessor edge");

    const Instruction* terminator = entry->terminator();
    smoke_test::require(terminator != nullptr && terminator->type() == Instruction::Goto,
                        "CFG simplify entry should still end in goto");
    const auto& go = static_cast<const GotoInst&>(*terminator);
    smoke_test::require(go.target == exit, "CFG simplify should redirect goto target");
}

void verify_cfg_simplification_preserves_entry_bridge() {
    ScriptUnit unit;
    unit.name = "cfg_simplify_entry";

    BasicBlock* entry = unit.create_block("entry", SourceSpan::invalid());
    BasicBlock* exit = unit.create_block("exit", SourceSpan::invalid());
    smoke_test::require(entry != nullptr && exit != nullptr,
                        "CFG simplify entry test blocks should be created");
    smoke_test::require(unit.set_entry_block(entry), "CFG simplify entry test should set entry");

    append_goto(*entry, *exit);
    append_return(*exit);

    IRPassContext context;
    context.unit = &unit;

    CFGSimplificationPass pass;
    IRPassResult result = pass.run(unit, context);
    smoke_test::require(result.ok(), "CFG simplify entry case should not error");
    smoke_test::require(!result.changed, "CFG simplify should not delete entry block");
    smoke_test::require(unit.basic_blocks.size() == 2,
                        "CFG simplify should preserve entry bridge shape");
}

void verify_cfg_simplification_runs_ube() {
    ScriptUnit unit;
    unit.name = "cfg_simplify_ube";

    BasicBlock* entry = unit.create_block("entry", SourceSpan::invalid());
    BasicBlock* dead = unit.create_block("dead", SourceSpan::invalid());
    smoke_test::require(entry != nullptr && dead != nullptr,
                        "CFG simplify UBE test blocks should be created");
    smoke_test::require(unit.set_entry_block(entry), "CFG simplify UBE test should set entry");

    append_return(*entry);
    append_return(*dead);

    IRPassContext context;
    context.unit = &unit;

    CFGSimplificationPass pass;
    IRPassResult result = pass.run(unit, context);
    smoke_test::require(result.ok(), "CFG simplify UBE case should not error");
    smoke_test::require(result.changed, "CFG simplify should delete unreachable blocks");
    smoke_test::require(unit.basic_blocks.size() == 1,
                        "CFG simplify should keep only reachable block");
    smoke_test::require(unit.basic_blocks[0].get() == entry,
                        "CFG simplify should preserve reachable entry");
}

void verify_cfg_simplification_normalizes_cfg_edges() {
    ScriptUnit unit;
    unit.name = "cfg_simplify_edges";

    BasicBlock* entry = unit.create_block("entry", SourceSpan::invalid());
    BasicBlock* exit = unit.create_block("exit", SourceSpan::invalid());
    BasicBlock* stale = unit.create_block("stale", SourceSpan::invalid());
    smoke_test::require(entry != nullptr && exit != nullptr && stale != nullptr,
                        "CFG simplify edge test blocks should be created");
    smoke_test::require(unit.set_entry_block(entry), "CFG simplify edge test should set entry");

    auto go = std::make_unique<GotoInst>();
    go->target = exit;
    go->parent = entry;
    entry->instructions.push_back(std::move(go));
    entry->successors.push_back(stale);
    exit->predecessors.push_back(stale);

    append_return(*exit);
    append_return(*stale);

    IRPassContext context;
    context.unit = &unit;

    CFGSimplificationPass pass;
    IRPassResult result = pass.run(unit, context);
    smoke_test::require(result.ok(), "CFG simplify edge normalize should not error");
    smoke_test::require(result.changed, "CFG simplify should normalize stale edges");
    smoke_test::require(entry->successors.size() == 1 && entry->successors[0] == exit,
                        "CFG simplify should rebuild successors from terminator");
    smoke_test::require(exit->predecessors.size() == 1 && exit->predecessors[0] == entry,
                        "CFG simplify should rebuild predecessors from successors");
    smoke_test::require(unit.basic_blocks.size() == 2,
                        "CFG simplify should also remove stale unreachable block");
}

ConstInst* append_const(BasicBlock& block, ValueId value_id, std::int64_t value = 42) {
    auto constant = std::make_unique<ConstInst>();
    constant->result = value_id;
    constant->value = Int64Constant{value};
    constant->parent = &block;
    ConstInst* raw_constant = constant.get();
    block.instructions.push_back(std::move(constant));
    return raw_constant;
}

void verify_cfg_simplification_merges_linear_block() {
    ScriptUnit unit;
    unit.name = "cfg_simplify_linear";

    BasicBlock* entry = unit.create_block("entry", SourceSpan::invalid());
    BasicBlock* middle = unit.create_block("middle", SourceSpan::invalid());
    BasicBlock* exit = unit.create_block("exit", SourceSpan::invalid());
    smoke_test::require(entry != nullptr && middle != nullptr && exit != nullptr,
                        "CFG simplify linear test blocks should be created");
    smoke_test::require(unit.set_entry_block(entry), "CFG simplify linear test should set entry");

    unit.value_table.values.push_back({ValueId(0), 0, {}, nullptr});

    append_goto(*entry, *middle);
    append_const(*middle, ValueId(0));
    unit.value_table.values[0].def = middle->instructions.front().get();
    append_goto(*middle, *exit);

    auto ret = std::make_unique<ReturnInst>();
    ret->values.push_back(ValueId(0));
    ret->parent = exit;
    exit->instructions.push_back(std::move(ret));

    IRPassContext context;
    context.unit = &unit;

    CFGSimplificationPass pass;
    IRPassResult result = pass.run(unit, context);
    smoke_test::require(result.ok(), "CFG simplify linear merge should not error");
    smoke_test::require(result.changed, "CFG simplify should merge linear block");
    smoke_test::require(unit.basic_blocks.size() == 2,
                        "CFG simplify should remove middle linear block");
    smoke_test::require(unit.basic_blocks[0].get() == entry,
                        "CFG simplify should keep entry after linear merge");
    smoke_test::require(unit.basic_blocks[1].get() == exit,
                        "CFG simplify should keep exit after linear merge");
    smoke_test::require(entry->instructions.size() == 2,
                        "CFG simplify should move middle instructions into predecessor");
    smoke_test::require(entry->instructions[0]->type() == Instruction::Const,
                        "CFG simplify should move const before merged terminator");
    smoke_test::require(entry->instructions[0]->parent == entry,
                        "CFG simplify should update moved instruction parent");
    smoke_test::require(unit.value_table.values[0].def == entry->instructions[0].get(),
                        "CFG simplify should keep ValueTable def pointing to moved instruction");
    smoke_test::require(entry->successors.size() == 1 && entry->successors[0] == exit,
                        "CFG simplify should redirect linear successor");
    smoke_test::require(exit->predecessors.size() == 1 && exit->predecessors[0] == entry,
                        "CFG simplify should rebuild linear predecessor");
}

void verify_cfg_simplification_merges_return_block_with_body() {
    ScriptUnit unit;
    unit.name = "cfg_simplify_return";

    BasicBlock* entry = unit.create_block("entry", SourceSpan::invalid());
    BasicBlock* exit = unit.create_block("exit", SourceSpan::invalid());
    smoke_test::require(entry != nullptr && exit != nullptr,
                        "CFG simplify return test blocks should be created");
    smoke_test::require(unit.set_entry_block(entry), "CFG simplify return test should set entry");

    unit.value_table.values.push_back({ValueId(0), 0, {}, nullptr});

    append_goto(*entry, *exit);
    append_const(*exit, ValueId(0));
    unit.value_table.values[0].def = exit->instructions.front().get();

    auto ret = std::make_unique<ReturnInst>();
    ret->values.push_back(ValueId(0));
    ret->parent = exit;
    exit->instructions.push_back(std::move(ret));

    IRPassContext context;
    context.unit = &unit;

    CFGSimplificationPass pass;
    IRPassResult result = pass.run(unit, context);
    smoke_test::require(result.ok(), "CFG simplify return merge should not error");
    smoke_test::require(result.changed, "CFG simplify should merge return block with body");
    smoke_test::require(unit.basic_blocks.size() == 1,
                        "CFG simplify should remove merged return block");
    smoke_test::require(unit.basic_blocks[0].get() == entry,
                        "CFG simplify should keep entry after return merge");
    smoke_test::require(entry->instructions.size() == 2,
                        "CFG simplify should move return block body into predecessor");
    smoke_test::require(entry->instructions[0]->type() == Instruction::Const,
                        "CFG simplify should move const before merged return");
    smoke_test::require(entry->instructions[1]->type() == Instruction::Return,
                        "CFG simplify should merge return terminator into predecessor");
    smoke_test::require(entry->successors.empty(),
                        "CFG simplify should clear merged return successors");
}

void verify_cfg_simplification_ignores_source_boundary() {
    ScriptUnit unit;
    unit.name = "cfg_simplify_source";

    BasicBlock* entry = unit.create_block("entry", SourceSpan::invalid());
    BasicBlock* middle = unit.create_block("middle", SourceSpan(1, 2));
    BasicBlock* exit = unit.create_block("exit", SourceSpan::invalid());
    smoke_test::require(entry != nullptr && middle != nullptr && exit != nullptr,
                        "CFG simplify source test blocks should be created");
    smoke_test::require(unit.set_entry_block(entry), "CFG simplify source test should set entry");

    append_goto(*entry, *middle);
    append_const(*middle, ValueId(0));
    append_goto(*middle, *exit);
    append_return(*exit);

    IRPassContext context;
    context.unit = &unit;

    CFGSimplificationPass pass;
    IRPassResult result = pass.run(unit, context);
    smoke_test::require(result.ok(), "CFG simplify source boundary case should not error");
    smoke_test::require(result.changed, "CFG simplify should ignore source boundary");
    smoke_test::require(unit.basic_blocks.size() == 2,
                        "CFG simplify should remove source boundary block");
}

void verify_dead_branch_elimination_rewrites_constant_branch() {
    auto mfile = std::make_unique<MFileUnit>();
    mfile->path = NormalizedPath("dead_branch.m");

    auto script = std::make_unique<ScriptUnit>();
    script->name = "dead_branch";
    script->file = mfile.get();

    BasicBlock* entry = script->create_block("entry", SourceSpan::invalid());
    BasicBlock* then_block = script->create_block("then", SourceSpan::invalid());
    BasicBlock* else_block = script->create_block("else", SourceSpan::invalid());
    smoke_test::require(entry != nullptr && then_block != nullptr && else_block != nullptr,
                        "dead branch test blocks should be created");
    smoke_test::require(script->set_entry_block(entry), "dead branch test should set entry");

    script->value_table.values.push_back({ValueId(0), 0, {}, nullptr});

    ConstInst* condition = append_const(*entry, ValueId(0), 1);
    script->value_table.values[0].def = condition;
    append_branch(*entry, ValueId(0), *then_block, *else_block);
    append_return(*then_block);
    append_return(*else_block);

    mfile->entry_unit = script.get();
    mfile->code_units.push_back(std::move(script));

    IRPassManagerOptions options;
    options.verify_after_pipeline = true;

    IRPassManager manager(options);
    manager.add_pass<DeadBranchEliminationPass>();

    IRPassManagerResult result = manager.run(*mfile);
    smoke_test::require(result.ok(), "dead branch elimination should keep IR verifier-clean");
    smoke_test::require(result.changed, "dead branch elimination should report changed");
    smoke_test::require(result.pass_runs.size() == 1, "dead branch elimination should record one summary");
    smoke_test::require(result.pass_runs[0].pass_name == "dead-branch-elimination",
                        "dead branch elimination pass name mismatch");

    CodeUnit& unit = *mfile->entry_unit;
    smoke_test::require(unit.basic_blocks.size() == 3,
                        "dead branch elimination should not remove blocks by itself");
    smoke_test::require(entry->instructions.back()->type() == Instruction::Goto,
                        "dead branch elimination should rewrite branch to goto");
    const auto& go = static_cast<const GotoInst&>(*entry->instructions.back());
    smoke_test::require(go.target == then_block,
                        "dead branch elimination should select the true target");
    smoke_test::require(entry->successors.size() == 1 && entry->successors[0] == then_block,
                        "dead branch elimination should keep only the chosen successor");
    smoke_test::require(then_block->predecessors.size() == 1 && then_block->predecessors[0] == entry,
                        "dead branch elimination should keep chosen predecessor");
    smoke_test::require(else_block->predecessors.empty(),
                        "dead branch elimination should remove dead predecessor");
}

void verify_dead_code_elimination_removes_unused_constants() {
    auto mfile = std::make_unique<MFileUnit>();
    mfile->path = NormalizedPath("dead_code_const.m");

    auto script = std::make_unique<ScriptUnit>();
    script->name = "dead_code_const";
    script->file = mfile.get();

    BasicBlock* entry = script->create_block("entry", SourceSpan::invalid());
    smoke_test::require(entry != nullptr, "dead code const test block should be created");
    smoke_test::require(script->set_entry_block(entry), "dead code const test should set entry");

    script->value_table.values.push_back({ValueId(0), 0, {}, nullptr});
    script->value_table.values.push_back({ValueId(1), 0, {}, nullptr});

    ConstInst* dead_const = append_const(*entry, ValueId(0), 1);
    ConstInst* live_const = append_const(*entry, ValueId(1), 2);
    script->value_table.values[0].def = dead_const;
    script->value_table.values[1].def = live_const;

    auto ret = std::make_unique<ReturnInst>();
    ret->values.push_back(ValueId(1));
    ret->parent = entry;
    entry->instructions.push_back(std::move(ret));

    mfile->entry_unit = script.get();
    mfile->code_units.push_back(std::move(script));

    IRPassManagerOptions options;
    options.verify_after_pipeline = true;

    IRPassManager manager(options);
    manager.add_pass<DeadCodeEliminationPass>();

    IRPassManagerResult result = manager.run(*mfile);
    smoke_test::require(result.ok(), "dead code elimination should keep const case verifier-clean");
    smoke_test::require(result.changed, "dead code elimination should remove unused const");
    smoke_test::require(result.pass_runs.size() == 1, "dead code elimination should record one summary");
    smoke_test::require(result.pass_runs[0].pass_name == "dead-code-elimination",
                        "dead code elimination pass name mismatch");

    CodeUnit& unit = *mfile->entry_unit;
    BasicBlock& block = *unit.entry_block;
    smoke_test::require(block.instructions.size() == 2,
                        "dead code elimination should keep live const and return");
    smoke_test::require(block.instructions[0]->type() == Instruction::Const,
                        "dead code elimination should keep live const");
    const auto& remaining_const = static_cast<const ConstInst&>(*block.instructions[0]);
    smoke_test::require(remaining_const.result == ValueId(1),
                        "dead code elimination should remove only the unused const");
    smoke_test::require(unit.value_table.values[0].def == nullptr,
                        "dead code elimination should clear removed const def");
    smoke_test::require(unit.value_table.values[1].def == block.instructions[0].get(),
                        "dead code elimination should keep live const def");
}

void verify_dead_code_elimination_removes_overwritten_store() {
    auto mfile = std::make_unique<MFileUnit>();
    mfile->path = NormalizedPath("dead_store.m");

    auto script = std::make_unique<ScriptUnit>();
    script->name = "dead_store";
    script->file = mfile.get();

    BasicBlock* entry = script->create_block("entry", SourceSpan::invalid());
    smoke_test::require(entry != nullptr, "dead store test block should be created");
    smoke_test::require(script->set_entry_block(entry), "dead store test should set entry");

    const Slot slot{SlotId(0), SlotTag::Local};
    script->slot_table.slots.push_back({
        slot,
        InternedString("x"),
        SourceSpan::invalid(),
        SlotValueType::Unknown,
    });

    script->value_table.values.push_back({ValueId(0), 0, {}, nullptr});
    script->value_table.values.push_back({ValueId(1), 0, {}, nullptr});
    script->value_table.values.push_back({ValueId(2), 0, {}, nullptr});

    ConstInst* dead_value = append_const(*entry, ValueId(0), 10);
    ConstInst* live_value = append_const(*entry, ValueId(1), 20);
    script->value_table.values[0].def = dead_value;
    script->value_table.values[1].def = live_value;
    append_store(*entry, slot, ValueId(0));
    append_store(*entry, slot, ValueId(1));
    LoadSlotInst* load = append_load(*entry, slot, ValueId(2));
    script->value_table.values[2].def = load;

    auto ret = std::make_unique<ReturnInst>();
    ret->values.push_back(ValueId(2));
    ret->parent = entry;
    entry->instructions.push_back(std::move(ret));

    mfile->entry_unit = script.get();
    mfile->code_units.push_back(std::move(script));

    IRPassManagerOptions options;
    options.verify_after_pipeline = true;

    IRPassManager manager(options);
    manager.add_pass<DeadCodeEliminationPass>();

    IRPassManagerResult result = manager.run(*mfile);
    smoke_test::require(result.ok(), "dead code elimination should keep store case verifier-clean");
    smoke_test::require(result.changed, "dead code elimination should remove overwritten store");

    CodeUnit& unit = *mfile->entry_unit;
    BasicBlock& block = *unit.entry_block;
    smoke_test::require(block.instructions.size() == 4,
                        "dead code elimination should remove dead const and overwritten store");
    smoke_test::require(block.instructions[0]->type() == Instruction::Const,
                        "dead code elimination should keep live store value const");
    const auto& remaining_const = static_cast<const ConstInst&>(*block.instructions[0]);
    smoke_test::require(remaining_const.result == ValueId(1),
                        "dead code elimination should drop const used only by removed store");
    smoke_test::require(block.instructions[1]->type() == Instruction::StoreSlot,
                        "dead code elimination should keep final store before load");
    const auto& remaining_store = static_cast<const StoreSlotInst&>(*block.instructions[1]);
    smoke_test::require(remaining_store.value == ValueId(1),
                        "dead code elimination should keep the overwritten store value");
    smoke_test::require(block.instructions[2]->type() == Instruction::LoadSlot,
                        "dead code elimination should keep load consuming final store");
    smoke_test::require(block.instructions[3]->type() == Instruction::Return,
                        "dead code elimination should keep return");
    smoke_test::require(unit.value_table.values[0].def == nullptr,
                        "dead code elimination should clear const used only by removed store");
    smoke_test::require(unit.value_table.values[1].def == block.instructions[0].get(),
                        "dead code elimination should keep live const def");
}

void verify_constant_deduplication_merges_duplicate_constants() {
    auto mfile = std::make_unique<MFileUnit>();
    mfile->path = NormalizedPath("const_dedup.m");

    auto script = std::make_unique<ScriptUnit>();
    script->name = "const_dedup";
    script->file = mfile.get();

    BasicBlock* entry = script->create_block("entry", SourceSpan::invalid());
    smoke_test::require(entry != nullptr, "constant dedup test block should be created");
    smoke_test::require(script->set_entry_block(entry), "constant dedup test should set entry");

    script->value_table.values.push_back({ValueId(0), 0, {}, nullptr});
    script->value_table.values.push_back({ValueId(1), 0, {}, nullptr});
    script->value_table.values.push_back({ValueId(2), 0, {}, nullptr});
    script->value_table.values.push_back({ValueId(3), 0, {}, nullptr});

    ConstInst* first_const = append_const(*entry, ValueId(0), 42);
    ConstInst* duplicate_const = append_const(*entry, ValueId(1), 42);
    ConstInst* other_const = append_const(*entry, ValueId(2), 7);
    script->value_table.values[0].def = first_const;
    script->value_table.values[1].def = duplicate_const;
    script->value_table.values[2].def = other_const;

    auto binary = std::make_unique<BinaryInst>();
    binary->result = ValueId(3);
    binary->op = Add;
    binary->lhs = ValueId(1);
    binary->rhs = ValueId(2);
    binary->parent = entry;
    script->value_table.values[3].def = binary.get();
    entry->instructions.push_back(std::move(binary));

    auto ret = std::make_unique<ReturnInst>();
    ret->values.push_back(ValueId(1));
    ret->values.push_back(ValueId(3));
    ret->parent = entry;
    entry->instructions.push_back(std::move(ret));

    mfile->entry_unit = script.get();
    mfile->code_units.push_back(std::move(script));

    IRPassManagerOptions options;
    options.verify_after_pipeline = true;

    IRPassManager manager(options);
    manager.add_pass<ConstantDeduplicationPass>();

    IRPassManagerResult result = manager.run(*mfile);
    smoke_test::require(result.ok(), "constant dedup should keep IR verifier-clean");
    smoke_test::require(result.changed, "constant dedup should report changed");
    smoke_test::require(result.pass_runs.size() == 1, "constant dedup should record one summary");
    smoke_test::require(result.pass_runs[0].pass_name == "constant-deduplication",
                        "constant dedup pass name mismatch");

    CodeUnit& unit = *mfile->entry_unit;
    BasicBlock& block = *unit.entry_block;
    smoke_test::require(block.instructions.size() == 4,
                        "constant dedup should remove one duplicate const");
    smoke_test::require(block.instructions[0]->type() == Instruction::Const,
                        "constant dedup should keep canonical const first");
    smoke_test::require(block.instructions[1]->type() == Instruction::Const,
                        "constant dedup should preserve distinct const");
    smoke_test::require(block.instructions[2]->type() == Instruction::Binary,
                        "constant dedup should preserve binary instruction");
    smoke_test::require(block.instructions[3]->type() == Instruction::Return,
                        "constant dedup should preserve return instruction");

    const auto& canonical_const = static_cast<const ConstInst&>(*block.instructions[0]);
    const auto& distinct_const = static_cast<const ConstInst&>(*block.instructions[1]);
    smoke_test::require(canonical_const.result == ValueId(0),
                        "constant dedup should preserve canonical ValueId");
    smoke_test::require(distinct_const.result == ValueId(2),
                        "constant dedup should keep distinct constant ValueId");
    smoke_test::require(unit.value_table.values[0].def == block.instructions[0].get(),
                        "constant dedup should keep canonical value table def");
    smoke_test::require(unit.value_table.values[1].def == nullptr,
                        "constant dedup should clear duplicate value table def");

    const auto& rewritten_binary = static_cast<const BinaryInst&>(*block.instructions[2]);
    smoke_test::require(std::get<ValueId>(rewritten_binary.lhs) == ValueId(0),
                        "constant dedup should rewrite binary lhs");
    smoke_test::require(std::get<ValueId>(rewritten_binary.rhs) == ValueId(2),
                        "constant dedup should preserve non-duplicate binary rhs");

    const auto& rewritten_return = static_cast<const ReturnInst&>(*block.instructions[3]);
    smoke_test::require(rewritten_return.values.size() == 2,
                        "constant dedup should preserve return arity");
    smoke_test::require(rewritten_return.values[0] == ValueId(0),
                        "constant dedup should rewrite return value");
    smoke_test::require(rewritten_return.values[1] == ValueId(3),
                        "constant dedup should preserve non-const return value");
}

void verify_constant_deduplication_hoists_loop_constants() {
    auto mfile = std::make_unique<MFileUnit>();
    mfile->path = NormalizedPath("const_hoist.m");

    auto script = std::make_unique<ScriptUnit>();
    script->name = "const_hoist";
    script->file = mfile.get();
    ScriptUnit* script_ptr = script.get();

    BasicBlock* entry = script->create_block("entry", SourceSpan::invalid());
    BasicBlock* header = script->create_block("while.header", SourceSpan::invalid());
    BasicBlock* body = script->create_block("while.body", SourceSpan::invalid());
    BasicBlock* exit = script->create_block("while.end", SourceSpan::invalid());
    smoke_test::require(entry != nullptr && header != nullptr && body != nullptr && exit != nullptr,
                        "constant hoist test blocks should be created");
    smoke_test::require(script->set_entry_block(entry), "constant hoist test should set entry");

    script->value_table.values.push_back({ValueId(0), 0, {}, nullptr});
    script->value_table.values.push_back({ValueId(1), 0, {}, nullptr});

    append_goto(*entry, *header);
    ConstInst* condition = append_const(*header, ValueId(0), 1);
    script->value_table.values[0].def = condition;
    append_branch(*header, ValueId(0), *body, *exit);
    ConstInst* body_const = append_const(*body, ValueId(1), 42);
    script->value_table.values[1].def = body_const;
    append_goto(*body, *header);
    append_return(*exit);

    mfile->entry_unit = script.get();
    mfile->code_units.push_back(std::move(script));

    IRPassManagerOptions options;
    options.verify_after_pipeline = true;

    IRPassManager manager(options);
    manager.add_pass<ConstantDeduplicationPass>();

    IRPassManagerResult result = manager.run(*mfile);
    smoke_test::require(result.ok(), "constant hoist should keep IR verifier-clean");
    smoke_test::require(result.changed, "constant hoist should report changed");
    smoke_test::require(entry->instructions.size() == 3,
                        "constant hoist should insert loop consts before entry terminator");
    smoke_test::require(entry->instructions[0]->type() == Instruction::Const,
                        "constant hoist should place header const before goto");
    smoke_test::require(entry->instructions[1]->type() == Instruction::Const,
                        "constant hoist should place body const before goto");
    smoke_test::require(entry->instructions[2]->type() == Instruction::Goto,
                        "constant hoist should keep entry terminator last");
    smoke_test::require(header->instructions.size() == 1 &&
                            header->instructions[0]->type() == Instruction::Branch,
                        "constant hoist should remove const from loop header");
    smoke_test::require(body->instructions.size() == 1 &&
                            body->instructions[0]->type() == Instruction::Goto,
                        "constant hoist should remove const from loop body");

    const auto& hoisted_condition = static_cast<const ConstInst&>(*entry->instructions[0]);
    const auto& hoisted_body_const = static_cast<const ConstInst&>(*entry->instructions[1]);
    smoke_test::require(hoisted_condition.result == ValueId(0),
                        "constant hoist should preserve header const ValueId");
    smoke_test::require(hoisted_body_const.result == ValueId(1),
                        "constant hoist should preserve body const ValueId");
    smoke_test::require(hoisted_condition.parent == entry && hoisted_body_const.parent == entry,
                        "constant hoist should update hoisted const parents");
    smoke_test::require(script_ptr->value_table.values[0].def == entry->instructions[0].get(),
                        "constant hoist should keep header value table def pointing at hoisted const");
    smoke_test::require(script_ptr->value_table.values[1].def == entry->instructions[1].get(),
                        "constant hoist should keep body value table def pointing at hoisted const");
}

void verify_load_forwarding_eliminates_redundant_load() {
    auto mfile = std::make_unique<MFileUnit>();
    mfile->path = NormalizedPath("load_forward.m");

    auto script = std::make_unique<ScriptUnit>();
    script->name = "load_forward";
    script->file = mfile.get();

    BasicBlock* entry = script->create_block("entry", SourceSpan::invalid());
    smoke_test::require(entry != nullptr, "load forwarding test block should be created");
    smoke_test::require(script->set_entry_block(entry), "load forwarding test should set entry");

    const Slot slot{SlotId(0), SlotTag::ScriptVar};
    script->slot_table.slots.push_back({
        slot,
        InternedString("a"),
        SourceSpan::invalid(),
        SlotValueType::Unknown,
    });

    script->value_table.values.push_back({ValueId(0), 0, {}, nullptr});
    script->value_table.values.push_back({ValueId(1), 0, {}, nullptr});

    auto constant = std::make_unique<ConstInst>();
    constant->result = ValueId(0);
    constant->value = Int64Constant{1};
    constant->source_span = SourceSpan(1, 2);
    constant->parent = entry;
    script->value_table.values[0].def = constant.get();
    entry->instructions.push_back(std::move(constant));

    append_store(*entry, slot, ValueId(0));
    entry->instructions.back()->source_span = SourceSpan(3, 8);
    LoadSlotInst* load = append_load(*entry, slot, ValueId(1));
    load->source_span = SourceSpan(20, 21);
    script->value_table.values[1].def = load;
    append_call(*entry, "sin", {ValueId(1)});
    entry->instructions.back()->source_span = SourceSpan(40, 46);

    auto ret = std::make_unique<ReturnInst>();
    ret->values.push_back(ValueId(1));
    ret->source_span = SourceSpan(60, 66);
    ret->parent = entry;
    entry->instructions.push_back(std::move(ret));

    mfile->entry_unit = script.get();
    mfile->code_units.push_back(std::move(script));

    IRPassManagerOptions options;
    options.verify_after_pipeline = true;

    IRPassManager manager(options);
    manager.add_pass<LoadForwardingPass>();

    IRPassManagerResult result = manager.run(*mfile);
    smoke_test::require(result.ok(), "load forwarding should keep IR verifier-clean");
    smoke_test::require(result.changed, "load forwarding should report changed");
    smoke_test::require(result.pass_runs.size() == 1, "load forwarding should record one summary");
    smoke_test::require(result.pass_runs[0].pass_name == "load-forwarding",
                        "load forwarding pass name mismatch");
    smoke_test::require(result.pass_runs[0].changed_invocations == 1,
                        "load forwarding changed invocation count mismatch");

    CodeUnit& unit = *mfile->entry_unit;
    BasicBlock& block = *unit.entry_block;
    smoke_test::require(block.instructions.size() == 4,
                        "load forwarding should remove the redundant load");
    smoke_test::require(block.instructions[0]->type() == Instruction::Const,
                        "load forwarding should keep const");
    smoke_test::require(block.instructions[1]->type() == Instruction::StoreSlot,
                        "load forwarding should keep store");
    smoke_test::require(block.instructions[2]->type() == Instruction::Call,
                        "load forwarding should keep call");
    smoke_test::require(block.instructions[3]->type() == Instruction::Return,
                        "load forwarding should keep return");

    const auto& call = static_cast<const CallInst&>(*block.instructions[2]);
    smoke_test::require(call.arguments.size() == 1, "load forwarding call arity mismatch");
    smoke_test::require(std::get<ValueId>(call.arguments[0]) == ValueId(0),
                        "load forwarding should rewrite call argument to store value");
    const auto& ret_inst = static_cast<const ReturnInst&>(*block.instructions[3]);
    smoke_test::require(ret_inst.values.size() == 1, "load forwarding return arity mismatch");
    smoke_test::require(ret_inst.values[0] == ValueId(0),
                        "load forwarding should rewrite return value across source spans");
    smoke_test::require(unit.value_table.values[1].def == nullptr,
                        "load forwarding should clear eliminated load def");
}

} // namespace
} // namespace baltam

int main() {
    try {
        baltam::verify_successful_pipeline();
        baltam::verify_error_stops_pipeline();
        baltam::verify_ube_removes_unreachable_blocks();
        baltam::verify_ube_skips_value_escape();
        baltam::verify_cfg_simplification_removes_empty_goto_blocks();
        baltam::verify_cfg_simplification_preserves_entry_bridge();
        baltam::verify_cfg_simplification_runs_ube();
        baltam::verify_cfg_simplification_normalizes_cfg_edges();
        baltam::verify_cfg_simplification_merges_linear_block();
        baltam::verify_cfg_simplification_merges_return_block_with_body();
        baltam::verify_cfg_simplification_ignores_source_boundary();
        baltam::verify_dead_branch_elimination_rewrites_constant_branch();
        baltam::verify_dead_code_elimination_removes_unused_constants();
        baltam::verify_dead_code_elimination_removes_overwritten_store();
        baltam::verify_constant_deduplication_merges_duplicate_constants();
        baltam::verify_constant_deduplication_hoists_loop_constants();
        baltam::verify_load_forwarding_eliminates_redundant_load();
        std::cout << "ir_pass_manager_smoke passed\n";
    } catch (const std::exception& ex) {
        std::cerr << "ir_pass_manager_smoke failed: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
