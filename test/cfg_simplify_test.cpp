#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "analysis/analysis_manager.h"
#include "analysis/verifier.h"
#include "ir/ir.h"
#include "ir/ir_printer.h"
#include "optimizer/cfg_simplify.h"
#include "optimizer/optimize.h"

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

void expect_not_contains(const std::string& text, const std::string& needle,
                         const std::string& message) {
    if (text.find(needle) != std::string::npos) {
        fail(message + " actual = " + text);
    }
}

Function& create_ssa_function(Module& module, const std::string& name,
                              std::vector<std::string> outputs = {}) {
    Function* function = module.create_function(name, Function::PrimaryFunction);
    module.set_entry_function(function);
    function->set_stage(IRNode::UntypedSSA);
    function->set_output_names(std::move(outputs));
    return *function;
}

std::string print_module(const Module& module) {
    std::ostringstream oss;
    print_ir(oss, module);
    return oss.str();
}

analysis::PreservedAnalyses run_cfg_simplify(Function& function) {
    analysis::verify_function_or_throw(function);

    analysis::FunctionAnalysisManager analysis_manager;
    optimizer::UntypedSSACFGSimplifyPass pass;
    return pass.run(function, analysis_manager);
}

BasicBlock* find_block(const Function& function, const std::string& name) {
    for (const auto& block : function.blocks()) {
        if (block != nullptr && block->name() == name) {
            return block.get();
        }
    }
    return nullptr;
}

void test_jump_only_trampoline_block_is_removed() {
    Module module("cfg_simplify_module", "test/cfg_simplify/trampoline.m", Module::M_Function);
    Function& function = create_ssa_function(module, "cfg_simplify", {"out"});

    BasicBlock* entry = function.create_block("entry");
    BasicBlock* mid = function.create_block("mid");
    BasicBlock* exit = function.create_block("exit");
    function.set_entry_block(entry);

    entry->add_successor(mid);
    entry->set_terminal(function.create_node<SSAJumpNode>(mid));

    mid->add_successor(exit);
    mid->set_terminal(function.create_node<SSAJumpNode>(exit));

    const ValueId out = function.create_value("out");
    exit->append_instruction(function.create_node<SSANumberNode>(out, std::int64_t{42}));
    exit->set_terminal(function.create_node<SSAReturnNode>(std::vector<ValueRef>{ValueRef{out}}));

    const analysis::PreservedAnalyses preserved = run_cfg_simplify(function);
    expect(!preserved.preserves_all(), "cfg simplify should remove the trampoline block.");

    analysis::verify_module_or_throw(module);

    expect(function.blocks().size() == 1,
           "CFG simplify should continue folding the linear exit block into entry.");
    expect(find_block(function, "mid") == nullptr, "mid block should no longer exist.");
    expect(find_block(function, "exit") == nullptr,
           "single-predecessor exit block should be merged into entry as well.");
    expect(entry->successors().empty(), "merged entry block should not keep successors.");

    const auto* ret = dynamic_cast<const SSAReturnNode*>(entry->terminal());
    expect(ret != nullptr, "merged entry block should end with the moved return.");
    expect(entry->instructions().size() == 1,
           "moved exit instruction should now live in the entry block.");

    const std::string text = print_module(module);
    expect_not_contains(text, "mid:", "printed IR should not keep the removed trampoline block.");
    expect_not_contains(text, "exit:", "printed IR should not keep the merged exit block.");
}

void test_default_pipeline_removes_jump_only_block_created_by_dce() {
    Module module("cfg_simplify_pipeline_module", "test/cfg_simplify/pipeline.m",
                  Module::M_Function);
    Function& function = create_ssa_function(module, "cfg_simplify_pipeline", {"out"});

    BasicBlock* entry = function.create_block("entry");
    BasicBlock* then_block = function.create_block("then");
    BasicBlock* else_block = function.create_block("else");
    BasicBlock* merge = function.create_block("merge");
    function.set_entry_block(entry);

    const ValueId one = function.create_value("one");
    entry->append_instruction(function.create_node<SSANumberNode>(one, std::int64_t{1}));
    const ValueId zero = function.create_value("zero");
    entry->append_instruction(function.create_node<SSANumberNode>(zero, std::int64_t{0}));
    const ValueId cond = function.create_value("cond");
    entry->append_instruction(function.create_node<SSABinOpNode>(
        BinOpType::Gt, cond, ValueRef{one}, ValueRef{zero}));
    entry->add_successor(then_block);
    entry->add_successor(else_block);
    entry->set_terminal(function.create_node<SSACondJumpNode>(ValueRef{cond}, then_block, else_block));

    const ValueId then_value = function.create_value("then_value");
    then_block->append_instruction(function.create_node<SSANumberNode>(then_value, std::int64_t{7}));
    then_block->add_successor(merge);
    then_block->set_terminal(function.create_node<SSAJumpNode>(merge));

    const ValueId else_value = function.create_value("else_value");
    else_block->append_instruction(function.create_node<SSANumberNode>(else_value, std::int64_t{9}));
    else_block->add_successor(merge);
    else_block->set_terminal(function.create_node<SSAJumpNode>(merge));

    const ValueId out = function.create_value("out");
    auto* phi = function.create_node<SSAPhiNode>(out);
    phi->add_incoming(then_block, ValueRef{then_value});
    phi->add_incoming(else_block, ValueRef{else_value});
    merge->append_phi(phi);
    merge->set_terminal(function.create_node<SSAReturnNode>(std::vector<ValueRef>{ValueRef{out}}));

    analysis::verify_module_or_throw(module);
    optimizer::optimize_function(function);
    analysis::verify_module_or_throw(module);

    expect(function.blocks().size() == 1,
           "default pipeline should collapse the function to a single block.");
    expect(find_block(function, "else") == nullptr, "dead else block should be erased.");
    expect(find_block(function, "then") == nullptr, "DCE-created trampoline block should be erased.");
    expect(find_block(function, "merge") == nullptr, "single-predecessor merge block should be folded into entry.");
    expect(entry->successors().empty(),
           "fully merged entry block should not keep outgoing CFG edges.");

    expect(entry->phi_nodes().empty(), "merged entry block should not contain phi nodes.");
    expect(entry->instructions().size() == 1,
           "optimized entry block should only keep the surviving constant.");

    const auto* out_number = dynamic_cast<const SSANumberNode*>(entry->instructions().front());
    expect(out_number != nullptr, "optimized output should remain a constant in entry.");

    const auto* ret = dynamic_cast<const SSAReturnNode*>(entry->terminal());
    expect(ret != nullptr, "fully merged entry block should end with return.");

    const std::string text = print_module(module);
    expect_not_contains(text, "then:", "printed IR should not contain the removed trampoline block.");
    expect_not_contains(text, "else:", "printed IR should not contain the dead else block.");
    expect_not_contains(text, "merge:", "printed IR should not contain the merged linear block.");
    expect_not_contains(text, "phi", "printed IR should not contain the lowered phi.");
}

}  // namespace

int main() {
    try {
        test_jump_only_trampoline_block_is_removed();
        test_default_pipeline_removes_jump_only_block_created_by_dce();
    } catch (const std::exception& ex) {
        std::cerr << "cfg_simplify_test FAILED: " << ex.what() << '\n';
        return 1;
    }

    std::cout << "cfg_simplify_test PASSED\n";
    return 0;
}
