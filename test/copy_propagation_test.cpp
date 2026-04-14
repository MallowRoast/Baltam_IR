#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "analysis/analysis_manager.h"
#include "analysis/verifier.h"
#include "ir/ir.h"
#include "ir/ir_printer.h"
#include "optimizer/copy_propagation.h"

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

analysis::PreservedAnalyses run_copy_propagation(Function& function) {
    analysis::verify_function_or_throw(function);

    analysis::FunctionAnalysisManager analysis_manager;
    optimizer::UntypedSSACopyPropagationPass pass;
    return pass.run(function, analysis_manager);
}

void test_same_name_copy_chain_is_propagated_into_return_and_erased() {
    Module module("copy_chain_module", "test/copy_propagation/copy_chain.m", Module::M_Function);
    Function& function = create_ssa_function(module, "copy_chain", {"x"});

    BasicBlock* entry = function.create_block("entry");
    function.set_entry_block(entry);

    const ValueId x1 = function.create_value("x");
    entry->append_instruction(function.create_node<SSANumberNode>(x1, std::int64_t{7}));

    const ValueId x2 = function.create_value("x");
    entry->append_instruction(function.create_node<SSACopyNode>(x2, ValueRef{x1}));

    const ValueId x3 = function.create_value("x");
    entry->append_instruction(function.create_node<SSACopyNode>(x3, ValueRef{x2}));
    entry->set_terminal(function.create_node<SSAReturnNode>(std::vector<ValueRef>{ValueRef{x3}}));

    const analysis::PreservedAnalyses preserved = run_copy_propagation(function);
    expect(!preserved.preserves_all(), "copy propagation should rewrite same-name copy chains.");

    analysis::verify_module_or_throw(module);

    expect(entry->instructions().size() == 1,
           "redundant same-name copies should be removed from the block.");
    expect(dynamic_cast<const SSANumberNode*>(entry->instructions().front()) != nullptr,
           "only the original constant definition should remain.");
    expect(!function.has_value(x2), "erased copy result should be tombstoned.");
    expect(!function.has_value(x3), "terminal copy result should be tombstoned.");

    const auto* ret = dynamic_cast<const SSAReturnNode*>(entry->terminal());
    expect(ret != nullptr, "entry should still end with a return.");
    expect(ret->values().size() == 1 && ret->values().front().id == x1,
           "return should be rewritten to the original source value.");

    const std::string text = print_module(module);
    expect_not_contains(text, " = copy ",
                        "printed IR should not keep redundant same-name copies.");
}

void test_copy_source_is_propagated_into_binop_use() {
    Module module("copy_use_module", "test/copy_propagation/copy_use.m", Module::M_Function);
    Function& function = create_ssa_function(module, "copy_use", {"out"});

    BasicBlock* entry = function.create_block("entry");
    function.set_entry_block(entry);

    const ValueId lhs = function.create_value("lhs");
    entry->append_instruction(function.create_node<SSANumberNode>(lhs, std::int64_t{3}));

    const ValueId alias = function.create_value("tmp");
    entry->append_instruction(function.create_node<SSACopyNode>(alias, ValueRef{lhs}));

    const ValueId rhs = function.create_value("rhs");
    entry->append_instruction(function.create_node<SSANumberNode>(rhs, std::int64_t{4}));

    const ValueId out = function.create_value("out");
    entry->append_instruction(
        function.create_node<SSABinOpNode>(BinOpType::Add, out, ValueRef{alias}, ValueRef{rhs}));
    entry->set_terminal(function.create_node<SSAReturnNode>(std::vector<ValueRef>{ValueRef{out}}));

    const analysis::PreservedAnalyses preserved = run_copy_propagation(function);
    expect(!preserved.preserves_all(), "copy propagation should rewrite binop operands.");

    analysis::verify_module_or_throw(module);

    expect(entry->instructions().size() == 4,
           "different-name copy should be left for later DCE once its uses are rewritten.");

    const auto* binop =
        dynamic_cast<const SSABinOpNode*>(entry->instructions()[3]);
    expect(binop != nullptr, "last instruction should remain the add node.");
    expect(binop->lhs().id == lhs, "binop lhs should be rewritten to the original value.");
    expect(function.has_value(alias),
           "different-name copy result should remain until a later cleanup pass removes it.");
}

}  // namespace

int main() {
    try {
        test_same_name_copy_chain_is_propagated_into_return_and_erased();
        test_copy_source_is_propagated_into_binop_use();
    } catch (const std::exception& ex) {
        std::cerr << "copy_propagation_test FAILED: " << ex.what() << '\n';
        return 1;
    }

    std::cout << "copy_propagation_test PASSED\n";
    return 0;
}
