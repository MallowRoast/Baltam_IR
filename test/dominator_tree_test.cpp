#include <initializer_list>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "analysis/analysis_manager.h"
#include "analysis/dominator_tree.h"
#include "analysis/verifier.h"
#include "ir/ir.h"

using namespace baltam;

namespace {

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

// 最小断言助手：测试失败时统一抛异常，最后由 main 汇总成单次失败输出。
void expect(bool condition, const std::string& message) {
    if (!condition) {
        fail(message);
    }
}

// 把块列表格式化成稳定文本，方便在顺序类断言失败时直接看到实际结果。
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

// 用于检查支配树孩子列表是否和预期完全一致。
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

// immediate dominator 是 DominatorTree 的核心结果，这里单独抽一个断言助手。
void expect_idom(const analysis::DominatorTree::Result& result, const BasicBlock* block,
                 const BasicBlock* expected, const std::string& label) {
    const BasicBlock* actual = result.immediate_dominator(block);
    if (actual != expected) {
        fail(label + " immediate dominator mismatch for block `" +
             (block != nullptr ? block->name() : "<null>") + "`.");
    }
}

// 测试里统一用 return/jump/condjump 帮助函数构造最小 CFG，避免样板代码淹没断言。
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

// 统一测试入口：先经过 verifier，再通过 FunctionAnalysisManager 运行 DominatorTree。
analysis::DominatorTree::Result run_dominator_tree(Function& function) {
    analysis::verify_function_or_throw(function);

    analysis::FunctionAnalysisManager analysis_manager;
    return analysis_manager.get<analysis::DominatorTree>(function);
}

void test_linear_cfg() {
    Function function("linear_cfg", Function::PrimaryFunction);

    // entry -> body -> exit
    // 线性 CFG 上的支配关系应该也是线性的父子链。
    BasicBlock* entry = function.create_block("entry");
    BasicBlock* body = function.create_block("body");
    BasicBlock* exit = function.create_block("exit");
    function.set_entry_block(entry);

    set_jump(function, entry, body);
    set_jump(function, body, exit);
    set_return(function, exit);

    const analysis::DominatorTree::Result result = run_dominator_tree(function);

    // 线性流上每个块的 immediate dominator 都应回到前一个块。
    expect_idom(result, entry, nullptr, "linear");
    expect_idom(result, body, entry, "linear");
    expect_idom(result, exit, body, "linear");
    // 支配树孩子列表也应形成同样的线性链。
    expect_block_order(result.children_of(entry), {body}, "linear children(entry)");
    expect_block_order(result.children_of(body), {exit}, "linear children(body)");
    expect_block_order(result.children_of(exit), {}, "linear children(exit)");
    // 基本 dominates / strictly_dominates 查询要和线性 CFG 直觉一致。
    expect(result.dominates(entry, entry), "entry should dominate itself.");
    expect(result.dominates(entry, exit), "entry should dominate exit.");
    expect(result.dominates(body, exit), "body should dominate exit.");
    expect(!result.dominates(exit, body), "exit should not dominate body.");
    expect(result.strictly_dominates(body, exit), "body should strictly dominate exit.");
}

void test_diamond_cfg() {
    Function function("diamond_cfg", Function::PrimaryFunction);

    //        -> then ->
    // entry             merge -> ret
    //        -> else ->
    // 菱形 CFG 里 merge 的 immediate dominator 应该回到 entry，
    // 因为 then 和 else 都不能单独支配 merge。
    BasicBlock* entry = function.create_block("entry");
    BasicBlock* then_block = function.create_block("then");
    BasicBlock* else_block = function.create_block("else");
    BasicBlock* merge = function.create_block("merge");
    function.set_entry_block(entry);

    set_cond_jump(function, entry, then_block, else_block, then_block, else_block);
    set_jump(function, then_block, merge);
    set_jump(function, else_block, merge);
    set_return(function, merge);

    const analysis::DominatorTree::Result result = run_dominator_tree(function);

    // 两个分支块都被 entry 直接支配，而 merge 的 idom 也应回到 entry。
    expect_idom(result, entry, nullptr, "diamond");
    expect_idom(result, then_block, entry, "diamond");
    expect_idom(result, else_block, entry, "diamond");
    expect_idom(result, merge, entry, "diamond");
    // 这里的孩子顺序跟随实现中基于 RPO 和插入顺序构建 children 的结果。
    expect_block_order(result.children_of(entry), {else_block, then_block, merge},
                       "diamond children(entry)");
    // then / else 各自都不能单独支配 merge，这是菱形 CFG 最关键的性质。
    expect(result.dominates(entry, merge), "entry should dominate merge.");
    expect(!result.dominates(then_block, merge), "then should not dominate merge.");
    expect(!result.dominates(else_block, merge), "else should not dominate merge.");
    expect(!result.strictly_dominates(merge, merge), "merge should not strictly dominate itself.");
}

void test_loop_cfg() {
    Function function("loop_cfg", Function::PrimaryFunction);

    // entry -> header -> body --+
    //           |               |
    //           +----> exit <---+
    // 循环 CFG 里 header 应该是 body 和 exit 的 immediate dominator。
    BasicBlock* entry = function.create_block("entry");
    BasicBlock* header = function.create_block("header");
    BasicBlock* body = function.create_block("body");
    BasicBlock* exit = function.create_block("exit");
    function.set_entry_block(entry);

    set_jump(function, entry, header);
    set_cond_jump(function, header, exit, body, body, exit);
    set_jump(function, body, header);
    set_return(function, exit);

    const analysis::DominatorTree::Result result = run_dominator_tree(function);

    // 回边不会改变“header 是 loop body 和 exit 的共同立即支配者”。
    expect_idom(result, entry, nullptr, "loop");
    expect_idom(result, header, entry, "loop");
    expect_idom(result, body, header, "loop");
    expect_idom(result, exit, header, "loop");
    // 支配树上 header 的直接孩子应正是 body 和 exit。
    expect_block_order(result.children_of(entry), {header}, "loop children(entry)");
    expect_block_order(result.children_of(header), {body, exit}, "loop children(header)");
    // 这些查询覆盖了“循环头支配循环体/退出块，但循环体不反向支配循环头”。
    expect(result.dominates(header, body), "header should dominate body.");
    expect(result.dominates(header, exit), "header should dominate exit.");
    expect(!result.dominates(body, exit), "body should not dominate exit.");
    expect(!result.dominates(body, header), "body should not dominate header.");
}

void test_unreachable_block_cfg() {
    Function function("unreachable_cfg", Function::PrimaryFunction);

    // entry -> exit -> ret
    //
    // dead -> ret
    //
    // 不可达块不应出现在支配树里，也不应参与 dominates 判定。
    BasicBlock* entry = function.create_block("entry");
    BasicBlock* exit = function.create_block("exit");
    BasicBlock* dead = function.create_block("dead");
    function.set_entry_block(entry);

    set_jump(function, entry, exit);
    set_return(function, exit);
    set_return(function, dead);

    const analysis::DominatorTree::Result result = run_dominator_tree(function);

    // 不可达块不应出现在 DominatorTree 的有效支配关系中。
    expect_idom(result, entry, nullptr, "unreachable");
    expect_idom(result, exit, entry, "unreachable");
    expect_idom(result, dead, nullptr, "unreachable");
    expect_block_order(result.children_of(entry), {exit}, "unreachable children(entry)");
    expect_block_order(result.children_of(dead), {}, "unreachable children(dead)");
    expect(result.dominates(entry, exit), "entry should dominate reachable exit.");
    expect(!result.dominates(entry, dead), "entry should not dominate unreachable dead.");
    expect(!result.dominates(dead, dead), "unreachable dead should not dominate itself.");
}

}  // namespace

int main() {
    try {
        // 当前测试覆盖线性、分支合流、循环、不可达块四类基础 CFG 形状。
        test_linear_cfg();
        test_diamond_cfg();
        test_loop_cfg();
        test_unreachable_block_cfg();
    } catch (const std::exception& ex) {
        std::cerr << "dominator_tree_test FAILED: " << ex.what() << '\n';
        return 1;
    }

    std::cout << "dominator_tree_test PASSED\n";
    return 0;
}
