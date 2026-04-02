#include <initializer_list>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "analysis/analysis_manager.h"
#include "analysis/dominance_frontier.h"
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

void set_return(Function& function, BasicBlock* block) {
    block->set_terminal(function.create_node<ReturnNode>(std::vector<NamedValue>{}));
}

void set_jump(Function& function, BasicBlock* from, BasicBlock* to) {
    from->add_successor(to);
    from->set_terminal(function.create_node<JumpNode>(to));
}

void set_cond_jump(Function& function, BasicBlock* from, BasicBlock* first_successor,
                   BasicBlock* second_successor, BasicBlock* true_block,
                   BasicBlock* false_block) {
    from->add_successor(first_successor);
    from->add_successor(second_successor);
    from->set_terminal(function.create_node<CondJumpNode>(
        NamedValue{"cond", NamedValue::Temporary}, true_block, false_block));
}

analysis::DominanceFrontier::Result run_dominance_frontier(Function& function) {
    analysis::verify_function_or_throw(function);

    analysis::FunctionAnalysisManager analysis_manager;
    return analysis_manager.get<analysis::DominanceFrontier>(function);
}

void test_linear_cfg() {
    Function function("linear_cfg", Function::PrimaryFunction);

    // entry -> body -> exit
    // 线性 CFG 不存在合流点，因此所有 frontier 都应为空。
    BasicBlock* entry = function.create_block("entry");
    BasicBlock* body = function.create_block("body");
    BasicBlock* exit = function.create_block("exit");
    function.set_entry_block(entry);

    set_jump(function, entry, body);
    set_jump(function, body, exit);
    set_return(function, exit);

    const analysis::DominanceFrontier::Result result = run_dominance_frontier(function);

    expect_block_order(result.frontier_of(entry), {}, "linear frontier(entry)");
    expect_block_order(result.frontier_of(body), {}, "linear frontier(body)");
    expect_block_order(result.frontier_of(exit), {}, "linear frontier(exit)");
}

void test_diamond_cfg() {
    Function function("diamond_cfg", Function::PrimaryFunction);

    //        -> then ->
    // entry             merge -> ret
    //        -> else ->
    // merge 是 then / else 的支配边界，但不是 entry 的 frontier。
    BasicBlock* entry = function.create_block("entry");
    BasicBlock* then_block = function.create_block("then");
    BasicBlock* else_block = function.create_block("else");
    BasicBlock* merge = function.create_block("merge");
    function.set_entry_block(entry);

    set_cond_jump(function, entry, then_block, else_block, then_block, else_block);
    set_jump(function, then_block, merge);
    set_jump(function, else_block, merge);
    set_return(function, merge);

    const analysis::DominanceFrontier::Result result = run_dominance_frontier(function);

    expect_block_order(result.frontier_of(entry), {}, "diamond frontier(entry)");
    expect_block_order(result.frontier_of(then_block), {merge}, "diamond frontier(then)");
    expect_block_order(result.frontier_of(else_block), {merge}, "diamond frontier(else)");
    expect_block_order(result.frontier_of(merge), {}, "diamond frontier(merge)");
}

void test_loop_cfg() {
    Function function("loop_cfg", Function::PrimaryFunction);

    // entry -> header -> body --+
    //           |               |
    //           +----> exit <---+
    // header 既是 body 的 frontier 成员，也会出现在自己的 frontier 中。
    BasicBlock* entry = function.create_block("entry");
    BasicBlock* header = function.create_block("header");
    BasicBlock* body = function.create_block("body");
    BasicBlock* exit = function.create_block("exit");
    function.set_entry_block(entry);

    set_jump(function, entry, header);
    set_cond_jump(function, header, exit, body, body, exit);
    set_jump(function, body, header);
    set_return(function, exit);

    const analysis::DominanceFrontier::Result result = run_dominance_frontier(function);

    expect_block_order(result.frontier_of(entry), {}, "loop frontier(entry)");
    expect_block_order(result.frontier_of(header), {header}, "loop frontier(header)");
    expect_block_order(result.frontier_of(body), {header}, "loop frontier(body)");
    expect_block_order(result.frontier_of(exit), {}, "loop frontier(exit)");
}

void test_unreachable_block_cfg() {
    Function function("unreachable_cfg", Function::PrimaryFunction);

    // entry -> exit -> ret
    //
    // dead -> ret
    //
    // 不可达块不会出现在 DominanceFrontier 的有效结果中。
    BasicBlock* entry = function.create_block("entry");
    BasicBlock* exit = function.create_block("exit");
    BasicBlock* dead = function.create_block("dead");
    function.set_entry_block(entry);

    set_jump(function, entry, exit);
    set_return(function, exit);
    set_return(function, dead);

    const analysis::DominanceFrontier::Result result = run_dominance_frontier(function);

    expect_block_order(result.frontier_of(entry), {}, "unreachable frontier(entry)");
    expect_block_order(result.frontier_of(exit), {}, "unreachable frontier(exit)");
    expect_block_order(result.frontier_of(dead), {}, "unreachable frontier(dead)");
    expect(result.frontier.find(dead) == result.frontier.end(),
           "dead block should not appear in frontier result.");
}

}  // namespace

int main() {
    try {
        test_linear_cfg();
        test_diamond_cfg();
        test_loop_cfg();
        test_unreachable_block_cfg();
    } catch (const std::exception& ex) {
        std::cerr << "dominance_frontier_test FAILED: " << ex.what() << '\n';
        return 1;
    }

    std::cout << "dominance_frontier_test PASSED\n";
    return 0;
}
