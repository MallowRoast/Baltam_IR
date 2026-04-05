#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

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

void expect_contains(const std::string& text, const std::string& needle,
                     const std::string& message) {
    if (text.find(needle) == std::string::npos) {
        fail(message + " actual = " + text);
    }
}

void test_valid_untyped_ssa_function() {
    Function function("valid_ssa", Function::PrimaryFunction);
    function.set_stage(IRNode::UntypedSSA);
    function.set_input_names({"cond", "x"});
    function.set_output_names({"out"});

    const ValueId cond = function.create_value("cond");
    const ValueId x = function.create_value("x");
    function.set_argument_values({cond, x});

    BasicBlock* entry = function.create_block("entry");
    BasicBlock* then_block = function.create_block("then");
    BasicBlock* else_block = function.create_block("else");
    BasicBlock* merge = function.create_block("merge");
    function.set_entry_block(entry);

    entry->add_successor(then_block);
    entry->add_successor(else_block);
    entry->set_terminal(function.create_node<SSACondJumpNode>(ValueRef{cond}, then_block, else_block));

    const ValueId one = function.create_value("one");
    auto* one_node =
        function.create_node<SSANumberNode>(one, SSANumberNode::NumberValue{std::int64_t{1}});
    then_block->append_instruction(one_node);
    then_block->add_successor(merge);
    then_block->set_terminal(function.create_node<SSAJumpNode>(merge));

    const ValueId two = function.create_value("two");
    auto* two_node =
        function.create_node<SSANumberNode>(two, SSANumberNode::NumberValue{std::int64_t{2}});
    else_block->append_instruction(two_node);
    else_block->add_successor(merge);
    else_block->set_terminal(function.create_node<SSAJumpNode>(merge));

    const ValueId merged = function.create_value("merged");
    auto* phi = function.create_node<SSAPhiNode>(merged);
    phi->add_incoming(then_block, ValueRef{one});
    phi->add_incoming(else_block, ValueRef{two});
    merge->append_phi(phi);
    merge->set_terminal(function.create_node<SSAReturnNode>(std::vector<ValueRef>{ValueRef{merged}}));

    const analysis::VerificationResult result = analysis::verify_function(function);
    expect(result.ok(), "valid untyped SSA should pass verifier: " + result.format());
}

void test_phi_incomings_must_match_predecessors() {
    Function function("bad_phi", Function::PrimaryFunction);
    function.set_stage(IRNode::UntypedSSA);
    function.set_input_names({"cond"});
    function.set_output_names({"out"});

    const ValueId cond = function.create_value("cond");
    function.set_argument_values({cond});

    BasicBlock* entry = function.create_block("entry");
    BasicBlock* then_block = function.create_block("then");
    BasicBlock* else_block = function.create_block("else");
    BasicBlock* merge = function.create_block("merge");
    function.set_entry_block(entry);

    entry->add_successor(then_block);
    entry->add_successor(else_block);
    entry->set_terminal(function.create_node<SSACondJumpNode>(ValueRef{cond}, then_block, else_block));

    const ValueId one = function.create_value("one");
    auto* one_node =
        function.create_node<SSANumberNode>(one, SSANumberNode::NumberValue{std::int64_t{1}});
    then_block->append_instruction(one_node);
    then_block->add_successor(merge);
    then_block->set_terminal(function.create_node<SSAJumpNode>(merge));

    const ValueId two = function.create_value("two");
    auto* two_node =
        function.create_node<SSANumberNode>(two, SSANumberNode::NumberValue{std::int64_t{2}});
    else_block->append_instruction(two_node);
    else_block->add_successor(merge);
    else_block->set_terminal(function.create_node<SSAJumpNode>(merge));

    const ValueId merged = function.create_value("merged");
    auto* phi = function.create_node<SSAPhiNode>(merged);
    phi->add_incoming(then_block, ValueRef{one});
    merge->append_phi(phi);
    merge->set_terminal(function.create_node<SSAReturnNode>(std::vector<ValueRef>{ValueRef{merged}}));

    const analysis::VerificationResult result = analysis::verify_function(function);
    expect(!result.ok(), "phi predecessor mismatch should be rejected.");
    expect_contains(result.format(), "phi incoming 集合与前驱列表不一致",
                    "expected phi incoming mismatch diagnostic.");
}

void test_ssa_definition_must_exist_in_value_table() {
    Function function("unknown_value_def", Function::PrimaryFunction);
    function.set_stage(IRNode::UntypedSSA);
    function.set_input_names({});
    function.set_output_names({"out"});

    BasicBlock* entry = function.create_block("entry");
    function.set_entry_block(entry);

    const ValueId constant = 7;
    auto* constant_node =
        function.create_node<SSANumberNode>(constant, SSANumberNode::NumberValue{std::int64_t{7}});
    entry->append_instruction(constant_node);
    entry->set_terminal(
        function.create_node<SSAReturnNode>(std::vector<ValueRef>{ValueRef{constant}}));

    const analysis::VerificationResult result = analysis::verify_function(function);
    expect(!result.ok(), "SSA definitions missing from the value table should be rejected.");
    expect_contains(result.format(), "产生了未知的 SSA 值",
                    "expected unknown SSA value diagnostic.");
}

}  // namespace

int main() {
    try {
        test_valid_untyped_ssa_function();
        test_phi_incomings_must_match_predecessors();
        test_ssa_definition_must_exist_in_value_table();
    } catch (const std::exception& ex) {
        std::cerr << "verifier_test FAILED: " << ex.what() << '\n';
        return 1;
    }

    std::cout << "verifier_test PASSED\n";
    return 0;
}
