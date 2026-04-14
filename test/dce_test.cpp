#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "analysis/analysis_manager.h"
#include "analysis/verifier.h"
#include "ir/ir.h"
#include "ir/ir_printer.h"
#include "optimizer/dce.h"
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

analysis::PreservedAnalyses run_dce(Function& function) {
    analysis::verify_function_or_throw(function);

    analysis::FunctionAnalysisManager analysis_manager;
    optimizer::UntypedSSADCEPass pass;
    return pass.run(function, analysis_manager);
}

void test_dead_chain_is_removed() {
    Module module("dce_dead_chain_module", "test/dce/dead_chain.m", Module::M_Function);
    Function& function = create_ssa_function(module, "dce_dead_chain");

    BasicBlock* entry = function.create_block("entry");
    function.set_entry_block(entry);

    const ValueId lhs = function.create_value("lhs");
    entry->append_instruction(function.create_node<SSANumberNode>(lhs, std::int64_t{1}));

    const ValueId rhs = function.create_value("rhs");
    entry->append_instruction(function.create_node<SSANumberNode>(rhs, std::int64_t{2}));

    const ValueId sum = function.create_value("sum");
    entry->append_instruction(
        function.create_node<SSABinOpNode>(BinOpType::Add, sum, ValueRef{lhs}, ValueRef{rhs}));
    entry->set_terminal(function.create_node<SSAReturnNode>(std::vector<ValueRef>{}));

    const analysis::PreservedAnalyses preserved = run_dce(function);
    expect(!preserved.preserves_all(), "dead chain should invalidate analyses.");

    analysis::verify_module_or_throw(module);

    expect(entry->instructions().empty(), "dead instruction chain should be fully removed.");
    expect(!function.has_value(lhs), "dead lhs value should be tombstoned.");
    expect(!function.has_value(rhs), "dead rhs value should be tombstoned.");
    expect(!function.has_value(sum), "dead sum value should be tombstoned.");
}

void test_returned_value_is_preserved() {
    Module module("dce_returned_value_module", "test/dce/returned_value.m", Module::M_Function);
    Function& function = create_ssa_function(module, "dce_returned_value", {"out"});

    BasicBlock* entry = function.create_block("entry");
    function.set_entry_block(entry);

    const ValueId out = function.create_value("out");
    entry->append_instruction(function.create_node<SSANumberNode>(out, std::int64_t{7}));
    entry->set_terminal(function.create_node<SSAReturnNode>(std::vector<ValueRef>{ValueRef{out}}));

    const analysis::PreservedAnalyses preserved = run_dce(function);
    expect(preserved.preserves_all(), "returned value should keep DCE from changing IR.");

    analysis::verify_module_or_throw(module);

    expect(entry->instructions().size() == 1, "returned constant should remain.");
    expect(function.has_value(out), "returned SSA value should remain live.");
}

void test_unused_phi_cascades_to_operands() {
    Module module("dce_phi_module", "test/dce/unused_phi.m", Module::M_Function);
    Function& function = create_ssa_function(module, "dce_unused_phi");
    function.set_input_names({"cond"});

    const ValueId cond = function.create_value("cond");
    function.set_argument_values({cond});

    BasicBlock* entry = function.create_block("entry");
    BasicBlock* left = function.create_block("left");
    BasicBlock* right = function.create_block("right");
    BasicBlock* merge = function.create_block("merge");
    function.set_entry_block(entry);

    entry->add_successor(left);
    entry->add_successor(right);
    entry->set_terminal(function.create_node<SSACondJumpNode>(ValueRef{cond}, left, right));

    const ValueId left_value = function.create_value("left_value");
    left->append_instruction(function.create_node<SSANumberNode>(left_value, std::int64_t{1}));
    left->add_successor(merge);
    left->set_terminal(function.create_node<SSAJumpNode>(merge));

    const ValueId right_value = function.create_value("right_value");
    right->append_instruction(function.create_node<SSANumberNode>(right_value, std::int64_t{2}));
    right->add_successor(merge);
    right->set_terminal(function.create_node<SSAJumpNode>(merge));

    const ValueId phi_value = function.create_value("phi_value");
    auto* phi = function.create_node<SSAPhiNode>(phi_value);
    phi->add_incoming(left, ValueRef{left_value});
    phi->add_incoming(right, ValueRef{right_value});
    merge->append_phi(phi);
    merge->set_terminal(function.create_node<SSAReturnNode>(std::vector<ValueRef>{}));

    const analysis::PreservedAnalyses preserved = run_dce(function);
    expect(!preserved.preserves_all(), "unused phi should be deleted.");

    analysis::verify_module_or_throw(module);

    expect(merge->phi_nodes().empty(), "dead phi should be removed from merge block.");
    expect(left->instructions().empty(), "left phi operand should be removed after cascade.");
    expect(right->instructions().empty(), "right phi operand should be removed after cascade.");
    expect(function.has_value(cond), "argument value should remain.");
    expect(!function.has_value(left_value), "left dead phi operand should be tombstoned.");
    expect(!function.has_value(right_value), "right dead phi operand should be tombstoned.");
    expect(!function.has_value(phi_value), "dead phi result should be tombstoned.");
}

void test_effectful_nodes_are_preserved() {
    Module module("dce_effectful_module", "test/dce/effectful.m", Module::M_Function);
    Function& function = create_ssa_function(module, "dce_effectful");

    BasicBlock* entry = function.create_block("entry");
    function.set_entry_block(entry);

    const ValueId global_value = function.create_value("global_value");
    entry->append_instruction(function.create_node<SSAGlobalLoadNode>(global_value, "G"));

    const ValueId one = function.create_value("one");
    entry->append_instruction(function.create_node<SSANumberNode>(one, std::int64_t{1}));

    SSACallNode::Callee callee;
    callee.type = SSACallNode::Callee::Direct;
    callee.direct_symbol = "__ir_make_cell__";
    const ValueId cell = function.create_value("cell");
    entry->append_instruction(function.create_node<SSACallNode>(
        callee, std::vector<ValueId>{cell}, std::vector<ValueRef>{ValueRef{one}}));
    entry->set_terminal(function.create_node<SSAReturnNode>(std::vector<ValueRef>{}));

    const analysis::PreservedAnalyses preserved = run_dce(function);
    expect(preserved.preserves_all(), "global.load and call should be preserved.");

    analysis::verify_module_or_throw(module);

    expect(entry->instructions().size() == 3,
           "effectful instructions and their operands should remain.");
    expect(function.has_value(global_value), "unused global.load result should remain live.");
    expect(function.has_value(one), "call input should remain because call is preserved.");
    expect(function.has_value(cell), "unused call result should remain live in DCE v1.");
}

void test_default_pipeline_runs_constant_fold_then_dce() {
    Module module("dce_pipeline_module", "test/dce/pipeline.m", Module::M_Function);
    Function& function = create_ssa_function(module, "dce_pipeline");

    BasicBlock* entry = function.create_block("entry");
    function.set_entry_block(entry);

    const ValueId input = function.create_value("input");
    entry->append_instruction(function.create_node<SSANumberNode>(input, std::int64_t{3}));

    const ValueId dead = function.create_value("dead");
    entry->append_instruction(
        function.create_node<SSAUnaryOpNode>(UnaryOpType::UMinus, dead, ValueRef{input}));
    entry->set_terminal(function.create_node<SSAReturnNode>(std::vector<ValueRef>{}));

    analysis::verify_module_or_throw(module);
    optimizer::optimize_function(function);
    analysis::verify_module_or_throw(module);

    expect(entry->instructions().empty(),
           "default pipeline should remove dead folded unary expression.");
    expect(!function.has_value(input), "dead unary operand should be removed by DCE.");
    expect(!function.has_value(dead), "dead folded result should be removed by DCE.");

    const std::string text = print_module(module);
    expect_not_contains(text, "const 3", "dead constant should not remain in printed IR.");
    expect_not_contains(text, "uminus", "dead unary op should not remain in printed IR.");
}

}  // namespace

int main() {
    try {
        test_dead_chain_is_removed();
        test_returned_value_is_preserved();
        test_unused_phi_cascades_to_operands();
        test_effectful_nodes_are_preserved();
        test_default_pipeline_runs_constant_fold_then_dce();
    } catch (const std::exception& ex) {
        std::cerr << "dce_test FAILED: " << ex.what() << '\n';
        return 1;
    }

    std::cout << "dce_test PASSED\n";
    return 0;
}
