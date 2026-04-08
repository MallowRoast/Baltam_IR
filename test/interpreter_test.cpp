#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "analysis/verifier.h"
#include "interpreter/interpreter.h"
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

const interpreter::Value& require_single_output(const interpreter::ExecResult& result) {
    if (result.outputs.size() != 1) {
        fail("expected exactly one output.");
    }
    return result.outputs.front();
}

void expect_output_int(const interpreter::ExecResult& result, std::int64_t expected,
                       const std::string& message) {
    const interpreter::Value& output = require_single_output(result);
    expect(output.type == interpreter::Value::Concrete, message + " output should be concrete.");
    expect(output.object != nullptr, message + " output object should not be null.");
    expect(output.object->as_int() == expected, message + " output mismatch.");
}

void test_execute_constant_and_copy() {
    Function function("const_copy", Function::PrimaryFunction);
    function.set_stage(IRNode::UntypedSSA);
    function.set_output_names({"out"});

    BasicBlock* entry = function.create_block("entry");
    function.set_entry_block(entry);

    const ValueId one = function.create_value("one");
    const ValueId out = function.create_value("out");
    entry->append_instruction(
        function.create_node<SSANumberNode>(one, SSANumberNode::NumberValue{std::int64_t{7}}));
    entry->append_instruction(function.create_node<SSACopyNode>(out, ValueRef{one}));
    entry->set_terminal(function.create_node<SSAReturnNode>(std::vector<ValueRef>{ValueRef{out}}));

    analysis::verify_function_or_throw(function);
    const interpreter::ExecResult result = interpreter::execute_function(function);
    expect_output_int(result, 7, "const/copy");
}

void test_execute_branch_phi() {
    Function function("branch_phi", Function::PrimaryFunction);
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
    then_block->append_instruction(
        function.create_node<SSANumberNode>(one, SSANumberNode::NumberValue{std::int64_t{1}}));
    then_block->add_successor(merge);
    then_block->set_terminal(function.create_node<SSAJumpNode>(merge));

    const ValueId two = function.create_value("two");
    else_block->append_instruction(
        function.create_node<SSANumberNode>(two, SSANumberNode::NumberValue{std::int64_t{2}}));
    else_block->add_successor(merge);
    else_block->set_terminal(function.create_node<SSAJumpNode>(merge));

    const ValueId merged = function.create_value("merged");
    auto* phi = function.create_node<SSAPhiNode>(merged);
    phi->add_incoming(then_block, ValueRef{one});
    phi->add_incoming(else_block, ValueRef{two});
    merge->append_phi(phi);
    merge->set_terminal(function.create_node<SSAReturnNode>(std::vector<ValueRef>{ValueRef{merged}}));

    analysis::verify_function_or_throw(function);

    interpreter::Value::Object true_arg = std::make_shared<ba_obj>(true);
    interpreter::Value::Object false_arg = std::make_shared<ba_obj>(false);
    expect_output_int(interpreter::execute_function(function, {true_arg}), 1, "branch phi true");
    expect_output_int(interpreter::execute_function(function, {false_arg}), 2, "branch phi false");
}

void test_returning_undef_throws() {
    Function function("undef_ret", Function::PrimaryFunction);
    function.set_stage(IRNode::UntypedSSA);
    function.set_output_names({"out"});

    BasicBlock* entry = function.create_block("entry");
    function.set_entry_block(entry);

    const ValueId missing = function.create_value("missing");
    entry->append_instruction(function.create_node<SSAUndefNode>(missing));
    entry->set_terminal(
        function.create_node<SSAReturnNode>(std::vector<ValueRef>{ValueRef{missing}}));

    analysis::verify_function_or_throw(function);

    bool threw = false;
    try {
        (void)interpreter::execute_function(function);
    } catch (const std::exception& ex) {
        threw = true;
        expect_contains(ex.what(), "undef", "undef return should mention undef.");
    }
    expect(threw, "returning undef should throw.");
}

void test_direct_module_function_call() {
    Module module("call_module", "test/call_module.m", Module::M_Function);
    Function* callee = module.create_function("callee", Function::LocalFunction);
    callee->set_stage(IRNode::UntypedSSA);
    callee->set_output_names({"out"});
    BasicBlock* callee_entry = callee->create_block("entry");
    callee->set_entry_block(callee_entry);
    const ValueId callee_out = callee->create_value("out");
    callee_entry->append_instruction(
        callee->create_node<SSANumberNode>(callee_out, SSANumberNode::NumberValue{std::int64_t{9}}));
    callee_entry->set_terminal(
        callee->create_node<SSAReturnNode>(std::vector<ValueRef>{ValueRef{callee_out}}));

    Function* caller = module.create_function("caller", Function::PrimaryFunction);
    module.set_entry_function(caller);
    caller->set_stage(IRNode::UntypedSSA);
    caller->set_output_names({"out"});
    BasicBlock* caller_entry = caller->create_block("entry");
    caller->set_entry_block(caller_entry);
    const ValueId caller_out = caller->create_value("out");
    caller_entry->append_instruction(caller->create_node<SSACallNode>(
        SSACallNode::Callee{SSACallNode::Callee::Direct, "callee", ValueRef{}},
        std::vector<ValueId>{caller_out}, std::vector<ValueRef>{}));
    caller_entry->set_terminal(
        caller->create_node<SSAReturnNode>(std::vector<ValueRef>{ValueRef{caller_out}}));

    analysis::verify_module_or_throw(module);
    const interpreter::ExecResult result = interpreter::execute_function(*caller);
    expect_output_int(result, 9, "direct module call");
}

void test_indirect_function_handle_call() {
    Module module("handle_module", "test/handle_module.m", Module::M_Function);
    Function* callee = module.create_function("callee", Function::LocalFunction);
    callee->set_stage(IRNode::UntypedSSA);
    callee->set_output_names({"out"});
    BasicBlock* callee_entry = callee->create_block("entry");
    callee->set_entry_block(callee_entry);
    const ValueId callee_out = callee->create_value("out");
    callee_entry->append_instruction(
        callee->create_node<SSANumberNode>(callee_out, SSANumberNode::NumberValue{std::int64_t{11}}));
    callee_entry->set_terminal(
        callee->create_node<SSAReturnNode>(std::vector<ValueRef>{ValueRef{callee_out}}));

    Function* caller = module.create_function("caller", Function::PrimaryFunction);
    module.set_entry_function(caller);
    caller->set_stage(IRNode::UntypedSSA);
    caller->set_output_names({"out"});
    BasicBlock* caller_entry = caller->create_block("entry");
    caller->set_entry_block(caller_entry);

    const ValueId callee_name = caller->create_value("callee_name");
    const ValueId handle = caller->create_value("handle");
    const ValueId caller_out = caller->create_value("out");

    caller_entry->append_instruction(
        caller->create_node<SSATextNode>(callee_name, std::string("callee")));
    caller_entry->append_instruction(caller->create_node<SSACallNode>(
        SSACallNode::Callee{SSACallNode::Callee::Direct, "__ir_make_function_handle__", ValueRef{}},
        std::vector<ValueId>{handle}, std::vector<ValueRef>{ValueRef{callee_name}}));
    SSACallNode::Callee indirect_callee;
    indirect_callee.type = SSACallNode::Callee::Indirect;
    indirect_callee.indirect_value = ValueRef{handle};
    caller_entry->append_instruction(caller->create_node<SSACallNode>(
        indirect_callee, std::vector<ValueId>{caller_out}, std::vector<ValueRef>{}));
    caller_entry->set_terminal(
        caller->create_node<SSAReturnNode>(std::vector<ValueRef>{ValueRef{caller_out}}));

    analysis::verify_module_or_throw(module);
    const interpreter::ExecResult result = interpreter::execute_function(*caller);
    expect_output_int(result, 11, "indirect function handle call");
}

}  // namespace

int main() {
    try {
        test_execute_constant_and_copy();
        test_execute_branch_phi();
        test_returning_undef_throws();
        test_direct_module_function_call();
        test_indirect_function_handle_call();
    } catch (const std::exception& ex) {
        std::cerr << "interpreter_test FAILED: " << ex.what() << '\n';
        return 1;
    }

    std::cout << "interpreter_test PASSED\n";
    return 0;
}
