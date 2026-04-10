#include <cstdint>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "analysis/verifier.h"
#include "ir/ir.h"
#include "ir/ir_printer.h"

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

void test_print_non_ssa_module() {
    Module module("non_ssa_module", "test/m/non_ssa_module.m", Module::M_Function);
    Function* function = module.create_function("plain", Function::PrimaryFunction);
    module.set_entry_function(function);
    function->set_input_names({"x"});
    function->set_output_names({"out"});

    BasicBlock* entry = function->create_block("entry");
    function->set_entry_block(entry);

    entry->append_instruction(function->create_node<NumberNode>(
        NamedValue{"tmp", NamedValue::Temporary}, NumberNode::NumberValue{std::int64_t{1}}));
    entry->set_terminal(function->create_node<ReturnNode>(
        std::vector<NamedValue>{NamedValue{"tmp", NamedValue::Temporary}}));

    analysis::verify_module_or_throw(module);

    std::ostringstream oss;
    print_ir(oss, module);
    const std::string text = oss.str();

    expect_contains(text, "; stage = \"non-ssa\"", "non-SSA module stage should be printed.");
    expect_contains(text, "define primary_function @plain(%x)",
                    "non-SSA function signature should be printed.");
    expect_contains(text, "%tmp = const 1", "non-SSA instruction should be printed.");
    expect_contains(text, "ret %tmp", "non-SSA return should be printed.");
}

void test_print_untyped_ssa_module() {
    Module module("ssa_module", "test/m/ssa_module.m", Module::M_Function);
    Function* function = module.create_function("ssa_fn", Function::PrimaryFunction);
    module.set_entry_function(function);
    function->set_stage(IRNode::UntypedSSA);
    function->set_input_names({"cond", "x"});
    function->set_output_names({"out"});

    const ValueId cond = function->create_value("cond");
    const ValueId x = function->create_value("x");
    function->set_argument_values({cond, x});

    BasicBlock* entry = function->create_block("entry");
    BasicBlock* then_block = function->create_block("then");
    BasicBlock* else_block = function->create_block("else");
    BasicBlock* merge = function->create_block("merge");
    function->set_entry_block(entry);

    entry->add_successor(then_block);
    entry->add_successor(else_block);
    entry->set_terminal(function->create_node<SSACondJumpNode>(ValueRef{cond}, then_block, else_block));

    const ValueId one = function->create_value("one");
    auto* one_node =
        function->create_node<SSANumberNode>(one, SSANumberNode::NumberValue{std::int64_t{1}});
    then_block->append_instruction(one_node);
    then_block->add_successor(merge);
    then_block->set_terminal(function->create_node<SSAJumpNode>(merge));

    const ValueId two = function->create_value("two");
    auto* two_node =
        function->create_node<SSANumberNode>(two, SSANumberNode::NumberValue{std::int64_t{2}});
    else_block->append_instruction(two_node);
    else_block->add_successor(merge);
    else_block->set_terminal(function->create_node<SSAJumpNode>(merge));

    const ValueId merged = function->create_value("merged");
    auto* phi = function->create_node<SSAPhiNode>(merged);
    phi->add_incoming(then_block, ValueRef{one});
    phi->add_incoming(else_block, ValueRef{two});
    merge->append_phi(phi);
    merge->set_terminal(function->create_node<SSAReturnNode>(std::vector<ValueRef>{ValueRef{merged}}));

    analysis::verify_module_or_throw(module);

    std::ostringstream oss;
    print_ir(oss, module);
    const std::string text = oss.str();

    expect_contains(text, "; stage = \"untyped-ssa\"",
                    "untyped SSA stage should be printed.");
    expect_contains(text, "define primary_function @ssa_fn(%cond, %x)",
                    "SSA arguments with single definitions should not be renumbered.");
    expect_contains(text, "%merged = phi [ %one, %then ], [ %two, %else ]",
                    "phi node should be printed in SSA form.");
    expect_contains(text, "ret %merged", "SSA return should be printed.");
}

}  // namespace

int main() {
    try {
        test_print_non_ssa_module();
        test_print_untyped_ssa_module();
    } catch (const std::exception& ex) {
        std::cerr << "ir_printer_test FAILED: " << ex.what() << '\n';
        return 1;
    }

    std::cout << "ir_printer_test PASSED\n";
    return 0;
}
