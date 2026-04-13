#include <iostream>
#include <stdexcept>
#include <string>

#include "analysis/analysis_manager.h"
#include "analysis/value_def.h"
#include "analysis/verifier.h"
#include "ir/ir.h"

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

analysis::ValueDefAnalysis::Result run_value_def(Function& function) {
    analysis::verify_function_or_throw(function);

    analysis::FunctionAnalysisManager analysis_manager;
    return analysis_manager.get<analysis::ValueDefAnalysis>(function);
}

Function create_ssa_function(const std::string& name) {
    Function function(name, Function::PrimaryFunction);
    function.set_stage(IRNode::UntypedSSA);
    function.set_output_names({"out"});
    return function;
}

void test_value_defs_cover_arguments_phi_and_calls() {
    Function function = create_ssa_function("value_defs_cover_arguments_phi_and_calls");
    function.set_input_names({"arg", "cond"});

    const ValueId arg = function.create_value("arg");
    const ValueId cond = function.create_value("cond");
    function.set_argument_values({arg, cond});

    BasicBlock* entry = function.create_block("entry");
    BasicBlock* left = function.create_block("left");
    BasicBlock* right = function.create_block("right");
    BasicBlock* merge = function.create_block("merge");
    function.set_entry_block(entry);

    entry->add_successor(left);
    entry->add_successor(right);
    entry->set_terminal(function.create_node<SSACondJumpNode>(ValueRef{cond}, left, right));

    const ValueId left_value = function.create_value("left_value");
    auto* left_number = function.create_node<SSANumberNode>(left_value, std::int64_t{1});
    left->append_instruction(left_number);
    left->add_successor(merge);
    left->set_terminal(function.create_node<SSAJumpNode>(merge));

    const ValueId right_value = function.create_value("right_value");
    auto* right_number = function.create_node<SSANumberNode>(right_value, std::int64_t{2});
    right->append_instruction(right_number);
    right->add_successor(merge);
    right->set_terminal(function.create_node<SSAJumpNode>(merge));

    const ValueId phi_value = function.create_value("phi_value");
    auto* phi = function.create_node<SSAPhiNode>(phi_value);
    phi->add_incoming(left, ValueRef{left_value});
    phi->add_incoming(right, ValueRef{right_value});
    merge->append_phi(phi);

    const ValueId call_result0 = function.create_value("call_result0");
    const ValueId call_result1 = function.create_value("call_result1");
    SSACallNode::Callee callee;
    callee.type = SSACallNode::Callee::Direct;
    callee.direct_symbol = "foo";
    auto* call = function.create_node<SSACallNode>(
        callee, std::vector<ValueId>{call_result0, call_result1},
        std::vector<ValueRef>{ValueRef{phi_value}, ValueRef{arg}});
    merge->append_instruction(call);
    merge->set_terminal(function.create_node<SSAReturnNode>(std::vector<ValueRef>{ValueRef{
        call_result0}}));

    const analysis::ValueDefAnalysis::Result result = run_value_def(function);

    expect(result.definition_of(arg) == nullptr, "argument value should not have instruction def.");
    expect(result.definition_of(cond) == nullptr,
           "condition argument should not have instruction def.");
    expect(result.definition_of(left_value) == left_number, "left number def mismatch.");
    expect(result.definition_of(right_value) == right_number, "right number def mismatch.");
    expect(result.definition_of(phi_value) == phi, "phi def mismatch.");
    expect(result.definition_of(call_result0) == call, "first call result should map to call.");
    expect(result.definition_of(call_result1) == call, "second call result should map to call.");
    expect(result.definition_of(InvalidValueId) == nullptr, "invalid value should have no def.");
}

void test_unreachable_defs_are_indexed() {
    Function function = create_ssa_function("unreachable_defs_are_indexed");

    BasicBlock* entry = function.create_block("entry");
    BasicBlock* dead = function.create_block("dead");
    function.set_entry_block(entry);

    const ValueId live_value = function.create_value("live_value");
    auto* live_number = function.create_node<SSANumberNode>(live_value, std::int64_t{1});
    entry->append_instruction(live_number);
    entry->set_terminal(function.create_node<SSAReturnNode>(std::vector<ValueRef>{ValueRef{
        live_value}}));

    const ValueId dead_value = function.create_value("dead_value");
    auto* dead_number = function.create_node<SSANumberNode>(dead_value, std::int64_t{2});
    dead->append_instruction(dead_number);
    dead->set_terminal(function.create_node<SSAReturnNode>(std::vector<ValueRef>{ValueRef{
        dead_value}}));

    const analysis::ValueDefAnalysis::Result result = run_value_def(function);

    expect(result.definition_of(live_value) == live_number, "reachable def mismatch.");
    expect(result.definition_of(dead_value) == dead_number,
           "unreachable block defs should still be indexed.");
}

}  // namespace

int main() {
    try {
        test_value_defs_cover_arguments_phi_and_calls();
        test_unreachable_defs_are_indexed();
    } catch (const std::exception& ex) {
        std::cerr << "value_def_test FAILED: " << ex.what() << '\n';
        return 1;
    }

    std::cout << "value_def_test PASSED\n";
    return 0;
}
