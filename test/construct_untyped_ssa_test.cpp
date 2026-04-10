#include <cstdint>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "analysis/verifier.h"
#include "ir/ir.h"
#include "ir/ir_printer.h"
#include "optimizer/construct_untyped_ssa.h"

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

std::string print_module(const Module& module) {
    std::ostringstream oss;
    print_ir(oss, module);
    return oss.str();
}

void test_if_else_generates_phi() {
    Module module("branch_module", "test/m/branch_module.m", Module::M_Function);
    Function* function = module.create_function("branch", Function::PrimaryFunction);
    module.set_entry_function(function);
    function->set_input_names({"cond"});
    function->set_output_names({"out"});

    BasicBlock* entry = function->create_block("entry");
    BasicBlock* then_block = function->create_block("then");
    BasicBlock* else_block = function->create_block("else");
    BasicBlock* merge = function->create_block("merge");
    function->set_entry_block(entry);

    entry->add_successor(then_block);
    entry->add_successor(else_block);
    entry->set_terminal(function->create_node<CondJumpNode>(
        NamedValue{"cond", NamedValue::UserVariable}, then_block, else_block));

    then_block->append_instruction(function->create_node<NumberNode>(
        NamedValue{"out", NamedValue::UserVariable}, NumberNode::NumberValue{std::int64_t{1}}));
    then_block->add_successor(merge);
    then_block->set_terminal(function->create_node<JumpNode>(merge));

    else_block->append_instruction(function->create_node<NumberNode>(
        NamedValue{"out", NamedValue::UserVariable}, NumberNode::NumberValue{std::int64_t{2}}));
    else_block->add_successor(merge);
    else_block->set_terminal(function->create_node<JumpNode>(merge));

    merge->set_terminal(function->create_node<ReturnNode>(
        std::vector<NamedValue>{NamedValue{"out", NamedValue::UserVariable}}));

    analysis::verify_module_or_throw(module);

    Module ssa_module = optimizer::construct_untyped_ssa_module(module);
    analysis::verify_module_or_throw(ssa_module);
    const std::string text = print_module(ssa_module);

    expect_contains(text, "; stage = \"untyped-ssa\"", "converted module should be SSA.");
    expect_contains(text, " = phi [ %out.", "if/else merge should create phi for `out`.");
    expect_contains(text, "ret %out.", "return should use SSA value.");
}

void test_missing_definition_materializes_undef() {
    Module module("undef_module", "test/m/undef_module.m", Module::M_Function);
    Function* function = module.create_function("maybe_assign", Function::PrimaryFunction);
    module.set_entry_function(function);
    function->set_input_names({"cond"});
    function->set_output_names({"out"});

    BasicBlock* entry = function->create_block("entry");
    BasicBlock* then_block = function->create_block("then");
    BasicBlock* else_block = function->create_block("else");
    BasicBlock* merge = function->create_block("merge");
    function->set_entry_block(entry);

    entry->add_successor(then_block);
    entry->add_successor(else_block);
    entry->set_terminal(function->create_node<CondJumpNode>(
        NamedValue{"cond", NamedValue::UserVariable}, then_block, else_block));

    then_block->append_instruction(function->create_node<NumberNode>(
        NamedValue{"out", NamedValue::UserVariable}, NumberNode::NumberValue{std::int64_t{1}}));
    then_block->add_successor(merge);
    then_block->set_terminal(function->create_node<JumpNode>(merge));

    else_block->add_successor(merge);
    else_block->set_terminal(function->create_node<JumpNode>(merge));

    merge->set_terminal(function->create_node<ReturnNode>(
        std::vector<NamedValue>{NamedValue{"out", NamedValue::UserVariable}}));

    analysis::verify_module_or_throw(module);

    Module ssa_module = optimizer::construct_untyped_ssa_module(module);
    analysis::verify_module_or_throw(ssa_module);
    const std::string text = print_module(ssa_module);

    expect_contains(text, " = undef", "partially defined variable should materialize undef.");
    expect_contains(text, " = phi [ %out.", "merge should still use phi for partially defined value.");
}

void test_loop_generates_header_phi() {
    Module module("loop_module", "test/m/loop_module.m", Module::M_Function);
    Function* function = module.create_function("loop", Function::PrimaryFunction);
    module.set_entry_function(function);
    function->set_input_names({"cond"});
    function->set_output_names({"out"});

    BasicBlock* entry = function->create_block("entry");
    BasicBlock* header = function->create_block("header");
    BasicBlock* body = function->create_block("body");
    BasicBlock* exit = function->create_block("exit");
    function->set_entry_block(entry);

    entry->append_instruction(function->create_node<NumberNode>(
        NamedValue{"out", NamedValue::UserVariable}, NumberNode::NumberValue{std::int64_t{0}}));
    entry->add_successor(header);
    entry->set_terminal(function->create_node<JumpNode>(header));

    header->add_successor(body);
    header->add_successor(exit);
    header->set_terminal(function->create_node<CondJumpNode>(
        NamedValue{"cond", NamedValue::UserVariable}, body, exit));

    body->append_instruction(function->create_node<NumberNode>(
        NamedValue{"one", NamedValue::Temporary}, NumberNode::NumberValue{std::int64_t{1}}));
    body->append_instruction(function->create_node<BinOpNode>(
        BinOpNode::Add, NamedValue{"out", NamedValue::UserVariable},
        NamedValue{"out", NamedValue::UserVariable}, NamedValue{"one", NamedValue::Temporary}));
    body->add_successor(header);
    body->set_terminal(function->create_node<JumpNode>(header));

    exit->set_terminal(function->create_node<ReturnNode>(
        std::vector<NamedValue>{NamedValue{"out", NamedValue::UserVariable}}));

    analysis::verify_module_or_throw(module);

    Module ssa_module = optimizer::construct_untyped_ssa_module(module);
    analysis::verify_module_or_throw(ssa_module);
    const std::string text = print_module(ssa_module);

    expect_contains(text, "header:", "loop header should be preserved.");
    expect_contains(text, " = phi [ %out.", "loop-carried variable should create header phi.");
    expect_contains(text, "add %out.", "loop body should read the header phi version.");
}

void test_dead_temporary_does_not_generate_phi() {
    Module module("dead_temp_module", "test/m/dead_temp_module.m", Module::M_Function);
    Function* function = module.create_function("dead_temp_branch", Function::PrimaryFunction);
    module.set_entry_function(function);
    function->set_input_names({"cond"});
    function->set_output_names({"out"});

    BasicBlock* entry = function->create_block("entry");
    BasicBlock* then_block = function->create_block("then");
    BasicBlock* else_block = function->create_block("else");
    BasicBlock* merge = function->create_block("merge");
    function->set_entry_block(entry);

    entry->add_successor(then_block);
    entry->add_successor(else_block);
    entry->set_terminal(function->create_node<CondJumpNode>(
        NamedValue{"cond", NamedValue::UserVariable}, then_block, else_block));

    then_block->append_instruction(function->create_node<NumberNode>(
        NamedValue{"tmp_dead", NamedValue::Temporary}, NumberNode::NumberValue{std::int64_t{10}}));
    then_block->append_instruction(function->create_node<NumberNode>(
        NamedValue{"out", NamedValue::UserVariable}, NumberNode::NumberValue{std::int64_t{1}}));
    then_block->add_successor(merge);
    then_block->set_terminal(function->create_node<JumpNode>(merge));

    else_block->append_instruction(function->create_node<NumberNode>(
        NamedValue{"tmp_dead", NamedValue::Temporary}, NumberNode::NumberValue{std::int64_t{20}}));
    else_block->append_instruction(function->create_node<NumberNode>(
        NamedValue{"out", NamedValue::UserVariable}, NumberNode::NumberValue{std::int64_t{2}}));
    else_block->add_successor(merge);
    else_block->set_terminal(function->create_node<JumpNode>(merge));

    merge->set_terminal(function->create_node<ReturnNode>(
        std::vector<NamedValue>{NamedValue{"out", NamedValue::UserVariable}}));

    analysis::verify_module_or_throw(module);

    Module ssa_module = optimizer::construct_untyped_ssa_module(module);
    analysis::verify_module_or_throw(ssa_module);
    const std::string text = print_module(ssa_module);

    expect_contains(text, " = phi [ %out.", "live-out variable should still create phi.");
    expect_not_contains(text, " = phi [ %tmp_dead.",
                        "dead temporary should not create a merge phi.");
}

void test_test0_local_temp_does_not_generate_phi() {
    Module module("test0_module", "test/m/test0/test0.m", Module::M_Function);
    Function* function = module.create_function("__script_main__", Function::Script);
    module.set_entry_function(function);
    function->set_output_names({"a", "b", "c"});

    BasicBlock* entry = function->create_block("entry");
    BasicBlock* then_block = function->create_block("if.then.0");
    BasicBlock* else_block = function->create_block("if.else.1");
    BasicBlock* merge = function->create_block("if.end.2");
    function->set_entry_block(entry);

    entry->append_instruction(function->create_node<NumberNode>(
        NamedValue{"__t0", NamedValue::Temporary}, NumberNode::NumberValue{std::int64_t{1}}));
    entry->append_instruction(function->create_node<NumberNode>(
        NamedValue{"__t1", NamedValue::Temporary}, NumberNode::NumberValue{std::int64_t{2}}));
    entry->append_instruction(function->create_node<BinOpNode>(
        BinOpNode::Add, NamedValue{"a", NamedValue::UserVariable},
        NamedValue{"__t0", NamedValue::Temporary}, NamedValue{"__t1", NamedValue::Temporary}));
    entry->append_instruction(function->create_node<CallNode>(
        CallNode::Direct, "sin", std::vector<NamedValue>{NamedValue{"b", NamedValue::UserVariable}},
        std::vector<NamedValue>{NamedValue{"a", NamedValue::UserVariable}}));
    entry->append_instruction(function->create_node<NumberNode>(
        NamedValue{"__t3", NamedValue::Temporary}, NumberNode::NumberValue{std::int64_t{0}}));
    entry->append_instruction(function->create_node<BinOpNode>(
        BinOpNode::Gt, NamedValue{"__t2", NamedValue::Temporary},
        NamedValue{"b", NamedValue::UserVariable}, NamedValue{"__t3", NamedValue::Temporary}));
    entry->add_successor(then_block);
    entry->add_successor(else_block);
    entry->set_terminal(function->create_node<CondJumpNode>(
        NamedValue{"__t2", NamedValue::Temporary}, then_block, else_block));

    then_block->append_instruction(function->create_node<NumberNode>(
        NamedValue{"__t4", NamedValue::Temporary}, NumberNode::NumberValue{std::int64_t{2}}));
    then_block->append_instruction(function->create_node<BinOpNode>(
        BinOpNode::Multiply, NamedValue{"c", NamedValue::UserVariable},
        NamedValue{"b", NamedValue::UserVariable}, NamedValue{"__t4", NamedValue::Temporary}));
    then_block->add_successor(merge);
    then_block->set_terminal(function->create_node<JumpNode>(merge));

    else_block->append_instruction(function->create_node<NumberNode>(
        NamedValue{"c", NamedValue::UserVariable}, NumberNode::NumberValue{std::int64_t{0}}));
    else_block->add_successor(merge);
    else_block->set_terminal(function->create_node<JumpNode>(merge));

    merge->set_terminal(function->create_node<ReturnNode>(
        std::vector<NamedValue>{
            NamedValue{"a", NamedValue::UserVariable},
            NamedValue{"b", NamedValue::UserVariable},
            NamedValue{"c", NamedValue::UserVariable},
        }));

    analysis::verify_module_or_throw(module);

    Module ssa_module = optimizer::construct_untyped_ssa_module(module);
    analysis::verify_module_or_throw(ssa_module);
    const std::string text = print_module(ssa_module);

    expect_contains(text, "if.end.2:", "test0 merge block should be preserved.");
    expect_contains(text, " = phi [ %c.", "merge should still create phi for `c`.");
    expect_not_contains(text, " = phi [ %__t4.",
                        "then-local temporary `__t4` should not create a merge phi.");
}

}  // namespace

int main() {
    try {
        test_if_else_generates_phi();
        test_missing_definition_materializes_undef();
        test_loop_generates_header_phi();
        test_dead_temporary_does_not_generate_phi();
        test_test0_local_temp_does_not_generate_phi();
    } catch (const std::exception& ex) {
        std::cerr << "construct_untyped_ssa_test FAILED: " << ex.what() << '\n';
        return 1;
    }

    std::cout << "construct_untyped_ssa_test PASSED\n";
    return 0;
}
