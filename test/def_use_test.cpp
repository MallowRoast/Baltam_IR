#include <initializer_list>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "analysis/analysis_manager.h"
#include "analysis/def_use.h"
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

NamedValue user(const char* name) {
    return NamedValue{name, NamedValue::UserVariable};
}

NamedValue temp(const char* name) {
    return NamedValue{name, NamedValue::Temporary};
}

std::string block_list_text(const std::vector<const BasicBlock*>& blocks) {
    std::ostringstream oss;
    oss << '[';
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        if (i != 0) {
            oss << ", ";
        }
        oss << (blocks[i] != nullptr ? blocks[i]->name() : "<null>");
    }
    oss << ']';
    return oss.str();
}

std::string node_label(const NonSSANode* node) {
    if (node == nullptr) {
        return "<null>";
    }

    const std::string block_name = node->parent() != nullptr ? node->parent()->name() : "<no-parent>";
    switch (node->type()) {
    case NonSSANode::Number:
        return block_name + ":Number";
    case NonSSANode::Text:
        return block_name + ":Text";
    case NonSSANode::Assign:
        return block_name + ":Assign";
    case NonSSANode::GlobalLoad:
        return block_name + ":GlobalLoad";
    case NonSSANode::GlobalStore:
        return block_name + ":GlobalStore";
    case NonSSANode::UnaryOp:
        return block_name + ":UnaryOp";
    case NonSSANode::BinOp:
        return block_name + ":BinOp";
    case NonSSANode::Call:
        return block_name + ":Call";
    case NonSSANode::CondJump:
        return block_name + ":CondJump";
    case NonSSANode::Jump:
        return block_name + ":Jump";
    case NonSSANode::Return:
        return block_name + ":Return";
    }

    return block_name + ":<unknown>";
}

std::string node_list_text(const std::vector<const NonSSANode*>& nodes) {
    std::ostringstream oss;
    oss << '[';
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        if (i != 0) {
            oss << ", ";
        }
        oss << node_label(nodes[i]);
    }
    oss << ']';
    return oss.str();
}

void expect_block_order(const std::vector<const BasicBlock*>& actual,
                        std::initializer_list<const BasicBlock*> expected,
                        const std::string& label) {
    if (actual.size() != expected.size()) {
        fail(label + " size mismatch: actual = " + block_list_text(actual));
    }

    std::vector<const BasicBlock*> expected_vector(expected);
    for (std::size_t i = 0; i < expected_vector.size(); ++i) {
        if (actual[i] != expected_vector[i]) {
            fail(label + " mismatch: actual = " + block_list_text(actual));
        }
    }
}

void expect_node_order(const std::vector<const NonSSANode*>& actual,
                       std::initializer_list<const NonSSANode*> expected,
                       const std::string& label) {
    if (actual.size() != expected.size()) {
        fail(label + " size mismatch: actual = " + node_list_text(actual));
    }

    std::vector<const NonSSANode*> expected_vector(expected);
    for (std::size_t i = 0; i < expected_vector.size(); ++i) {
        if (actual[i] != expected_vector[i]) {
            fail(label + " mismatch: actual = " + node_list_text(actual));
        }
    }
}

NumberNode* append_number(Function& function, BasicBlock* block, NamedValue result) {
    NumberNode* node = function.create_node<NumberNode>(result, std::int64_t{1});
    block->append_instruction(node);
    return node;
}

AssignNode* append_assign(Function& function, BasicBlock* block, NamedValue dst, NamedValue src) {
    AssignNode* node = function.create_node<AssignNode>(dst, src);
    block->append_instruction(node);
    return node;
}

CallNode* append_call(Function& function, BasicBlock* block, CallNode::CalleeType callee_type,
                      const std::string& callee, std::vector<NamedValue> outputs,
                      std::vector<NamedValue> inputs) {
    CallNode* node =
        function.create_node<CallNode>(callee_type, callee, std::move(outputs), std::move(inputs));
    block->append_instruction(node);
    return node;
}

ReturnNode* set_return(Function& function, BasicBlock* block, std::vector<NamedValue> values) {
    ReturnNode* node = function.create_node<ReturnNode>(std::move(values));
    block->set_terminal(node);
    return node;
}

JumpNode* set_jump(Function& function, BasicBlock* from, BasicBlock* to) {
    from->add_successor(to);
    JumpNode* node = function.create_node<JumpNode>(to);
    from->set_terminal(node);
    return node;
}

analysis::DefUse::Result run_def_use(Function& function) {
    analysis::verify_function_or_throw(function);

    analysis::FunctionAnalysisManager analysis_manager;
    return analysis_manager.get<analysis::DefUse>(function);
}

void test_call_uses_and_defs() {
    Function function("call_uses_and_defs", Function::PrimaryFunction);
    function.set_input_names({"arg", "callee_name"});

    BasicBlock* entry = function.create_block("entry");
    function.set_entry_block(entry);

    NumberNode* tmp_def = append_number(function, entry, temp("tmp0"));
    AssignNode* y_def = append_assign(function, entry, user("y"), user("arg"));
    CallNode* direct_call =
        append_call(function, entry, CallNode::Direct, "foo", {temp("r1")}, {temp("tmp0"), user("y")});
    CallNode* indirect_call = append_call(function, entry, CallNode::Indirect, "callee_name",
                                          {user("out")}, {temp("r1")});
    ReturnNode* ret = set_return(function, entry, {user("out"), user("y")});

    const analysis::DefUse::Result result = run_def_use(function);

    expect_node_order(result.definitions_of("tmp0"), {tmp_def}, "call defs(tmp0)");
    expect_node_order(result.definitions_of("y"), {y_def}, "call defs(y)");
    expect_node_order(result.definitions_of("r1"), {direct_call}, "call defs(r1)");
    expect_node_order(result.definitions_of("out"), {indirect_call}, "call defs(out)");

    expect_node_order(result.uses_of("arg"), {y_def}, "call uses(arg)");
    expect_node_order(result.uses_of("tmp0"), {direct_call}, "call uses(tmp0)");
    expect_node_order(result.uses_of("y"), {direct_call, ret}, "call uses(y)");
    expect_node_order(result.uses_of("r1"), {indirect_call}, "call uses(r1)");
    expect_node_order(result.uses_of("out"), {ret}, "call uses(out)");
    expect_node_order(result.uses_of("callee_name"), {indirect_call}, "call uses(callee_name)");
    expect_node_order(result.uses_of("foo"), {}, "direct callee should not be tracked as name use");

    expect_block_order(result.definition_blocks_of("tmp0"), {entry}, "call def_blocks(tmp0)");
    expect_block_order(result.definition_blocks_of("y"), {entry}, "call def_blocks(y)");
    expect_block_order(result.definition_blocks_of("r1"), {entry}, "call def_blocks(r1)");
    expect_block_order(result.definition_blocks_of("out"), {entry}, "call def_blocks(out)");
}

void test_reachable_only_and_block_dedup() {
    Function function("reachable_only", Function::PrimaryFunction);

    BasicBlock* entry = function.create_block("entry");
    BasicBlock* body = function.create_block("body");
    BasicBlock* exit = function.create_block("exit");
    BasicBlock* dead = function.create_block("dead");
    function.set_entry_block(entry);

    NumberNode* entry_x_def = append_number(function, entry, user("x"));
    set_jump(function, entry, body);

    NumberNode* body_x_def = append_number(function, body, user("x"));
    AssignNode* y_def = append_assign(function, body, user("y"), user("x"));
    set_jump(function, body, exit);

    ReturnNode* ret = set_return(function, exit, {user("y")});

    append_number(function, dead, user("x"));
    set_return(function, dead, {user("x")});

    const analysis::DefUse::Result result = run_def_use(function);

    expect_node_order(result.definitions_of("x"), {entry_x_def, body_x_def},
                      "reachable defs(x)");
    expect_node_order(result.uses_of("x"), {y_def}, "reachable uses(x)");
    expect_block_order(result.definition_blocks_of("x"), {entry, body},
                       "reachable def_blocks(x)");

    expect_node_order(result.definitions_of("y"), {y_def}, "reachable defs(y)");
    expect_node_order(result.uses_of("y"), {ret}, "reachable uses(y)");
    expect_block_order(result.definition_blocks_of("y"), {body},
                       "reachable def_blocks(y)");
}

}  // namespace

int main() {
    try {
        test_call_uses_and_defs();
        test_reachable_only_and_block_dedup();
    } catch (const std::exception& ex) {
        std::cerr << "def_use_test FAILED: " << ex.what() << '\n';
        return 1;
    }

    std::cout << "def_use_test PASSED\n";
    return 0;
}
