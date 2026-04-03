#include <initializer_list>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "analysis/analysis_manager.h"
#include "analysis/liveness.h"
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

std::string name_list_text(const std::vector<std::string>& names) {
    std::ostringstream oss;
    oss << '[';
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (i != 0) {
            oss << ", ";
        }
        oss << names[i];
    }
    oss << ']';
    return oss.str();
}

void expect_name_order(const std::vector<std::string>& actual,
                       std::initializer_list<const char*> expected, const std::string& label) {
    if (actual.size() != expected.size()) {
        fail(label + " size mismatch: actual = " + name_list_text(actual));
    }

    std::vector<std::string> expected_vector(expected.begin(), expected.end());
    for (std::size_t i = 0; i < expected_vector.size(); ++i) {
        if (actual[i] != expected_vector[i]) {
            fail(label + " mismatch: actual = " + name_list_text(actual));
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

CondJumpNode* set_cond_jump(Function& function, BasicBlock* from, NamedValue cond,
                            BasicBlock* true_block, BasicBlock* false_block) {
    from->add_successor(true_block);
    from->add_successor(false_block);
    CondJumpNode* node = function.create_node<CondJumpNode>(cond, true_block, false_block);
    from->set_terminal(node);
    return node;
}

analysis::Liveness::Result run_liveness(Function& function) {
    analysis::verify_function_or_throw(function);

    analysis::FunctionAnalysisManager analysis_manager;
    return analysis_manager.get<analysis::Liveness>(function);
}

void test_linear_cfg() {
    Function function("linear_cfg", Function::PrimaryFunction);
    function.set_input_names({"x"});

    // entry -> body -> exit
    // body 使用参数 x 定义 y，因而 x 在 entry/body 的边界上保持 live。
    BasicBlock* entry = function.create_block("entry");
    BasicBlock* body = function.create_block("body");
    BasicBlock* exit = function.create_block("exit");
    function.set_entry_block(entry);

    set_jump(function, entry, body);
    append_assign(function, body, user("y"), user("x"));
    set_jump(function, body, exit);
    set_return(function, exit, {user("y")});

    const analysis::Liveness::Result result = run_liveness(function);

    expect_name_order(result.live_in_of(entry), {"x"}, "linear live_in(entry)");
    expect_name_order(result.live_out_of(entry), {"x"}, "linear live_out(entry)");
    expect_name_order(result.live_in_of(body), {"x"}, "linear live_in(body)");
    expect_name_order(result.live_out_of(body), {"y"}, "linear live_out(body)");
    expect_name_order(result.live_in_of(exit), {"y"}, "linear live_in(exit)");
    expect_name_order(result.live_out_of(exit), {}, "linear live_out(exit)");
}

void test_branch_cfg() {
    Function function("branch_cfg", Function::PrimaryFunction);

    // entry 定义临时量 t0，then/else 都会使用它定义 y，最后 merge 返回 y。
    BasicBlock* entry = function.create_block("entry");
    BasicBlock* then_block = function.create_block("then");
    BasicBlock* else_block = function.create_block("else");
    BasicBlock* merge = function.create_block("merge");
    function.set_entry_block(entry);

    append_number(function, entry, temp("t0"));
    append_number(function, entry, temp("cond"));
    set_cond_jump(function, entry, temp("cond"), then_block, else_block);

    append_assign(function, then_block, user("y"), temp("t0"));
    set_jump(function, then_block, merge);

    append_assign(function, else_block, user("y"), temp("t0"));
    set_jump(function, else_block, merge);

    set_return(function, merge, {user("y")});

    const analysis::Liveness::Result result = run_liveness(function);

    expect_name_order(result.live_in_of(entry), {}, "branch live_in(entry)");
    expect_name_order(result.live_out_of(entry), {"t0"}, "branch live_out(entry)");
    expect_name_order(result.live_in_of(then_block), {"t0"}, "branch live_in(then)");
    expect_name_order(result.live_out_of(then_block), {"y"}, "branch live_out(then)");
    expect_name_order(result.live_in_of(else_block), {"t0"}, "branch live_in(else)");
    expect_name_order(result.live_out_of(else_block), {"y"}, "branch live_out(else)");
    expect_name_order(result.live_in_of(merge), {"y"}, "branch live_in(merge)");
    expect_name_order(result.live_out_of(merge), {}, "branch live_out(merge)");
}

void test_loop_cfg() {
    Function function("loop_cfg", Function::PrimaryFunction);

    // entry 定义 x，header/body/exit 都需要它，因此它会在回边上传播。
    BasicBlock* entry = function.create_block("entry");
    BasicBlock* header = function.create_block("header");
    BasicBlock* body = function.create_block("body");
    BasicBlock* exit = function.create_block("exit");
    function.set_entry_block(entry);

    append_number(function, entry, user("x"));
    set_jump(function, entry, header);

    set_cond_jump(function, header, user("x"), body, exit);

    append_assign(function, body, user("x"), user("x"));
    set_jump(function, body, header);

    set_return(function, exit, {user("x")});

    const analysis::Liveness::Result result = run_liveness(function);

    expect_name_order(result.live_in_of(entry), {}, "loop live_in(entry)");
    expect_name_order(result.live_out_of(entry), {"x"}, "loop live_out(entry)");
    expect_name_order(result.live_in_of(header), {"x"}, "loop live_in(header)");
    expect_name_order(result.live_out_of(header), {"x"}, "loop live_out(header)");
    expect_name_order(result.live_in_of(body), {"x"}, "loop live_in(body)");
    expect_name_order(result.live_out_of(body), {"x"}, "loop live_out(body)");
    expect_name_order(result.live_in_of(exit), {"x"}, "loop live_in(exit)");
    expect_name_order(result.live_out_of(exit), {}, "loop live_out(exit)");
}

void test_unreachable_block_cfg() {
    Function function("unreachable_cfg", Function::PrimaryFunction);
    function.set_input_names({"x"});

    // dead 块里虽然有名字使用，但不可达块不应出现在结果中。
    BasicBlock* entry = function.create_block("entry");
    BasicBlock* exit = function.create_block("exit");
    BasicBlock* dead = function.create_block("dead");
    function.set_entry_block(entry);

    set_jump(function, entry, exit);
    set_return(function, exit, {});

    append_assign(function, dead, user("y"), user("x"));
    set_return(function, dead, {user("y")});

    const analysis::Liveness::Result result = run_liveness(function);

    expect_name_order(result.live_in_of(entry), {}, "unreachable live_in(entry)");
    expect_name_order(result.live_out_of(entry), {}, "unreachable live_out(entry)");
    expect_name_order(result.live_in_of(exit), {}, "unreachable live_in(exit)");
    expect_name_order(result.live_out_of(exit), {}, "unreachable live_out(exit)");
    expect_name_order(result.live_in_of(dead), {}, "unreachable live_in(dead)");
    expect_name_order(result.live_out_of(dead), {}, "unreachable live_out(dead)");
    expect(result.live_in.find(dead) == result.live_in.end(),
           "dead block should not appear in live_in.");
    expect(result.live_out.find(dead) == result.live_out.end(),
           "dead block should not appear in live_out.");
}

}  // namespace

int main() {
    try {
        test_linear_cfg();
        test_branch_cfg();
        test_loop_cfg();
        test_unreachable_block_cfg();
    } catch (const std::exception& ex) {
        std::cerr << "liveness_test FAILED: " << ex.what() << '\n';
        return 1;
    }

    std::cout << "liveness_test PASSED\n";
    return 0;
}
