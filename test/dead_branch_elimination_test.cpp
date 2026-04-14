#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "analysis/analysis_manager.h"
#include "analysis/verifier.h"
#include "ir/ir.h"
#include "ir/ir_printer.h"
#include "optimizer/dead_branch_elimination.h"
#include "optimizer/optimize.h"

using namespace baltam;

namespace {

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

void expect(bool condition, const std::string& message) {
    if (!condition) {
        fail(message);
    }
}

void expect_contains(const std::string& text, const std::string& needle,
                     const std::string& message) {
    if (text.find(needle) == std::string::npos) {
        fail(message + " actual = " + text);
    }
}

void expect_not_contains(const std::string& text, const std::string& needle,
                         const std::string& message) {
    if (text.find(needle) != std::string::npos) {
        fail(message + " actual = " + text);
    }
}

Function& create_ssa_function(Module& module, const std::string& name,
                              std::vector<std::string> outputs = {}) {
    Function* function = module.create_function(name, Function::PrimaryFunction);
    module.set_entry_function(function);
    function->set_stage(IRNode::UntypedSSA);
    function->set_output_names(std::move(outputs));
    return *function;
}

std::string print_module(const Module& module) {
    std::ostringstream oss;
    print_ir(oss, module);
    return oss.str();
}

analysis::PreservedAnalyses run_dead_branch_elimination(Function& function) {
    analysis::verify_function_or_throw(function);

    analysis::FunctionAnalysisManager analysis_manager;
    optimizer::UntypedSSADeadBranchEliminationPass pass;
    return pass.run(function, analysis_manager);
}

BasicBlock* find_block(const Function& function, const std::string& name) {
    for (const auto& block : function.blocks()) {
        if (block != nullptr && block->name() == name) {
            return block.get();
        }
    }
    return nullptr;
}

void test_constant_bool_branch_becomes_jump_and_unreachable_block_is_removed() {
    Module module("dead_branch_module", "test/dead_branch/constant_bool.m", Module::M_Function);
    Function& function = create_ssa_function(module, "dead_branch", {"out"});

    BasicBlock* entry = function.create_block("entry");
    BasicBlock* then_block = function.create_block("then");
    BasicBlock* else_block = function.create_block("else");
    BasicBlock* merge = function.create_block("merge");
    function.set_entry_block(entry);

    const ValueId cond = function.create_value("cond");
    entry->append_instruction(function.create_node<SSANumberNode>(cond, true));
    entry->add_successor(then_block);
    entry->add_successor(else_block);
    entry->set_terminal(function.create_node<SSACondJumpNode>(ValueRef{cond}, then_block, else_block));

    const ValueId then_value = function.create_value("then_value");
    then_block->append_instruction(function.create_node<SSANumberNode>(then_value, std::int64_t{7}));
    then_block->add_successor(merge);
    then_block->set_terminal(function.create_node<SSAJumpNode>(merge));

    const ValueId else_value = function.create_value("else_value");
    else_block->append_instruction(function.create_node<SSANumberNode>(else_value, std::int64_t{9}));
    else_block->add_successor(merge);
    else_block->set_terminal(function.create_node<SSAJumpNode>(merge));

    const ValueId out = function.create_value("out");
    auto* phi = function.create_node<SSAPhiNode>(out);
    phi->add_incoming(then_block, ValueRef{then_value});
    phi->add_incoming(else_block, ValueRef{else_value});
    merge->append_phi(phi);
    merge->set_terminal(function.create_node<SSAReturnNode>(std::vector<ValueRef>{ValueRef{out}}));

    const analysis::PreservedAnalyses preserved = run_dead_branch_elimination(function);
    expect(!preserved.preserves_all(), "dead branch elimination should change constant branch.");

    analysis::verify_module_or_throw(module);

    expect(function.blocks().size() == 3, "unreachable else block should be removed.");
    expect(find_block(function, "else") == nullptr, "else block should no longer exist.");
    expect(!function.has_value(else_value), "values defined only in removed block should be tombstoned.");

    const auto* jump = dynamic_cast<const SSAJumpNode*>(entry->terminal());
    expect(jump != nullptr, "constant condjump should become jump.");
    expect(jump->target() == then_block, "jump should target the live successor.");
    expect(entry->successors().size() == 1 && entry->successors().front() == then_block,
           "entry successors should keep only the live branch.");

    expect(merge->predecessors().size() == 1 && merge->predecessors().front() == then_block,
           "merge should keep only the live predecessor.");
    expect(merge->phi_nodes().empty(), "single-incoming phi should be lowered away.");
    expect(!merge->instructions().empty(), "phi replacement should materialize as an instruction.");

    const auto* replacement = dynamic_cast<const SSACopyNode*>(merge->instructions().front());
    expect(replacement != nullptr, "single-incoming phi should become SSACopyNode.");
    expect(replacement->src().id == then_value,
           "phi replacement should forward the surviving incoming value.");
}

void test_default_pipeline_runs_constant_fold_then_dead_branch_elimination_then_dce_then_cfg_simplify() {
    Module module("dead_branch_pipeline_module", "test/dead_branch/pipeline.m", Module::M_Function);
    Function& function = create_ssa_function(module, "dead_branch_pipeline", {"out"});

    BasicBlock* entry = function.create_block("entry");
    BasicBlock* then_block = function.create_block("then");
    BasicBlock* else_block = function.create_block("else");
    BasicBlock* merge = function.create_block("merge");
    function.set_entry_block(entry);

    const ValueId one = function.create_value("one");
    entry->append_instruction(function.create_node<SSANumberNode>(one, std::int64_t{1}));
    const ValueId zero = function.create_value("zero");
    entry->append_instruction(function.create_node<SSANumberNode>(zero, std::int64_t{0}));
    const ValueId cond = function.create_value("cond");
    entry->append_instruction(function.create_node<SSABinOpNode>(
        BinOpType::Gt, cond, ValueRef{one}, ValueRef{zero}));
    entry->add_successor(then_block);
    entry->add_successor(else_block);
    entry->set_terminal(function.create_node<SSACondJumpNode>(ValueRef{cond}, then_block, else_block));

    const ValueId then_value = function.create_value("then_value");
    then_block->append_instruction(function.create_node<SSANumberNode>(then_value, std::int64_t{7}));
    then_block->add_successor(merge);
    then_block->set_terminal(function.create_node<SSAJumpNode>(merge));

    const ValueId else_value = function.create_value("else_value");
    else_block->append_instruction(function.create_node<SSANumberNode>(else_value, std::int64_t{9}));
    else_block->add_successor(merge);
    else_block->set_terminal(function.create_node<SSAJumpNode>(merge));

    const ValueId out = function.create_value("out");
    auto* phi = function.create_node<SSAPhiNode>(out);
    phi->add_incoming(then_block, ValueRef{then_value});
    phi->add_incoming(else_block, ValueRef{else_value});
    merge->append_phi(phi);
    merge->set_terminal(function.create_node<SSAReturnNode>(std::vector<ValueRef>{ValueRef{out}}));

    analysis::verify_module_or_throw(module);
    optimizer::optimize_function(function);
    analysis::verify_module_or_throw(module);

    expect(function.blocks().size() == 1,
           "default pipeline should fold the function down to one block.");
    expect(find_block(function, "else") == nullptr, "default pipeline should erase unreachable else.");
    expect(find_block(function, "then") == nullptr,
           "default pipeline should erase the empty trampoline block.");
    expect(find_block(function, "merge") == nullptr,
           "default pipeline should merge the linear merge block into entry.");
    expect(entry->instructions().size() == 1,
           "dead branch condition should be removed while keeping the surviving constant.");
    expect(entry->successors().empty(), "fully merged entry block should not keep CFG edges.");
    expect(entry->phi_nodes().empty(), "fully merged entry block should not keep phi nodes.");
    const auto* entry_ret = dynamic_cast<const SSAReturnNode*>(entry->terminal());
    expect(entry_ret != nullptr, "entry terminal should be simplified to the merged return.");

    const auto* out_number = dynamic_cast<const SSANumberNode*>(entry->instructions().front());
    expect(out_number != nullptr, "surviving return value should be materialized as a constant.");
    expect(std::holds_alternative<IntegerConstant>(out_number->value()),
           "optimized output should stay in integer constant form.");
    expect(std::get<IntegerConstant>(out_number->value()).as_int64().has_value() &&
               *std::get<IntegerConstant>(out_number->value()).as_int64() == 7,
           "optimized pipeline should preserve the live branch value.");

    const std::string text = print_module(module);
    expect_not_contains(text, "else:", "dead else block should not be printed.");
    expect_not_contains(text, "then:", "empty trampoline block should not be printed.");
    expect_not_contains(text, "merge:", "linear merge block should not be printed.");
    expect_not_contains(text, "phi", "single-incoming phi should not remain in printed IR.");
    expect_not_contains(text, "gt", "folded branch condition should not remain in printed IR.");
}

}  // namespace

int main() {
    try {
        test_constant_bool_branch_becomes_jump_and_unreachable_block_is_removed();
        test_default_pipeline_runs_constant_fold_then_dead_branch_elimination_then_dce_then_cfg_simplify();
    } catch (const std::exception& ex) {
        std::cerr << "dead_branch_elimination_test FAILED: " << ex.what() << '\n';
        return 1;
    }

    std::cout << "dead_branch_elimination_test PASSED\n";
    return 0;
}
