#include <initializer_list>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "analysis/analysis_manager.h"
#include "analysis/cfg_analysis.h"
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

void expect_rpo_index(const analysis::CFGAnalysis::Result& result, const BasicBlock* block,
                      std::size_t expected_index, const std::string& label) {
    auto it = result.rpo_index.find(block);
    if (it == result.rpo_index.end()) {
        fail(label + " missing block `" + (block != nullptr ? block->name() : "<null>") + "`.");
    }
    if (it->second != expected_index) {
        fail(label + " index mismatch for block `" + block->name() + "`.");
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

analysis::CFGAnalysis::Result run_cfg_analysis(Function& function) {
    analysis::verify_function_or_throw(function);

    analysis::FunctionAnalysisManager analysis_manager;
    return analysis_manager.get<analysis::CFGAnalysis>(function);
}

void test_linear_cfg() {
    Function function("linear_cfg", Function::PrimaryFunction);

    // entry -> body -> exit
    // 最小线性 CFG，用来验证最基础的后序 / 逆后序顺序。
    BasicBlock* entry = function.create_block("entry");
    BasicBlock* body = function.create_block("body");
    BasicBlock* exit = function.create_block("exit");
    function.set_entry_block(entry);

    set_jump(function, entry, body);
    set_jump(function, body, exit);
    set_return(function, exit);

    const analysis::CFGAnalysis::Result result = run_cfg_analysis(function);

    expect_block_order(result.postorder, {exit, body, entry}, "linear postorder");
    expect_block_order(result.reverse_postorder, {entry, body, exit}, "linear reverse_postorder");
    expect(result.is_reachable(entry), "entry should be reachable.");
    expect(result.is_reachable(body), "body should be reachable.");
    expect(result.is_reachable(exit), "exit should be reachable.");
    expect(!result.is_reachable(nullptr), "nullptr should not be reachable.");
    expect(result.rpo_index.size() == 3, "linear rpo_index size mismatch.");
    expect_rpo_index(result, entry, 0, "linear rpo_index");
    expect_rpo_index(result, body, 1, "linear rpo_index");
    expect_rpo_index(result, exit, 2, "linear rpo_index");
}

void test_diamond_cfg() {
    Function function("diamond_cfg", Function::PrimaryFunction);

    //        -> then ->
    // entry             merge -> ret
    //        -> else ->
    // 菱形 CFG 用来验证分支 + 合流时 DFS 后序和 RPO 的稳定性。
    BasicBlock* entry = function.create_block("entry");
    BasicBlock* then_block = function.create_block("then");
    BasicBlock* else_block = function.create_block("else");
    BasicBlock* merge = function.create_block("merge");
    function.set_entry_block(entry);

    set_cond_jump(function, entry, then_block, else_block, then_block, else_block);
    set_jump(function, then_block, merge);
    set_jump(function, else_block, merge);
    set_return(function, merge);

    const analysis::CFGAnalysis::Result result = run_cfg_analysis(function);

    expect_block_order(result.postorder, {merge, then_block, else_block, entry},
                       "diamond postorder");
    expect_block_order(result.reverse_postorder, {entry, else_block, then_block, merge},
                       "diamond reverse_postorder");
    expect(result.is_reachable(entry), "diamond entry should be reachable.");
    expect(result.is_reachable(then_block), "diamond then should be reachable.");
    expect(result.is_reachable(else_block), "diamond else should be reachable.");
    expect(result.is_reachable(merge), "diamond merge should be reachable.");
    expect(result.rpo_index.size() == 4, "diamond rpo_index size mismatch.");
    expect_rpo_index(result, entry, 0, "diamond rpo_index");
    expect_rpo_index(result, else_block, 1, "diamond rpo_index");
    expect_rpo_index(result, then_block, 2, "diamond rpo_index");
    expect_rpo_index(result, merge, 3, "diamond rpo_index");
}

void test_loop_cfg() {
    Function function("loop_cfg", Function::PrimaryFunction);

    // entry -> header -> body --+
    //           |               |
    //           +----> exit <---+
    // 含回边的循环 CFG，用来验证 visited 去重后不会陷入死循环，
    // 并且仍能得到合理的 postorder / reverse_postorder。
    BasicBlock* entry = function.create_block("entry");
    BasicBlock* header = function.create_block("header");
    BasicBlock* body = function.create_block("body");
    BasicBlock* exit = function.create_block("exit");
    function.set_entry_block(entry);

    set_jump(function, entry, header);
    set_cond_jump(function, header, exit, body, body, exit);
    set_jump(function, body, header);
    set_return(function, exit);

    const analysis::CFGAnalysis::Result result = run_cfg_analysis(function);

    expect_block_order(result.postorder, {exit, body, header, entry}, "loop postorder");
    expect_block_order(result.reverse_postorder, {entry, header, body, exit},
                       "loop reverse_postorder");
    expect(result.is_reachable(entry), "loop entry should be reachable.");
    expect(result.is_reachable(header), "loop header should be reachable.");
    expect(result.is_reachable(body), "loop body should be reachable.");
    expect(result.is_reachable(exit), "loop exit should be reachable.");
    expect(result.rpo_index.size() == 4, "loop rpo_index size mismatch.");
    expect_rpo_index(result, entry, 0, "loop rpo_index");
    expect_rpo_index(result, header, 1, "loop rpo_index");
    expect_rpo_index(result, body, 2, "loop rpo_index");
    expect_rpo_index(result, exit, 3, "loop rpo_index");
}

void test_unreachable_block_cfg() {
    Function function("unreachable_cfg", Function::PrimaryFunction);

    // entry -> exit -> ret
    //
    // dead -> ret
    //
    // 不可达块仍然是合法 block，但不应出现在 CFGAnalysis 的结果里。
    BasicBlock* entry = function.create_block("entry");
    BasicBlock* exit = function.create_block("exit");
    BasicBlock* dead = function.create_block("dead");
    function.set_entry_block(entry);

    set_jump(function, entry, exit);
    set_return(function, exit);
    set_return(function, dead);

    const analysis::CFGAnalysis::Result result = run_cfg_analysis(function);

    expect_block_order(result.postorder, {exit, entry}, "unreachable postorder");
    expect_block_order(result.reverse_postorder, {entry, exit}, "unreachable reverse_postorder");
    expect(result.is_reachable(entry), "unreachable entry should be reachable.");
    expect(result.is_reachable(exit), "unreachable exit should be reachable.");
    expect(!result.is_reachable(dead), "dead block should not be reachable.");
    expect(result.rpo_index.size() == 2, "unreachable rpo_index size mismatch.");
    expect_rpo_index(result, entry, 0, "unreachable rpo_index");
    expect_rpo_index(result, exit, 1, "unreachable rpo_index");
    expect(result.rpo_index.find(dead) == result.rpo_index.end(),
           "dead block should not appear in rpo_index.");
}

}  // namespace

int main() {
    try {
        test_linear_cfg();
        test_diamond_cfg();
        test_loop_cfg();
        test_unreachable_block_cfg();
    } catch (const std::exception& ex) {
        std::cerr << "cfg_analysis_test FAILED: " << ex.what() << '\n';
        return 1;
    }

    std::cout << "cfg_analysis_test PASSED\n";
    return 0;
}
