#include <cmath>
#include <complex>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>

#include "analysis/analysis_manager.h"
#include "analysis/verifier.h"
#include "ir/ir.h"
#include "ir/ir_printer.h"
#include "optimizer/constant_fold.h"
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

Function& create_ssa_function(Module& module, const std::string& name) {
    Function* function = module.create_function(name, Function::PrimaryFunction);
    module.set_entry_function(function);
    function->set_stage(IRNode::UntypedSSA);
    function->set_output_names({"out"});
    return *function;
}

std::string print_module(const Module& module) {
    std::ostringstream oss;
    print_ir(oss, module);
    return oss.str();
}

void test_uminus_folds_to_constant() {
    Module module("fold_uminus_module", "test/constant_fold/fold_uminus.m", Module::M_Function);
    Function& function = create_ssa_function(module, "fold_uminus");

    BasicBlock* entry = function.create_block("entry");
    function.set_entry_block(entry);

    const ValueId input = function.create_value("input");
    entry->append_instruction(
        function.create_node<SSANumberNode>(input, SSANumberNode::NumberValue{3.5}));

    const ValueId out = function.create_value("out");
    entry->append_instruction(
        function.create_node<SSAUnaryOpNode>(UnaryOpType::UMinus, out, ValueRef{input}));
    entry->set_terminal(function.create_node<SSAReturnNode>(std::vector<ValueRef>{ValueRef{out}}));

    analysis::verify_module_or_throw(module);

    optimizer::optimize_function(function);
    analysis::verify_module_or_throw(module);

    const std::string text = print_module(module);
    expect_contains(text, "%out = const -3.5", "uminus should fold to a constant number.");
    expect_not_contains(text, "uminus %input", "uminus instruction should be removed.");

    IRNode* folded = entry->instructions()[1];
    const auto* number = dynamic_cast<const SSANumberNode*>(folded);
    expect(number != nullptr, "folded unary node should become SSANumberNode.");
    expect(std::holds_alternative<double>(number->value()),
           "folded uminus result should stay in double form.");
    expect(std::get<double>(number->value()) == -3.5,
           "folded uminus constant should equal -3.5.");
}

void test_copy_chain_and_ctranspose_fold() {
    Module module("fold_ctranspose_module", "test/constant_fold/fold_ctranspose.m",
                  Module::M_Function);
    Function& function = create_ssa_function(module, "fold_ctranspose");

    BasicBlock* entry = function.create_block("entry");
    function.set_entry_block(entry);

    const ValueId input = function.create_value("input");
    entry->append_instruction(function.create_node<SSANumberNode>(
        input, SSANumberNode::NumberValue{std::complex<double>{2.0, 3.0}}));

    const ValueId copied = function.create_value("copied");
    entry->append_instruction(function.create_node<SSACopyNode>(copied, ValueRef{input}));

    const ValueId out = function.create_value("out");
    entry->append_instruction(
        function.create_node<SSAUnaryOpNode>(UnaryOpType::CTranspose, out, ValueRef{copied}));
    entry->set_terminal(function.create_node<SSAReturnNode>(std::vector<ValueRef>{ValueRef{out}}));

    analysis::verify_module_or_throw(module);

    analysis::FunctionAnalysisManager analysis_manager;
    optimizer::UntypedSSAConstantFoldPass pass;
    const analysis::PreservedAnalyses preserved = pass.run(function, analysis_manager);
    expect(!preserved.preserves_all(), "successful fold should invalidate cached analyses.");

    analysis::verify_module_or_throw(module);

    IRNode* folded = entry->instructions()[2];
    const auto* number = dynamic_cast<const SSANumberNode*>(folded);
    expect(number != nullptr, "ctranspose should fold to SSANumberNode.");
    expect(std::holds_alternative<std::complex<double>>(number->value()),
           "ctranspose should preserve complex scalar form.");
    expect(std::get<std::complex<double>>(number->value()) == std::complex<double>(2.0, -3.0),
           "ctranspose should conjugate complex scalar constants.");
}

void test_not_folds_zero_to_true() {
    Module module("fold_not_module", "test/constant_fold/fold_not.m", Module::M_Function);
    Function& function = create_ssa_function(module, "fold_not");

    BasicBlock* entry = function.create_block("entry");
    function.set_entry_block(entry);

    const ValueId zero = function.create_value("zero");
    entry->append_instruction(
        function.create_node<SSANumberNode>(zero, SSANumberNode::NumberValue{0.0}));

    const ValueId out = function.create_value("out");
    entry->append_instruction(
        function.create_node<SSAUnaryOpNode>(UnaryOpType::Logic_Not, out, ValueRef{zero}));
    entry->set_terminal(function.create_node<SSAReturnNode>(std::vector<ValueRef>{ValueRef{out}}));

    analysis::verify_module_or_throw(module);

    optimizer::optimize_function(function);
    analysis::verify_module_or_throw(module);

    const auto* number = dynamic_cast<const SSANumberNode*>(entry->instructions()[1]);
    expect(number != nullptr, "not should fold to SSANumberNode.");
    expect(std::holds_alternative<bool>(number->value()), "not should produce bool constant.");
    expect(std::get<bool>(number->value()), "not 0 should fold to true.");
}

void test_not_on_complex_stays_unary() {
    Module module("fold_not_complex_module", "test/constant_fold/fold_not_complex.m",
                  Module::M_Function);
    Function& function = create_ssa_function(module, "fold_not_complex");

    BasicBlock* entry = function.create_block("entry");
    function.set_entry_block(entry);

    const ValueId input = function.create_value("input");
    entry->append_instruction(function.create_node<SSANumberNode>(
        input, SSANumberNode::NumberValue{std::complex<double>{0.0, 1.0}}));

    const ValueId out = function.create_value("out");
    entry->append_instruction(
        function.create_node<SSAUnaryOpNode>(UnaryOpType::Logic_Not, out, ValueRef{input}));
    entry->set_terminal(function.create_node<SSAReturnNode>(std::vector<ValueRef>{ValueRef{out}}));

    analysis::verify_module_or_throw(module);

    analysis::FunctionAnalysisManager analysis_manager;
    optimizer::UntypedSSAConstantFoldPass pass;
    const analysis::PreservedAnalyses preserved = pass.run(function, analysis_manager);
    expect(preserved.preserves_all(), "complex logic_not should not fold.");

    IRNode* instruction = entry->instructions()[1];
    expect(dynamic_cast<const SSAUnaryOpNode*>(instruction) != nullptr,
           "complex logic_not should remain unary.");
}

void test_bool_uplus_and_uminus_fold_to_double() {
    Module module("fold_bool_unary_module", "test/constant_fold/fold_bool_unary.m",
                  Module::M_Function);
    Function& function = create_ssa_function(module, "fold_bool_unary");
    function.set_output_names({"plus_out", "minus_out"});

    BasicBlock* entry = function.create_block("entry");
    function.set_entry_block(entry);

    const ValueId plus_input = function.create_value("plus_input");
    entry->append_instruction(
        function.create_node<SSANumberNode>(plus_input, SSANumberNode::NumberValue{true}));

    const ValueId plus_out = function.create_value("plus_out");
    entry->append_instruction(
        function.create_node<SSAUnaryOpNode>(UnaryOpType::UPlus, plus_out, ValueRef{plus_input}));

    const ValueId minus_input = function.create_value("minus_input");
    entry->append_instruction(
        function.create_node<SSANumberNode>(minus_input, SSANumberNode::NumberValue{true}));

    const ValueId minus_out = function.create_value("minus_out");
    entry->append_instruction(
        function.create_node<SSAUnaryOpNode>(UnaryOpType::UMinus, minus_out, ValueRef{minus_input}));
    entry->set_terminal(
        function.create_node<SSAReturnNode>(std::vector<ValueRef>{ValueRef{plus_out}, ValueRef{minus_out}}));

    analysis::verify_module_or_throw(module);

    analysis::FunctionAnalysisManager analysis_manager;
    optimizer::UntypedSSAConstantFoldPass pass;
    const analysis::PreservedAnalyses preserved = pass.run(function, analysis_manager);
    expect(!preserved.preserves_all(), "bool unary ops should fold.");

    const auto* plus_number = dynamic_cast<const SSANumberNode*>(entry->instructions()[1]);
    expect(plus_number != nullptr, "bool uplus should fold to SSANumberNode.");
    expect(std::holds_alternative<double>(plus_number->value()),
           "bool uplus should produce double constant.");
    expect(std::get<double>(plus_number->value()) == 1.0,
           "bool uplus(true) should fold to 1.0.");

    const auto* minus_number = dynamic_cast<const SSANumberNode*>(entry->instructions()[3]);
    expect(minus_number != nullptr, "bool uminus should fold to SSANumberNode.");
    expect(std::holds_alternative<double>(minus_number->value()),
           "bool uminus should produce double constant.");
    expect(std::get<double>(minus_number->value()) == -1.0,
           "bool uminus(true) should fold to -1.0.");
}

void test_integer_uminus_handles_signed_and_unsigned() {
    Module module("fold_integer_uminus_module", "test/constant_fold/fold_integer_uminus.m",
                  Module::M_Function);
    Function& function = create_ssa_function(module, "fold_integer_uminus");
    function.set_output_names({"signed_out", "unsigned_out"});

    BasicBlock* entry = function.create_block("entry");
    function.set_entry_block(entry);

    const ValueId signed_input = function.create_value("signed_input");
    entry->append_instruction(function.create_node<SSANumberNode>(
        signed_input, SSANumberNode::NumberValue{IntegerConstant(std::int8_t{7})}));

    const ValueId signed_out = function.create_value("signed_out");
    entry->append_instruction(function.create_node<SSAUnaryOpNode>(
        UnaryOpType::UMinus, signed_out, ValueRef{signed_input}));

    const ValueId unsigned_input = function.create_value("unsigned_input");
    entry->append_instruction(function.create_node<SSANumberNode>(
        unsigned_input, SSANumberNode::NumberValue{IntegerConstant(std::uint8_t{9})}));

    const ValueId unsigned_out = function.create_value("unsigned_out");
    entry->append_instruction(function.create_node<SSAUnaryOpNode>(
        UnaryOpType::UMinus, unsigned_out, ValueRef{unsigned_input}));
    entry->set_terminal(function.create_node<SSAReturnNode>(
        std::vector<ValueRef>{ValueRef{signed_out}, ValueRef{unsigned_out}}));

    analysis::verify_module_or_throw(module);

    analysis::FunctionAnalysisManager analysis_manager;
    optimizer::UntypedSSAConstantFoldPass pass;
    const analysis::PreservedAnalyses preserved = pass.run(function, analysis_manager);
    expect(!preserved.preserves_all(), "integer uminus should fold.");

    const auto* signed_number = dynamic_cast<const SSANumberNode*>(entry->instructions()[1]);
    expect(signed_number != nullptr, "signed integer uminus should fold to SSANumberNode.");
    const auto* signed_integer = std::get_if<IntegerConstant>(&signed_number->value());
    expect(signed_integer != nullptr, "signed integer uminus should stay integer.");
    expect(signed_integer->as_int8().has_value() && *signed_integer->as_int8() == std::int8_t{-7},
           "signed integer uminus should produce negated value.");

    const auto* unsigned_number = dynamic_cast<const SSANumberNode*>(entry->instructions()[3]);
    expect(unsigned_number != nullptr, "unsigned integer uminus should fold to SSANumberNode.");
    const auto* unsigned_integer = std::get_if<IntegerConstant>(&unsigned_number->value());
    expect(unsigned_integer != nullptr, "unsigned integer uminus should stay integer.");
    expect(unsigned_integer->as_uint8().has_value() &&
               *unsigned_integer->as_uint8() == std::uint8_t{0},
           "unsigned integer uminus should fold to zero.");
}

void test_signed_integer_uminus_min_value_folds_to_max() {
    Module module("fold_signed_integer_uminus_min_module",
                  "test/constant_fold/fold_signed_integer_uminus_min.m", Module::M_Function);
    Function& function = create_ssa_function(module, "fold_signed_integer_uminus_min");

    BasicBlock* entry = function.create_block("entry");
    function.set_entry_block(entry);

    const ValueId input = function.create_value("input");
    entry->append_instruction(function.create_node<SSANumberNode>(
        input, SSANumberNode::NumberValue{IntegerConstant(std::int8_t{-128})}));

    const ValueId out = function.create_value("out");
    entry->append_instruction(
        function.create_node<SSAUnaryOpNode>(UnaryOpType::UMinus, out, ValueRef{input}));
    entry->set_terminal(function.create_node<SSAReturnNode>(std::vector<ValueRef>{ValueRef{out}}));

    analysis::verify_module_or_throw(module);

    analysis::FunctionAnalysisManager analysis_manager;
    optimizer::UntypedSSAConstantFoldPass pass;
    const analysis::PreservedAnalyses preserved = pass.run(function, analysis_manager);
    expect(!preserved.preserves_all(), "signed minimum uminus should fold to signed maximum.");

    const auto* number = dynamic_cast<const SSANumberNode*>(entry->instructions()[1]);
    expect(number != nullptr, "signed minimum uminus should fold to SSANumberNode.");
    const auto* integer = std::get_if<IntegerConstant>(&number->value());
    expect(integer != nullptr, "signed minimum uminus should stay integer.");
    expect(integer->as_int8().has_value() &&
               *integer->as_int8() == std::numeric_limits<std::int8_t>::max(),
           "signed minimum uminus should fold to signed maximum.");
}

void test_add_bool_and_bool_folds_to_double() {
    Module module("fold_add_bool_bool_module", "test/constant_fold/fold_add_bool_bool.m",
                  Module::M_Function);
    Function& function = create_ssa_function(module, "fold_add_bool_bool");

    BasicBlock* entry = function.create_block("entry");
    function.set_entry_block(entry);

    const ValueId lhs = function.create_value("lhs");
    entry->append_instruction(function.create_node<SSANumberNode>(lhs, SSANumberNode::NumberValue{true}));

    const ValueId rhs = function.create_value("rhs");
    entry->append_instruction(function.create_node<SSANumberNode>(rhs, SSANumberNode::NumberValue{false}));

    const ValueId out = function.create_value("out");
    entry->append_instruction(
        function.create_node<SSABinOpNode>(BinOpType::Add, out, ValueRef{lhs}, ValueRef{rhs}));
    entry->set_terminal(function.create_node<SSAReturnNode>(std::vector<ValueRef>{ValueRef{out}}));

    analysis::verify_module_or_throw(module);

    optimizer::optimize_function(function);
    analysis::verify_module_or_throw(module);

    const auto* number = dynamic_cast<const SSANumberNode*>(entry->instructions()[2]);
    expect(number != nullptr, "bool + bool should fold to SSANumberNode.");
    expect(std::holds_alternative<double>(number->value()),
           "bool + bool should produce double constant.");
    expect(std::get<double>(number->value()) == 1.0,
           "true + false should fold to 1.0.");
}

void test_add_bool_integer_and_invalid_integer_combos_stay_binary() {
    Module module("keep_invalid_add_module", "test/constant_fold/keep_invalid_add.m",
                  Module::M_Function);
    Function& function = create_ssa_function(module, "keep_invalid_add");
    function.set_output_names({"out0", "out1", "out2"});

    BasicBlock* entry = function.create_block("entry");
    function.set_entry_block(entry);

    const ValueId bool_value = function.create_value("bool_value");
    entry->append_instruction(
        function.create_node<SSANumberNode>(bool_value, SSANumberNode::NumberValue{true}));
    const ValueId int8_value = function.create_value("int8_value");
    entry->append_instruction(function.create_node<SSANumberNode>(
        int8_value, SSANumberNode::NumberValue{IntegerConstant(std::int8_t{4})}));
    const ValueId out0 = function.create_value("out0");
    entry->append_instruction(function.create_node<SSABinOpNode>(
        BinOpType::Add, out0, ValueRef{bool_value}, ValueRef{int8_value}));

    const ValueId int16_value = function.create_value("int16_value");
    entry->append_instruction(function.create_node<SSANumberNode>(
        int16_value, SSANumberNode::NumberValue{IntegerConstant(std::int16_t{9})}));
    const ValueId out1 = function.create_value("out1");
    entry->append_instruction(function.create_node<SSABinOpNode>(
        BinOpType::Add, out1, ValueRef{int8_value}, ValueRef{int16_value}));

    const ValueId complex_value = function.create_value("complex_value");
    entry->append_instruction(function.create_node<SSANumberNode>(
        complex_value, SSANumberNode::NumberValue{std::complex<double>{1.0, 2.0}}));
    const ValueId out2 = function.create_value("out2");
    entry->append_instruction(function.create_node<SSABinOpNode>(
        BinOpType::Add, out2, ValueRef{int8_value}, ValueRef{complex_value}));
    entry->set_terminal(function.create_node<SSAReturnNode>(
        std::vector<ValueRef>{ValueRef{out0}, ValueRef{out1}, ValueRef{out2}}));

    analysis::verify_module_or_throw(module);

    analysis::FunctionAnalysisManager analysis_manager;
    optimizer::UntypedSSAConstantFoldPass pass;
    const analysis::PreservedAnalyses preserved = pass.run(function, analysis_manager);
    expect(preserved.preserves_all(), "invalid add combinations should not fold.");

    expect(dynamic_cast<const SSABinOpNode*>(entry->instructions()[2]) != nullptr,
           "bool + integer should remain binary.");
    expect(dynamic_cast<const SSABinOpNode*>(entry->instructions()[4]) != nullptr,
           "mismatched integer add should remain binary.");
    expect(dynamic_cast<const SSABinOpNode*>(entry->instructions()[6]) != nullptr,
           "integer + complex should remain binary.");
}

void test_add_integer_same_type_and_double_fold() {
    Module module("fold_add_integer_module", "test/constant_fold/fold_add_integer.m",
                  Module::M_Function);
    Function& function = create_ssa_function(module, "fold_add_integer");
    function.set_output_names({"out0", "out1", "out2", "out3"});

    BasicBlock* entry = function.create_block("entry");
    function.set_entry_block(entry);

    const ValueId signed_lhs = function.create_value("signed_lhs");
    entry->append_instruction(function.create_node<SSANumberNode>(
        signed_lhs, SSANumberNode::NumberValue{IntegerConstant(std::int8_t{120})}));
    const ValueId signed_rhs = function.create_value("signed_rhs");
    entry->append_instruction(function.create_node<SSANumberNode>(
        signed_rhs, SSANumberNode::NumberValue{IntegerConstant(std::int8_t{20})}));
    const ValueId out0 = function.create_value("out0");
    entry->append_instruction(function.create_node<SSABinOpNode>(
        BinOpType::Add, out0, ValueRef{signed_lhs}, ValueRef{signed_rhs}));

    const ValueId neg_lhs = function.create_value("neg_lhs");
    entry->append_instruction(function.create_node<SSANumberNode>(
        neg_lhs, SSANumberNode::NumberValue{IntegerConstant(std::int8_t{-120})}));
    const ValueId neg_rhs = function.create_value("neg_rhs");
    entry->append_instruction(function.create_node<SSANumberNode>(
        neg_rhs, SSANumberNode::NumberValue{IntegerConstant(std::int8_t{-20})}));
    const ValueId out1 = function.create_value("out1");
    entry->append_instruction(function.create_node<SSABinOpNode>(
        BinOpType::Add, out1, ValueRef{neg_lhs}, ValueRef{neg_rhs}));

    const ValueId unsigned_lhs = function.create_value("unsigned_lhs");
    entry->append_instruction(function.create_node<SSANumberNode>(
        unsigned_lhs, SSANumberNode::NumberValue{IntegerConstant(std::uint8_t{250})}));
    const ValueId unsigned_rhs = function.create_value("unsigned_rhs");
    entry->append_instruction(function.create_node<SSANumberNode>(
        unsigned_rhs, SSANumberNode::NumberValue{IntegerConstant(std::uint8_t{20})}));
    const ValueId out2 = function.create_value("out2");
    entry->append_instruction(function.create_node<SSABinOpNode>(
        BinOpType::Add, out2, ValueRef{unsigned_lhs}, ValueRef{unsigned_rhs}));

    const ValueId double_lhs = function.create_value("double_lhs");
    entry->append_instruction(function.create_node<SSANumberNode>(
        double_lhs, SSANumberNode::NumberValue{IntegerConstant(std::int8_t{2})}));
    const ValueId double_rhs = function.create_value("double_rhs");
    entry->append_instruction(
        function.create_node<SSANumberNode>(double_rhs, SSANumberNode::NumberValue{0.5}));
    const ValueId out3 = function.create_value("out3");
    entry->append_instruction(function.create_node<SSABinOpNode>(
        BinOpType::Add, out3, ValueRef{double_lhs}, ValueRef{double_rhs}));
    entry->set_terminal(function.create_node<SSAReturnNode>(
        std::vector<ValueRef>{ValueRef{out0}, ValueRef{out1}, ValueRef{out2}, ValueRef{out3}}));

    analysis::verify_module_or_throw(module);

    optimizer::optimize_function(function);
    analysis::verify_module_or_throw(module);

    const auto* sat_high = dynamic_cast<const SSANumberNode*>(entry->instructions()[2]);
    expect(sat_high != nullptr, "signed add should fold to SSANumberNode.");
    const auto* sat_high_integer = std::get_if<IntegerConstant>(&sat_high->value());
    expect(sat_high_integer != nullptr, "signed add should stay integer.");
    expect(sat_high_integer->as_int8().has_value() &&
               *sat_high_integer->as_int8() == std::numeric_limits<std::int8_t>::max(),
           "signed positive overflow should saturate to int8 max.");

    const auto* sat_low = dynamic_cast<const SSANumberNode*>(entry->instructions()[5]);
    expect(sat_low != nullptr, "negative signed add should fold to SSANumberNode.");
    const auto* sat_low_integer = std::get_if<IntegerConstant>(&sat_low->value());
    expect(sat_low_integer != nullptr, "negative signed add should stay integer.");
    expect(sat_low_integer->as_int8().has_value() &&
               *sat_low_integer->as_int8() == std::numeric_limits<std::int8_t>::min(),
           "signed negative overflow should saturate to int8 min.");

    const auto* sat_unsigned = dynamic_cast<const SSANumberNode*>(entry->instructions()[8]);
    expect(sat_unsigned != nullptr, "unsigned add should fold to SSANumberNode.");
    const auto* sat_unsigned_integer = std::get_if<IntegerConstant>(&sat_unsigned->value());
    expect(sat_unsigned_integer != nullptr, "unsigned add should stay integer.");
    expect(sat_unsigned_integer->as_uint8().has_value() &&
               *sat_unsigned_integer->as_uint8() == std::numeric_limits<std::uint8_t>::max(),
           "unsigned overflow should saturate to uint8 max.");

    const auto* to_double = dynamic_cast<const SSANumberNode*>(entry->instructions()[11]);
    expect(to_double != nullptr, "integer + double should fold to SSANumberNode.");
    expect(std::holds_alternative<double>(to_double->value()),
           "integer + double should produce double constant.");
    expect(std::get<double>(to_double->value()) == 2.5,
           "integer + double should fold to 2.5.");
}

void test_add_double_and_complex_variants_fold() {
    Module module("fold_add_mixed_module", "test/constant_fold/fold_add_mixed.m",
                  Module::M_Function);
    Function& function = create_ssa_function(module, "fold_add_mixed");
    function.set_output_names({"out0", "out1", "out2", "out3"});

    BasicBlock* entry = function.create_block("entry");
    function.set_entry_block(entry);

    const ValueId bool_value = function.create_value("bool_value");
    entry->append_instruction(
        function.create_node<SSANumberNode>(bool_value, SSANumberNode::NumberValue{true}));
    const ValueId complex0 = function.create_value("complex0");
    entry->append_instruction(function.create_node<SSANumberNode>(
        complex0, SSANumberNode::NumberValue{std::complex<double>{2.0, 3.0}}));
    const ValueId out0 = function.create_value("out0");
    entry->append_instruction(function.create_node<SSABinOpNode>(
        BinOpType::Add, out0, ValueRef{bool_value}, ValueRef{complex0}));

    const ValueId double0 = function.create_value("double0");
    entry->append_instruction(
        function.create_node<SSANumberNode>(double0, SSANumberNode::NumberValue{1.5}));
    const ValueId out1 = function.create_value("out1");
    entry->append_instruction(function.create_node<SSABinOpNode>(
        BinOpType::Add, out1, ValueRef{double0}, ValueRef{bool_value}));

    const ValueId out2 = function.create_value("out2");
    entry->append_instruction(function.create_node<SSABinOpNode>(
        BinOpType::Add, out2, ValueRef{double0}, ValueRef{complex0}));

    const ValueId complex1 = function.create_value("complex1");
    entry->append_instruction(function.create_node<SSANumberNode>(
        complex1, SSANumberNode::NumberValue{std::complex<double>{1.0, 2.0}}));
    const ValueId complex2 = function.create_value("complex2");
    entry->append_instruction(function.create_node<SSANumberNode>(
        complex2, SSANumberNode::NumberValue{std::complex<double>{3.0, 4.0}}));
    const ValueId out3 = function.create_value("out3");
    entry->append_instruction(function.create_node<SSABinOpNode>(
        BinOpType::Add, out3, ValueRef{complex1}, ValueRef{complex2}));
    entry->set_terminal(function.create_node<SSAReturnNode>(
        std::vector<ValueRef>{ValueRef{out0}, ValueRef{out1}, ValueRef{out2}, ValueRef{out3}}));

    analysis::verify_module_or_throw(module);

    optimizer::optimize_function(function);
    analysis::verify_module_or_throw(module);

    const auto* bool_complex = dynamic_cast<const SSANumberNode*>(entry->instructions()[2]);
    expect(bool_complex != nullptr, "bool + complex should fold to SSANumberNode.");
    expect(std::holds_alternative<std::complex<double>>(bool_complex->value()),
           "bool + complex should produce complex constant.");
    expect(std::get<std::complex<double>>(bool_complex->value()) == std::complex<double>(3.0, 3.0),
           "bool + complex should fold to shifted complex value.");

    const auto* double_bool = dynamic_cast<const SSANumberNode*>(entry->instructions()[4]);
    expect(double_bool != nullptr, "double + bool should fold to SSANumberNode.");
    expect(std::holds_alternative<double>(double_bool->value()),
           "double + bool should produce double constant.");
    expect(std::get<double>(double_bool->value()) == 2.5,
           "double + bool should fold to 2.5.");

    const auto* double_complex = dynamic_cast<const SSANumberNode*>(entry->instructions()[5]);
    expect(double_complex != nullptr, "double + complex should fold to SSANumberNode.");
    expect(std::holds_alternative<std::complex<double>>(double_complex->value()),
           "double + complex should produce complex constant.");
    expect(std::get<std::complex<double>>(double_complex->value()) ==
               std::complex<double>(3.5, 3.0),
           "double + complex should fold to shifted complex value.");

    const auto* complex_complex = dynamic_cast<const SSANumberNode*>(entry->instructions()[8]);
    expect(complex_complex != nullptr, "complex + complex should fold to SSANumberNode.");
    expect(std::holds_alternative<std::complex<double>>(complex_complex->value()),
           "complex + complex should produce complex constant.");
    expect(std::get<std::complex<double>>(complex_complex->value()) ==
               std::complex<double>(4.0, 6.0),
           "complex + complex should fold to summed complex value.");
}

void test_subtract_bool_and_bool_folds_to_double() {
    Module module("fold_subtract_bool_bool_module",
                  "test/constant_fold/fold_subtract_bool_bool.m", Module::M_Function);
    Function& function = create_ssa_function(module, "fold_subtract_bool_bool");

    BasicBlock* entry = function.create_block("entry");
    function.set_entry_block(entry);

    const ValueId lhs = function.create_value("lhs");
    entry->append_instruction(
        function.create_node<SSANumberNode>(lhs, SSANumberNode::NumberValue{true}));

    const ValueId rhs = function.create_value("rhs");
    entry->append_instruction(
        function.create_node<SSANumberNode>(rhs, SSANumberNode::NumberValue{false}));

    const ValueId out = function.create_value("out");
    entry->append_instruction(function.create_node<SSABinOpNode>(
        BinOpType::Subtract, out, ValueRef{lhs}, ValueRef{rhs}));
    entry->set_terminal(function.create_node<SSAReturnNode>(std::vector<ValueRef>{ValueRef{out}}));

    analysis::verify_module_or_throw(module);

    optimizer::optimize_function(function);
    analysis::verify_module_or_throw(module);

    const auto* number = dynamic_cast<const SSANumberNode*>(entry->instructions()[2]);
    expect(number != nullptr, "bool - bool should fold to SSANumberNode.");
    expect(std::holds_alternative<double>(number->value()),
           "bool - bool should produce double constant.");
    expect(std::get<double>(number->value()) == 1.0,
           "true - false should fold to 1.0.");
}

void test_subtract_bool_integer_and_invalid_integer_combos_stay_binary() {
    Module module("keep_invalid_subtract_module", "test/constant_fold/keep_invalid_subtract.m",
                  Module::M_Function);
    Function& function = create_ssa_function(module, "keep_invalid_subtract");
    function.set_output_names({"out0", "out1", "out2"});

    BasicBlock* entry = function.create_block("entry");
    function.set_entry_block(entry);

    const ValueId bool_value = function.create_value("bool_value");
    entry->append_instruction(
        function.create_node<SSANumberNode>(bool_value, SSANumberNode::NumberValue{true}));
    const ValueId int8_value = function.create_value("int8_value");
    entry->append_instruction(function.create_node<SSANumberNode>(
        int8_value, SSANumberNode::NumberValue{IntegerConstant(std::int8_t{4})}));
    const ValueId out0 = function.create_value("out0");
    entry->append_instruction(function.create_node<SSABinOpNode>(
        BinOpType::Subtract, out0, ValueRef{bool_value}, ValueRef{int8_value}));

    const ValueId int16_value = function.create_value("int16_value");
    entry->append_instruction(function.create_node<SSANumberNode>(
        int16_value, SSANumberNode::NumberValue{IntegerConstant(std::int16_t{9})}));
    const ValueId out1 = function.create_value("out1");
    entry->append_instruction(function.create_node<SSABinOpNode>(
        BinOpType::Subtract, out1, ValueRef{int8_value}, ValueRef{int16_value}));

    const ValueId complex_value = function.create_value("complex_value");
    entry->append_instruction(function.create_node<SSANumberNode>(
        complex_value, SSANumberNode::NumberValue{std::complex<double>{1.0, 2.0}}));
    const ValueId out2 = function.create_value("out2");
    entry->append_instruction(function.create_node<SSABinOpNode>(
        BinOpType::Subtract, out2, ValueRef{int8_value}, ValueRef{complex_value}));
    entry->set_terminal(function.create_node<SSAReturnNode>(
        std::vector<ValueRef>{ValueRef{out0}, ValueRef{out1}, ValueRef{out2}}));

    analysis::verify_module_or_throw(module);

    analysis::FunctionAnalysisManager analysis_manager;
    optimizer::UntypedSSAConstantFoldPass pass;
    const analysis::PreservedAnalyses preserved = pass.run(function, analysis_manager);
    expect(preserved.preserves_all(), "invalid subtract combinations should not fold.");

    expect(dynamic_cast<const SSABinOpNode*>(entry->instructions()[2]) != nullptr,
           "bool - integer should remain binary.");
    expect(dynamic_cast<const SSABinOpNode*>(entry->instructions()[4]) != nullptr,
           "mismatched integer subtract should remain binary.");
    expect(dynamic_cast<const SSABinOpNode*>(entry->instructions()[6]) != nullptr,
           "integer - complex should remain binary.");
}

void test_subtract_integer_same_type_and_double_fold() {
    Module module("fold_subtract_integer_module", "test/constant_fold/fold_subtract_integer.m",
                  Module::M_Function);
    Function& function = create_ssa_function(module, "fold_subtract_integer");
    function.set_output_names({"out0", "out1", "out2", "out3"});

    BasicBlock* entry = function.create_block("entry");
    function.set_entry_block(entry);

    const ValueId signed_lhs = function.create_value("signed_lhs");
    entry->append_instruction(function.create_node<SSANumberNode>(
        signed_lhs, SSANumberNode::NumberValue{IntegerConstant(std::int8_t{120})}));
    const ValueId signed_rhs = function.create_value("signed_rhs");
    entry->append_instruction(function.create_node<SSANumberNode>(
        signed_rhs, SSANumberNode::NumberValue{IntegerConstant(std::int8_t{-20})}));
    const ValueId out0 = function.create_value("out0");
    entry->append_instruction(function.create_node<SSABinOpNode>(
        BinOpType::Subtract, out0, ValueRef{signed_lhs}, ValueRef{signed_rhs}));

    const ValueId neg_lhs = function.create_value("neg_lhs");
    entry->append_instruction(function.create_node<SSANumberNode>(
        neg_lhs, SSANumberNode::NumberValue{IntegerConstant(std::int8_t{-120})}));
    const ValueId neg_rhs = function.create_value("neg_rhs");
    entry->append_instruction(function.create_node<SSANumberNode>(
        neg_rhs, SSANumberNode::NumberValue{IntegerConstant(std::int8_t{20})}));
    const ValueId out1 = function.create_value("out1");
    entry->append_instruction(function.create_node<SSABinOpNode>(
        BinOpType::Subtract, out1, ValueRef{neg_lhs}, ValueRef{neg_rhs}));

    const ValueId unsigned_lhs = function.create_value("unsigned_lhs");
    entry->append_instruction(function.create_node<SSANumberNode>(
        unsigned_lhs, SSANumberNode::NumberValue{IntegerConstant(std::uint8_t{10})}));
    const ValueId unsigned_rhs = function.create_value("unsigned_rhs");
    entry->append_instruction(function.create_node<SSANumberNode>(
        unsigned_rhs, SSANumberNode::NumberValue{IntegerConstant(std::uint8_t{20})}));
    const ValueId out2 = function.create_value("out2");
    entry->append_instruction(function.create_node<SSABinOpNode>(
        BinOpType::Subtract, out2, ValueRef{unsigned_lhs}, ValueRef{unsigned_rhs}));

    const ValueId double_lhs = function.create_value("double_lhs");
    entry->append_instruction(function.create_node<SSANumberNode>(
        double_lhs, SSANumberNode::NumberValue{IntegerConstant(std::int8_t{2})}));
    const ValueId double_rhs = function.create_value("double_rhs");
    entry->append_instruction(
        function.create_node<SSANumberNode>(double_rhs, SSANumberNode::NumberValue{0.5}));
    const ValueId out3 = function.create_value("out3");
    entry->append_instruction(function.create_node<SSABinOpNode>(
        BinOpType::Subtract, out3, ValueRef{double_lhs}, ValueRef{double_rhs}));
    entry->set_terminal(function.create_node<SSAReturnNode>(
        std::vector<ValueRef>{ValueRef{out0}, ValueRef{out1}, ValueRef{out2}, ValueRef{out3}}));

    analysis::verify_module_or_throw(module);

    optimizer::optimize_function(function);
    analysis::verify_module_or_throw(module);

    const auto* sat_high = dynamic_cast<const SSANumberNode*>(entry->instructions()[2]);
    expect(sat_high != nullptr, "signed subtract should fold to SSANumberNode.");
    const auto* sat_high_integer = std::get_if<IntegerConstant>(&sat_high->value());
    expect(sat_high_integer != nullptr, "signed subtract should stay integer.");
    expect(sat_high_integer->as_int8().has_value() &&
               *sat_high_integer->as_int8() == std::numeric_limits<std::int8_t>::max(),
           "signed positive overflow should saturate to int8 max.");

    const auto* sat_low = dynamic_cast<const SSANumberNode*>(entry->instructions()[5]);
    expect(sat_low != nullptr, "negative signed subtract should fold to SSANumberNode.");
    const auto* sat_low_integer = std::get_if<IntegerConstant>(&sat_low->value());
    expect(sat_low_integer != nullptr, "negative signed subtract should stay integer.");
    expect(sat_low_integer->as_int8().has_value() &&
               *sat_low_integer->as_int8() == std::numeric_limits<std::int8_t>::min(),
           "signed negative overflow should saturate to int8 min.");

    const auto* sat_unsigned = dynamic_cast<const SSANumberNode*>(entry->instructions()[8]);
    expect(sat_unsigned != nullptr, "unsigned subtract should fold to SSANumberNode.");
    const auto* sat_unsigned_integer = std::get_if<IntegerConstant>(&sat_unsigned->value());
    expect(sat_unsigned_integer != nullptr, "unsigned subtract should stay integer.");
    expect(sat_unsigned_integer->as_uint8().has_value() &&
               *sat_unsigned_integer->as_uint8() == static_cast<std::uint8_t>(0),
           "unsigned underflow should saturate to zero.");

    const auto* to_double = dynamic_cast<const SSANumberNode*>(entry->instructions()[11]);
    expect(to_double != nullptr, "integer - double should fold to SSANumberNode.");
    expect(std::holds_alternative<double>(to_double->value()),
           "integer - double should produce double constant.");
    expect(std::get<double>(to_double->value()) == 1.5,
           "integer - double should fold to 1.5.");
}

void test_subtract_double_and_complex_variants_fold() {
    Module module("fold_subtract_mixed_module", "test/constant_fold/fold_subtract_mixed.m",
                  Module::M_Function);
    Function& function = create_ssa_function(module, "fold_subtract_mixed");
    function.set_output_names({"out0", "out1", "out2", "out3"});

    BasicBlock* entry = function.create_block("entry");
    function.set_entry_block(entry);

    const ValueId bool_value = function.create_value("bool_value");
    entry->append_instruction(
        function.create_node<SSANumberNode>(bool_value, SSANumberNode::NumberValue{true}));
    const ValueId complex0 = function.create_value("complex0");
    entry->append_instruction(function.create_node<SSANumberNode>(
        complex0, SSANumberNode::NumberValue{std::complex<double>{2.0, 3.0}}));
    const ValueId out0 = function.create_value("out0");
    entry->append_instruction(function.create_node<SSABinOpNode>(
        BinOpType::Subtract, out0, ValueRef{bool_value}, ValueRef{complex0}));

    const ValueId double0 = function.create_value("double0");
    entry->append_instruction(
        function.create_node<SSANumberNode>(double0, SSANumberNode::NumberValue{1.5}));
    const ValueId out1 = function.create_value("out1");
    entry->append_instruction(function.create_node<SSABinOpNode>(
        BinOpType::Subtract, out1, ValueRef{double0}, ValueRef{bool_value}));

    const ValueId out2 = function.create_value("out2");
    entry->append_instruction(function.create_node<SSABinOpNode>(
        BinOpType::Subtract, out2, ValueRef{double0}, ValueRef{complex0}));

    const ValueId complex1 = function.create_value("complex1");
    entry->append_instruction(function.create_node<SSANumberNode>(
        complex1, SSANumberNode::NumberValue{std::complex<double>{3.0, 4.0}}));
    const ValueId complex2 = function.create_value("complex2");
    entry->append_instruction(function.create_node<SSANumberNode>(
        complex2, SSANumberNode::NumberValue{std::complex<double>{1.0, 2.0}}));
    const ValueId out3 = function.create_value("out3");
    entry->append_instruction(function.create_node<SSABinOpNode>(
        BinOpType::Subtract, out3, ValueRef{complex1}, ValueRef{complex2}));
    entry->set_terminal(function.create_node<SSAReturnNode>(
        std::vector<ValueRef>{ValueRef{out0}, ValueRef{out1}, ValueRef{out2}, ValueRef{out3}}));

    analysis::verify_module_or_throw(module);

    optimizer::optimize_function(function);
    analysis::verify_module_or_throw(module);

    const auto* bool_complex = dynamic_cast<const SSANumberNode*>(entry->instructions()[2]);
    expect(bool_complex != nullptr, "bool - complex should fold to SSANumberNode.");
    expect(std::holds_alternative<std::complex<double>>(bool_complex->value()),
           "bool - complex should produce complex constant.");
    expect(std::get<std::complex<double>>(bool_complex->value()) ==
               std::complex<double>(-1.0, -3.0),
           "bool - complex should fold to shifted complex value.");

    const auto* double_bool = dynamic_cast<const SSANumberNode*>(entry->instructions()[4]);
    expect(double_bool != nullptr, "double - bool should fold to SSANumberNode.");
    expect(std::holds_alternative<double>(double_bool->value()),
           "double - bool should produce double constant.");
    expect(std::get<double>(double_bool->value()) == 0.5,
           "double - bool should fold to 0.5.");

    const auto* double_complex = dynamic_cast<const SSANumberNode*>(entry->instructions()[5]);
    expect(double_complex != nullptr, "double - complex should fold to SSANumberNode.");
    expect(std::holds_alternative<std::complex<double>>(double_complex->value()),
           "double - complex should produce complex constant.");
    expect(std::get<std::complex<double>>(double_complex->value()) ==
               std::complex<double>(-0.5, -3.0),
           "double - complex should fold to shifted complex value.");

    const auto* complex_complex = dynamic_cast<const SSANumberNode*>(entry->instructions()[8]);
    expect(complex_complex != nullptr, "complex - complex should fold to SSANumberNode.");
    expect(std::holds_alternative<std::complex<double>>(complex_complex->value()),
           "complex - complex should produce complex constant.");
    expect(std::get<std::complex<double>>(complex_complex->value()) ==
               std::complex<double>(2.0, 2.0),
           "complex - complex should fold to subtracted complex value.");
}

void test_times_bool_and_bool_folds_to_double() {
    Module module("fold_times_bool_bool_module", "test/constant_fold/fold_times_bool_bool.m",
                  Module::M_Function);
    Function& function = create_ssa_function(module, "fold_times_bool_bool");

    BasicBlock* entry = function.create_block("entry");
    function.set_entry_block(entry);

    const ValueId lhs = function.create_value("lhs");
    entry->append_instruction(
        function.create_node<SSANumberNode>(lhs, SSANumberNode::NumberValue{true}));

    const ValueId rhs = function.create_value("rhs");
    entry->append_instruction(
        function.create_node<SSANumberNode>(rhs, SSANumberNode::NumberValue{false}));

    const ValueId out = function.create_value("out");
    entry->append_instruction(
        function.create_node<SSABinOpNode>(BinOpType::Times, out, ValueRef{lhs}, ValueRef{rhs}));
    entry->set_terminal(function.create_node<SSAReturnNode>(std::vector<ValueRef>{ValueRef{out}}));

    analysis::verify_module_or_throw(module);

    optimizer::optimize_function(function);
    analysis::verify_module_or_throw(module);

    const auto* number = dynamic_cast<const SSANumberNode*>(entry->instructions()[2]);
    expect(number != nullptr, "bool .* bool should fold to SSANumberNode.");
    expect(std::holds_alternative<double>(number->value()),
           "bool .* bool should produce double constant.");
    expect(std::get<double>(number->value()) == 0.0,
           "true .* false should fold to 0.0.");
}

void test_times_invalid_integer_combos_stay_binary() {
    Module module("keep_invalid_times_module", "test/constant_fold/keep_invalid_times.m",
                  Module::M_Function);
    Function& function = create_ssa_function(module, "keep_invalid_times");
    function.set_output_names({"out0", "out1", "out2"});

    BasicBlock* entry = function.create_block("entry");
    function.set_entry_block(entry);

    const ValueId bool_value = function.create_value("bool_value");
    entry->append_instruction(
        function.create_node<SSANumberNode>(bool_value, SSANumberNode::NumberValue{true}));
    const ValueId int8_value = function.create_value("int8_value");
    entry->append_instruction(function.create_node<SSANumberNode>(
        int8_value, SSANumberNode::NumberValue{IntegerConstant(std::int8_t{4})}));
    const ValueId out0 = function.create_value("out0");
    entry->append_instruction(
        function.create_node<SSABinOpNode>(BinOpType::Times, out0, ValueRef{bool_value},
                                           ValueRef{int8_value}));

    const ValueId int16_value = function.create_value("int16_value");
    entry->append_instruction(function.create_node<SSANumberNode>(
        int16_value, SSANumberNode::NumberValue{IntegerConstant(std::int16_t{9})}));
    const ValueId out1 = function.create_value("out1");
    entry->append_instruction(function.create_node<SSABinOpNode>(
        BinOpType::Times, out1, ValueRef{int8_value}, ValueRef{int16_value}));

    const ValueId complex_value = function.create_value("complex_value");
    entry->append_instruction(function.create_node<SSANumberNode>(
        complex_value, SSANumberNode::NumberValue{std::complex<double>{1.0, 2.0}}));
    const ValueId out2 = function.create_value("out2");
    entry->append_instruction(function.create_node<SSABinOpNode>(
        BinOpType::Times, out2, ValueRef{int8_value}, ValueRef{complex_value}));
    entry->set_terminal(function.create_node<SSAReturnNode>(
        std::vector<ValueRef>{ValueRef{out0}, ValueRef{out1}, ValueRef{out2}}));

    analysis::verify_module_or_throw(module);

    analysis::FunctionAnalysisManager analysis_manager;
    optimizer::UntypedSSAConstantFoldPass pass;
    const analysis::PreservedAnalyses preserved = pass.run(function, analysis_manager);
    expect(preserved.preserves_all(), "invalid times combinations should not fold.");

    expect(dynamic_cast<const SSABinOpNode*>(entry->instructions()[2]) != nullptr,
           "bool .* integer should remain binary.");
    expect(dynamic_cast<const SSABinOpNode*>(entry->instructions()[4]) != nullptr,
           "mismatched integer times should remain binary.");
    expect(dynamic_cast<const SSABinOpNode*>(entry->instructions()[6]) != nullptr,
           "integer .* complex should remain binary.");
}

void test_times_integer_same_type_and_double_fold() {
    Module module("fold_times_integer_module", "test/constant_fold/fold_times_integer.m",
                  Module::M_Function);
    Function& function = create_ssa_function(module, "fold_times_integer");
    function.set_output_names({"out0", "out1", "out2", "out3", "out4"});

    BasicBlock* entry = function.create_block("entry");
    function.set_entry_block(entry);

    const ValueId signed_lhs = function.create_value("signed_lhs");
    entry->append_instruction(function.create_node<SSANumberNode>(
        signed_lhs, SSANumberNode::NumberValue{IntegerConstant(std::int8_t{20})}));
    const ValueId signed_rhs = function.create_value("signed_rhs");
    entry->append_instruction(function.create_node<SSANumberNode>(
        signed_rhs, SSANumberNode::NumberValue{IntegerConstant(std::int8_t{7})}));
    const ValueId out0 = function.create_value("out0");
    entry->append_instruction(
        function.create_node<SSABinOpNode>(BinOpType::Times, out0, ValueRef{signed_lhs},
                                           ValueRef{signed_rhs}));

    const ValueId neg_lhs = function.create_value("neg_lhs");
    entry->append_instruction(function.create_node<SSANumberNode>(
        neg_lhs, SSANumberNode::NumberValue{IntegerConstant(std::int8_t{-20})}));
    const ValueId neg_rhs = function.create_value("neg_rhs");
    entry->append_instruction(function.create_node<SSANumberNode>(
        neg_rhs, SSANumberNode::NumberValue{IntegerConstant(std::int8_t{7})}));
    const ValueId out1 = function.create_value("out1");
    entry->append_instruction(
        function.create_node<SSABinOpNode>(BinOpType::Times, out1, ValueRef{neg_lhs},
                                           ValueRef{neg_rhs}));

    const ValueId unsigned_lhs = function.create_value("unsigned_lhs");
    entry->append_instruction(function.create_node<SSANumberNode>(
        unsigned_lhs, SSANumberNode::NumberValue{IntegerConstant(std::uint8_t{40})}));
    const ValueId unsigned_rhs = function.create_value("unsigned_rhs");
    entry->append_instruction(function.create_node<SSANumberNode>(
        unsigned_rhs, SSANumberNode::NumberValue{IntegerConstant(std::uint8_t{7})}));
    const ValueId out2 = function.create_value("out2");
    entry->append_instruction(
        function.create_node<SSABinOpNode>(BinOpType::Times, out2, ValueRef{unsigned_lhs},
                                           ValueRef{unsigned_rhs}));

    const ValueId double_lhs = function.create_value("double_lhs");
    entry->append_instruction(function.create_node<SSANumberNode>(
        double_lhs, SSANumberNode::NumberValue{IntegerConstant(std::int8_t{2})}));
    const ValueId double_rhs = function.create_value("double_rhs");
    entry->append_instruction(
        function.create_node<SSANumberNode>(double_rhs, SSANumberNode::NumberValue{0.5}));
    const ValueId out3 = function.create_value("out3");
    entry->append_instruction(
        function.create_node<SSABinOpNode>(BinOpType::Times, out3, ValueRef{double_lhs},
                                           ValueRef{double_rhs}));

    const ValueId int_lhs = function.create_value("int_lhs");
    entry->append_instruction(
        function.create_node<SSANumberNode>(int_lhs, SSANumberNode::NumberValue{1.5}));
    const ValueId int_rhs = function.create_value("int_rhs");
    entry->append_instruction(function.create_node<SSANumberNode>(
        int_rhs, SSANumberNode::NumberValue{IntegerConstant(std::int8_t{2})}));
    const ValueId out4 = function.create_value("out4");
    entry->append_instruction(
        function.create_node<SSABinOpNode>(BinOpType::Times, out4, ValueRef{int_lhs},
                                           ValueRef{int_rhs}));
    entry->set_terminal(function.create_node<SSAReturnNode>(
        std::vector<ValueRef>{ValueRef{out0}, ValueRef{out1}, ValueRef{out2},
                              ValueRef{out3}, ValueRef{out4}}));

    analysis::verify_module_or_throw(module);

    optimizer::optimize_function(function);
    analysis::verify_module_or_throw(module);

    const auto* sat_high = dynamic_cast<const SSANumberNode*>(entry->instructions()[2]);
    expect(sat_high != nullptr, "signed times should fold to SSANumberNode.");
    const auto* sat_high_integer = std::get_if<IntegerConstant>(&sat_high->value());
    expect(sat_high_integer != nullptr, "signed times should stay integer.");
    expect(sat_high_integer->as_int8().has_value() &&
               *sat_high_integer->as_int8() == std::numeric_limits<std::int8_t>::max(),
           "signed positive overflow should saturate to int8 max.");

    const auto* sat_low = dynamic_cast<const SSANumberNode*>(entry->instructions()[5]);
    expect(sat_low != nullptr, "negative signed times should fold to SSANumberNode.");
    const auto* sat_low_integer = std::get_if<IntegerConstant>(&sat_low->value());
    expect(sat_low_integer != nullptr, "negative signed times should stay integer.");
    expect(sat_low_integer->as_int8().has_value() &&
               *sat_low_integer->as_int8() == std::numeric_limits<std::int8_t>::min(),
           "signed negative overflow should saturate to int8 min.");

    const auto* sat_unsigned = dynamic_cast<const SSANumberNode*>(entry->instructions()[8]);
    expect(sat_unsigned != nullptr, "unsigned times should fold to SSANumberNode.");
    const auto* sat_unsigned_integer = std::get_if<IntegerConstant>(&sat_unsigned->value());
    expect(sat_unsigned_integer != nullptr, "unsigned times should stay integer.");
    expect(sat_unsigned_integer->as_uint8().has_value() &&
               *sat_unsigned_integer->as_uint8() == std::numeric_limits<std::uint8_t>::max(),
           "unsigned overflow should saturate to uint8 max.");

    const auto* to_integer = dynamic_cast<const SSANumberNode*>(entry->instructions()[11]);
    expect(to_integer != nullptr, "integer .* double should fold to SSANumberNode.");
    const auto* to_integer_value = std::get_if<IntegerConstant>(&to_integer->value());
    expect(to_integer_value != nullptr, "integer .* double should produce integer constant.");
    expect(to_integer_value->as_int8().has_value() &&
               *to_integer_value->as_int8() == static_cast<std::int8_t>(1),
           "integer .* double should fold to int8(1).");

    const auto* from_double = dynamic_cast<const SSANumberNode*>(entry->instructions()[14]);
    expect(from_double != nullptr, "double .* integer should fold to SSANumberNode.");
    const auto* from_double_value = std::get_if<IntegerConstant>(&from_double->value());
    expect(from_double_value != nullptr, "double .* integer should produce integer constant.");
    expect(from_double_value->as_int8().has_value() &&
               *from_double_value->as_int8() == static_cast<std::int8_t>(3),
           "double .* integer should fold to int8(3).");
}

void test_multiply_double_and_complex_variants_fold() {
    Module module("fold_multiply_mixed_module", "test/constant_fold/fold_multiply_mixed.m",
                  Module::M_Function);
    Function& function = create_ssa_function(module, "fold_multiply_mixed");
    function.set_output_names({"out0", "out1", "out2", "out3", "out4", "out5"});

    BasicBlock* entry = function.create_block("entry");
    function.set_entry_block(entry);

    const ValueId bool_value = function.create_value("bool_value");
    entry->append_instruction(
        function.create_node<SSANumberNode>(bool_value, SSANumberNode::NumberValue{true}));
    const ValueId complex0 = function.create_value("complex0");
    entry->append_instruction(function.create_node<SSANumberNode>(
        complex0, SSANumberNode::NumberValue{std::complex<double>{2.0, 3.0}}));
    const ValueId out0 = function.create_value("out0");
    entry->append_instruction(function.create_node<SSABinOpNode>(
        BinOpType::Multiply, out0, ValueRef{bool_value}, ValueRef{complex0}));

    const ValueId false_bool = function.create_value("false_bool");
    entry->append_instruction(
        function.create_node<SSANumberNode>(false_bool, SSANumberNode::NumberValue{false}));
    const ValueId nan_double = function.create_value("nan_double");
    entry->append_instruction(function.create_node<SSANumberNode>(
        nan_double,
        SSANumberNode::NumberValue{std::numeric_limits<double>::quiet_NaN()}));
    const ValueId out1 = function.create_value("out1");
    entry->append_instruction(function.create_node<SSABinOpNode>(
        BinOpType::Multiply, out1, ValueRef{false_bool}, ValueRef{nan_double}));

    const ValueId zero_real_complex = function.create_value("zero_real_complex");
    entry->append_instruction(function.create_node<SSANumberNode>(
        zero_real_complex, SSANumberNode::NumberValue{std::complex<double>{0.0, 2.0}}));
    const ValueId out2 = function.create_value("out2");
    entry->append_instruction(function.create_node<SSABinOpNode>(
        BinOpType::Multiply, out2, ValueRef{nan_double}, ValueRef{zero_real_complex}));

    const ValueId complex3 = function.create_value("complex3");
    entry->append_instruction(function.create_node<SSANumberNode>(
        complex3,
        SSANumberNode::NumberValue{std::complex<double>{std::numeric_limits<double>::quiet_NaN(),
                                                        2.0}}));
    const ValueId out3 = function.create_value("out3");
    entry->append_instruction(function.create_node<SSABinOpNode>(
        BinOpType::Multiply, out3, ValueRef{false_bool}, ValueRef{complex3}));

    const ValueId complex1 = function.create_value("complex1");
    entry->append_instruction(function.create_node<SSANumberNode>(
        complex1, SSANumberNode::NumberValue{std::complex<double>{0.0, 1.0}}));
    const ValueId complex2 = function.create_value("complex2");
    entry->append_instruction(function.create_node<SSANumberNode>(
        complex2,
        SSANumberNode::NumberValue{std::complex<double>{std::numeric_limits<double>::quiet_NaN(),
                                                        0.0}}));
    const ValueId out4 = function.create_value("out4");
    entry->append_instruction(function.create_node<SSABinOpNode>(
        BinOpType::Multiply, out4, ValueRef{complex1}, ValueRef{complex2}));

    const ValueId double0 = function.create_value("double0");
    entry->append_instruction(
        function.create_node<SSANumberNode>(double0, SSANumberNode::NumberValue{1.5}));
    const ValueId out5 = function.create_value("out5");
    entry->append_instruction(function.create_node<SSABinOpNode>(
        BinOpType::Multiply, out5, ValueRef{double0}, ValueRef{bool_value}));
    entry->set_terminal(function.create_node<SSAReturnNode>(
        std::vector<ValueRef>{ValueRef{out0}, ValueRef{out1}, ValueRef{out2},
                              ValueRef{out3}, ValueRef{out4}, ValueRef{out5}}));

    analysis::verify_module_or_throw(module);

    optimizer::optimize_function(function);
    analysis::verify_module_or_throw(module);

    const auto* bool_complex = dynamic_cast<const SSANumberNode*>(entry->instructions()[2]);
    expect(bool_complex != nullptr, "bool * complex should fold to SSANumberNode.");
    expect(std::holds_alternative<std::complex<double>>(bool_complex->value()),
           "bool * complex should produce complex constant.");
    expect(std::get<std::complex<double>>(bool_complex->value()) == std::complex<double>(2.0, 3.0),
           "true * complex should keep the complex value.");

    const auto* false_nan = dynamic_cast<const SSANumberNode*>(entry->instructions()[5]);
    expect(false_nan != nullptr, "false * nan should fold to SSANumberNode.");
    expect(std::holds_alternative<double>(false_nan->value()),
           "false * nan should produce double constant.");
    expect(std::isnan(std::get<double>(false_nan->value())),
           "false * nan should fold to nan.");

    const auto* zero_complex = dynamic_cast<const SSANumberNode*>(entry->instructions()[7]);
    expect(zero_complex != nullptr, "double * complex should fold to SSANumberNode.");
    expect(std::holds_alternative<std::complex<double>>(zero_complex->value()),
           "double * complex should produce complex constant.");
    const std::complex<double> zero_complex_value =
        std::get<std::complex<double>>(zero_complex->value());
    expect(zero_complex_value.real() == 0.0,
           "nan * complex(0, y) should keep zero real part.");
    expect(std::isnan(zero_complex_value.imag()),
           "nan * complex(0, y) should keep non-zero imaginary part as nan.");

    const auto* false_complex = dynamic_cast<const SSANumberNode*>(entry->instructions()[9]);
    expect(false_complex != nullptr, "false * complex should fold to SSANumberNode.");
    expect(std::holds_alternative<std::complex<double>>(false_complex->value()),
           "false * complex should produce complex constant.");
    const std::complex<double> false_complex_value =
        std::get<std::complex<double>>(false_complex->value());
    expect(std::isnan(false_complex_value.real()),
           "false * complex(nan, y) should keep nan real part.");
    expect(false_complex_value.imag() == 0.0,
           "false * complex(nan, y) should keep zero imaginary part.");

    const auto* complex_complex = dynamic_cast<const SSANumberNode*>(entry->instructions()[12]);
    expect(complex_complex != nullptr, "complex * complex should fold to SSANumberNode.");
    expect(std::holds_alternative<std::complex<double>>(complex_complex->value()),
           "complex * complex should produce complex constant.");
    const std::complex<double> complex_complex_value =
        std::get<std::complex<double>>(complex_complex->value());
    expect(complex_complex_value.real() == 0.0,
           "zero term in complex multiplication should keep real part at zero.");
    expect(std::isnan(complex_complex_value.imag()),
           "non-zero nan term in complex multiplication should remain nan.");

    const auto* double_bool = dynamic_cast<const SSANumberNode*>(entry->instructions()[14]);
    expect(double_bool != nullptr, "double * bool should fold to SSANumberNode.");
    expect(std::holds_alternative<double>(double_bool->value()),
           "double * bool should produce double constant.");
    expect(std::get<double>(double_bool->value()) == 1.5,
           "double * true should fold to 1.5.");
}

void test_divide_direction_and_matrix_variants_fold() {
    Module module("fold_divide_direction_module", "test/constant_fold/fold_divide_direction.m",
                  Module::M_Function);
    Function& function = create_ssa_function(module, "fold_divide_direction");
    function.set_output_names({"out0", "out1", "out2", "out3"});

    BasicBlock* entry = function.create_block("entry");
    function.set_entry_block(entry);

    const ValueId two = function.create_value("two");
    entry->append_instruction(function.create_node<SSANumberNode>(two, SSANumberNode::NumberValue{2.0}));
    const ValueId eight = function.create_value("eight");
    entry->append_instruction(function.create_node<SSANumberNode>(eight, SSANumberNode::NumberValue{8.0}));

    const ValueId out0 = function.create_value("out0");
    entry->append_instruction(
        function.create_node<SSABinOpNode>(BinOpType::RDivide, out0, ValueRef{eight}, ValueRef{two}));
    const ValueId out1 = function.create_value("out1");
    entry->append_instruction(
        function.create_node<SSABinOpNode>(BinOpType::LDivide, out1, ValueRef{two}, ValueRef{eight}));
    const ValueId out2 = function.create_value("out2");
    entry->append_instruction(function.create_node<SSABinOpNode>(
        BinOpType::MRightDivide, out2, ValueRef{eight}, ValueRef{two}));
    const ValueId out3 = function.create_value("out3");
    entry->append_instruction(function.create_node<SSABinOpNode>(
        BinOpType::MLeftDivide, out3, ValueRef{two}, ValueRef{eight}));
    entry->set_terminal(function.create_node<SSAReturnNode>(
        std::vector<ValueRef>{ValueRef{out0}, ValueRef{out1}, ValueRef{out2}, ValueRef{out3}}));

    analysis::verify_module_or_throw(module);

    optimizer::optimize_function(function);
    analysis::verify_module_or_throw(module);

    for (std::size_t index = 2; index <= 5; ++index) {
        const auto* number = dynamic_cast<const SSANumberNode*>(entry->instructions()[index]);
        expect(number != nullptr, "divide direction variants should fold to SSANumberNode.");
        expect(std::holds_alternative<double>(number->value()),
               "divide direction variants should produce double constants.");
        expect(std::get<double>(number->value()) == 4.0,
               "left/right divide and matrix divide should all fold to 4.0.");
    }
}

void test_divide_invalid_integer_combos_and_integer_zero_stay_binary() {
    Module module("keep_invalid_divide_module", "test/constant_fold/keep_invalid_divide.m",
                  Module::M_Function);
    Function& function = create_ssa_function(module, "keep_invalid_divide");
    function.set_output_names({"out0", "out1", "out2", "out3"});

    BasicBlock* entry = function.create_block("entry");
    function.set_entry_block(entry);

    const ValueId bool_value = function.create_value("bool_value");
    entry->append_instruction(
        function.create_node<SSANumberNode>(bool_value, SSANumberNode::NumberValue{true}));
    const ValueId int8_value = function.create_value("int8_value");
    entry->append_instruction(function.create_node<SSANumberNode>(
        int8_value, SSANumberNode::NumberValue{IntegerConstant(std::int8_t{4})}));
    const ValueId out0 = function.create_value("out0");
    entry->append_instruction(function.create_node<SSABinOpNode>(
        BinOpType::RDivide, out0, ValueRef{bool_value}, ValueRef{int8_value}));

    const ValueId int16_value = function.create_value("int16_value");
    entry->append_instruction(function.create_node<SSANumberNode>(
        int16_value, SSANumberNode::NumberValue{IntegerConstant(std::int16_t{9})}));
    const ValueId out1 = function.create_value("out1");
    entry->append_instruction(function.create_node<SSABinOpNode>(
        BinOpType::RDivide, out1, ValueRef{int8_value}, ValueRef{int16_value}));

    const ValueId complex_value = function.create_value("complex_value");
    entry->append_instruction(function.create_node<SSANumberNode>(
        complex_value, SSANumberNode::NumberValue{std::complex<double>{1.0, 2.0}}));
    const ValueId out2 = function.create_value("out2");
    entry->append_instruction(function.create_node<SSABinOpNode>(
        BinOpType::RDivide, out2, ValueRef{int8_value}, ValueRef{complex_value}));

    const ValueId zero_value = function.create_value("zero_value");
    entry->append_instruction(function.create_node<SSANumberNode>(
        zero_value, SSANumberNode::NumberValue{IntegerConstant(std::int8_t{0})}));
    const ValueId out3 = function.create_value("out3");
    entry->append_instruction(function.create_node<SSABinOpNode>(
        BinOpType::RDivide, out3, ValueRef{int8_value}, ValueRef{zero_value}));
    entry->set_terminal(function.create_node<SSAReturnNode>(
        std::vector<ValueRef>{ValueRef{out0}, ValueRef{out1}, ValueRef{out2}, ValueRef{out3}}));

    analysis::verify_module_or_throw(module);

    analysis::FunctionAnalysisManager analysis_manager;
    optimizer::UntypedSSAConstantFoldPass pass;
    const analysis::PreservedAnalyses preserved = pass.run(function, analysis_manager);
    expect(preserved.preserves_all(), "invalid divide combinations should not fold.");

    expect(dynamic_cast<const SSABinOpNode*>(entry->instructions()[2]) != nullptr,
           "bool / integer should remain binary.");
    expect(dynamic_cast<const SSABinOpNode*>(entry->instructions()[4]) != nullptr,
           "mismatched integer divide should remain binary.");
    expect(dynamic_cast<const SSABinOpNode*>(entry->instructions()[6]) != nullptr,
           "integer / complex should remain binary.");
    expect(dynamic_cast<const SSABinOpNode*>(entry->instructions()[8]) != nullptr,
           "integer / 0 should remain binary.");
}

void test_rdivide_integer_same_type_and_double_fold() {
    Module module("fold_rdivide_integer_module", "test/constant_fold/fold_rdivide_integer.m",
                  Module::M_Function);
    Function& function = create_ssa_function(module, "fold_rdivide_integer");
    function.set_output_names({"out0", "out1", "out2", "out3", "out4", "out5"});

    BasicBlock* entry = function.create_block("entry");
    function.set_entry_block(entry);

    const ValueId signed_lhs = function.create_value("signed_lhs");
    entry->append_instruction(function.create_node<SSANumberNode>(
        signed_lhs, SSANumberNode::NumberValue{IntegerConstant(std::int8_t{7})}));
    const ValueId signed_rhs = function.create_value("signed_rhs");
    entry->append_instruction(function.create_node<SSANumberNode>(
        signed_rhs, SSANumberNode::NumberValue{IntegerConstant(std::int8_t{2})}));
    const ValueId out0 = function.create_value("out0");
    entry->append_instruction(function.create_node<SSABinOpNode>(
        BinOpType::RDivide, out0, ValueRef{signed_lhs}, ValueRef{signed_rhs}));

    const ValueId min_lhs = function.create_value("min_lhs");
    entry->append_instruction(function.create_node<SSANumberNode>(
        min_lhs, SSANumberNode::NumberValue{IntegerConstant(std::int8_t{-128})}));
    const ValueId minus_one = function.create_value("minus_one");
    entry->append_instruction(function.create_node<SSANumberNode>(
        minus_one, SSANumberNode::NumberValue{IntegerConstant(std::int8_t{-1})}));
    const ValueId out1 = function.create_value("out1");
    entry->append_instruction(function.create_node<SSABinOpNode>(
        BinOpType::RDivide, out1, ValueRef{min_lhs}, ValueRef{minus_one}));

    const ValueId unsigned_lhs = function.create_value("unsigned_lhs");
    entry->append_instruction(function.create_node<SSANumberNode>(
        unsigned_lhs, SSANumberNode::NumberValue{IntegerConstant(std::uint8_t{9})}));
    const ValueId unsigned_rhs = function.create_value("unsigned_rhs");
    entry->append_instruction(function.create_node<SSANumberNode>(
        unsigned_rhs, SSANumberNode::NumberValue{IntegerConstant(std::uint8_t{2})}));
    const ValueId out2 = function.create_value("out2");
    entry->append_instruction(function.create_node<SSABinOpNode>(
        BinOpType::RDivide, out2, ValueRef{unsigned_lhs}, ValueRef{unsigned_rhs}));

    const ValueId int_double_lhs = function.create_value("int_double_lhs");
    entry->append_instruction(function.create_node<SSANumberNode>(
        int_double_lhs, SSANumberNode::NumberValue{IntegerConstant(std::int8_t{7})}));
    const ValueId int_double_rhs = function.create_value("int_double_rhs");
    entry->append_instruction(
        function.create_node<SSANumberNode>(int_double_rhs, SSANumberNode::NumberValue{2.0}));
    const ValueId out3 = function.create_value("out3");
    entry->append_instruction(function.create_node<SSABinOpNode>(
        BinOpType::RDivide, out3, ValueRef{int_double_lhs}, ValueRef{int_double_rhs}));

    const ValueId double_int_lhs = function.create_value("double_int_lhs");
    entry->append_instruction(
        function.create_node<SSANumberNode>(double_int_lhs, SSANumberNode::NumberValue{7.0}));
    const ValueId double_int_rhs = function.create_value("double_int_rhs");
    entry->append_instruction(function.create_node<SSANumberNode>(
        double_int_rhs, SSANumberNode::NumberValue{IntegerConstant(std::int8_t{2})}));
    const ValueId out4 = function.create_value("out4");
    entry->append_instruction(function.create_node<SSABinOpNode>(
        BinOpType::RDivide, out4, ValueRef{double_int_lhs}, ValueRef{double_int_rhs}));

    const ValueId overflow_lhs = function.create_value("overflow_lhs");
    entry->append_instruction(function.create_node<SSANumberNode>(
        overflow_lhs, SSANumberNode::NumberValue{IntegerConstant(std::int8_t{100})}));
    const ValueId overflow_rhs = function.create_value("overflow_rhs");
    entry->append_instruction(
        function.create_node<SSANumberNode>(overflow_rhs, SSANumberNode::NumberValue{0.5}));
    const ValueId out5 = function.create_value("out5");
    entry->append_instruction(function.create_node<SSABinOpNode>(
        BinOpType::RDivide, out5, ValueRef{overflow_lhs}, ValueRef{overflow_rhs}));
    entry->set_terminal(function.create_node<SSAReturnNode>(
        std::vector<ValueRef>{ValueRef{out0}, ValueRef{out1}, ValueRef{out2},
                              ValueRef{out3}, ValueRef{out4}, ValueRef{out5}}));

    analysis::verify_module_or_throw(module);

    optimizer::optimize_function(function);
    analysis::verify_module_or_throw(module);

    const auto* signed_div = dynamic_cast<const SSANumberNode*>(entry->instructions()[2]);
    expect(signed_div != nullptr, "signed divide should fold to SSANumberNode.");
    const auto* signed_div_value = std::get_if<IntegerConstant>(&signed_div->value());
    expect(signed_div_value != nullptr, "signed divide should stay integer.");
    expect(signed_div_value->as_int8().has_value() &&
               *signed_div_value->as_int8() == static_cast<std::int8_t>(3),
           "signed divide should truncate toward zero.");

    const auto* min_div = dynamic_cast<const SSANumberNode*>(entry->instructions()[5]);
    expect(min_div != nullptr, "signed min / -1 should fold to SSANumberNode.");
    const auto* min_div_value = std::get_if<IntegerConstant>(&min_div->value());
    expect(min_div_value != nullptr, "signed min / -1 should stay integer.");
    expect(min_div_value->as_int8().has_value() &&
               *min_div_value->as_int8() == std::numeric_limits<std::int8_t>::max(),
           "signed min / -1 should saturate to int8 max.");

    const auto* unsigned_div = dynamic_cast<const SSANumberNode*>(entry->instructions()[8]);
    expect(unsigned_div != nullptr, "unsigned divide should fold to SSANumberNode.");
    const auto* unsigned_div_value = std::get_if<IntegerConstant>(&unsigned_div->value());
    expect(unsigned_div_value != nullptr, "unsigned divide should stay integer.");
    expect(unsigned_div_value->as_uint8().has_value() &&
               *unsigned_div_value->as_uint8() == static_cast<std::uint8_t>(4),
           "unsigned divide should truncate toward zero.");

    const auto* int_double_div = dynamic_cast<const SSANumberNode*>(entry->instructions()[11]);
    expect(int_double_div != nullptr, "integer / double should fold to SSANumberNode.");
    const auto* int_double_div_value = std::get_if<IntegerConstant>(&int_double_div->value());
    expect(int_double_div_value != nullptr, "integer / double should produce integer constant.");
    expect(int_double_div_value->as_int8().has_value() &&
               *int_double_div_value->as_int8() == static_cast<std::int8_t>(3),
           "integer / double should fold to int8(3).");

    const auto* double_int_div = dynamic_cast<const SSANumberNode*>(entry->instructions()[14]);
    expect(double_int_div != nullptr, "double / integer should fold to SSANumberNode.");
    const auto* double_int_div_value = std::get_if<IntegerConstant>(&double_int_div->value());
    expect(double_int_div_value != nullptr, "double / integer should produce integer constant.");
    expect(double_int_div_value->as_int8().has_value() &&
               *double_int_div_value->as_int8() == static_cast<std::int8_t>(3),
           "double / integer should fold to int8(3).");

    const auto* overflow_div = dynamic_cast<const SSANumberNode*>(entry->instructions()[17]);
    expect(overflow_div != nullptr, "overflowing integer / double should fold to SSANumberNode.");
    const auto* overflow_div_value = std::get_if<IntegerConstant>(&overflow_div->value());
    expect(overflow_div_value != nullptr,
           "overflowing integer / double should produce integer constant.");
    expect(overflow_div_value->as_int8().has_value() &&
               *overflow_div_value->as_int8() == std::numeric_limits<std::int8_t>::max(),
           "overflowing integer / double should saturate to int8 max.");
}

void test_divide_double_and_complex_variants_fold() {
    Module module("fold_divide_mixed_module", "test/constant_fold/fold_divide_mixed.m",
                  Module::M_Function);
    Function& function = create_ssa_function(module, "fold_divide_mixed");
    function.set_output_names({"out0", "out1", "out2", "out3", "out4", "out5", "out6"});

    BasicBlock* entry = function.create_block("entry");
    function.set_entry_block(entry);

    const ValueId true_bool = function.create_value("true_bool");
    entry->append_instruction(
        function.create_node<SSANumberNode>(true_bool, SSANumberNode::NumberValue{true}));
    const ValueId false_bool = function.create_value("false_bool");
    entry->append_instruction(
        function.create_node<SSANumberNode>(false_bool, SSANumberNode::NumberValue{false}));
    const ValueId out0 = function.create_value("out0");
    entry->append_instruction(function.create_node<SSABinOpNode>(
        BinOpType::RDivide, out0, ValueRef{true_bool}, ValueRef{false_bool}));

    const ValueId pure_real = function.create_value("pure_real");
    entry->append_instruction(function.create_node<SSANumberNode>(
        pure_real, SSANumberNode::NumberValue{std::complex<double>{2.0, 0.0}}));
    const ValueId out1 = function.create_value("out1");
    entry->append_instruction(function.create_node<SSABinOpNode>(
        BinOpType::RDivide, out1, ValueRef{true_bool}, ValueRef{pure_real}));

    const ValueId one_double = function.create_value("one_double");
    entry->append_instruction(
        function.create_node<SSANumberNode>(one_double, SSANumberNode::NumberValue{1.0}));
    const ValueId pure_imag = function.create_value("pure_imag");
    entry->append_instruction(function.create_node<SSANumberNode>(
        pure_imag, SSANumberNode::NumberValue{std::complex<double>{0.0, 2.0}}));
    const ValueId out2 = function.create_value("out2");
    entry->append_instruction(function.create_node<SSABinOpNode>(
        BinOpType::RDivide, out2, ValueRef{one_double}, ValueRef{pure_imag}));

    const ValueId complex0 = function.create_value("complex0");
    entry->append_instruction(function.create_node<SSANumberNode>(
        complex0, SSANumberNode::NumberValue{std::complex<double>{2.0, 4.0}}));
    const ValueId two_double = function.create_value("two_double");
    entry->append_instruction(
        function.create_node<SSANumberNode>(two_double, SSANumberNode::NumberValue{2.0}));
    const ValueId out3 = function.create_value("out3");
    entry->append_instruction(function.create_node<SSABinOpNode>(
        BinOpType::RDivide, out3, ValueRef{complex0}, ValueRef{two_double}));

    const ValueId complex1 = function.create_value("complex1");
    entry->append_instruction(function.create_node<SSANumberNode>(
        complex1, SSANumberNode::NumberValue{std::complex<double>{2.0, 4.0}}));
    const ValueId pure_imag2 = function.create_value("pure_imag2");
    entry->append_instruction(function.create_node<SSANumberNode>(
        pure_imag2, SSANumberNode::NumberValue{std::complex<double>{0.0, 2.0}}));
    const ValueId out4 = function.create_value("out4");
    entry->append_instruction(function.create_node<SSABinOpNode>(
        BinOpType::RDivide, out4, ValueRef{complex1}, ValueRef{pure_imag2}));

    const ValueId zero_double = function.create_value("zero_double");
    entry->append_instruction(
        function.create_node<SSANumberNode>(zero_double, SSANumberNode::NumberValue{0.0}));
    const ValueId out5 = function.create_value("out5");
    entry->append_instruction(function.create_node<SSABinOpNode>(
        BinOpType::RDivide, out5, ValueRef{one_double}, ValueRef{zero_double}));
    const ValueId out6 = function.create_value("out6");
    entry->append_instruction(function.create_node<SSABinOpNode>(
        BinOpType::RDivide, out6, ValueRef{zero_double}, ValueRef{zero_double}));
    entry->set_terminal(function.create_node<SSAReturnNode>(
        std::vector<ValueRef>{ValueRef{out0}, ValueRef{out1}, ValueRef{out2}, ValueRef{out3},
                              ValueRef{out4}, ValueRef{out5}, ValueRef{out6}}));

    analysis::verify_module_or_throw(module);

    optimizer::optimize_function(function);
    analysis::verify_module_or_throw(module);

    const auto* bool_bool = dynamic_cast<const SSANumberNode*>(entry->instructions()[2]);
    expect(bool_bool != nullptr, "bool / bool should fold to SSANumberNode.");
    expect(std::holds_alternative<double>(bool_bool->value()),
           "bool / bool should produce double constant.");
    expect(std::isinf(std::get<double>(bool_bool->value())),
           "true / false should fold to inf.");

    const auto* bool_complex = dynamic_cast<const SSANumberNode*>(entry->instructions()[4]);
    expect(bool_complex != nullptr, "bool / pure-real complex should fold to SSANumberNode.");
    expect(std::holds_alternative<std::complex<double>>(bool_complex->value()),
           "bool / pure-real complex should produce complex constant.");
    expect(std::get<std::complex<double>>(bool_complex->value()) ==
               std::complex<double>(0.5, 0.0),
           "bool / complex(2, 0) should fold to 0.5 + 0i.");

    const auto* double_complex = dynamic_cast<const SSANumberNode*>(entry->instructions()[7]);
    expect(double_complex != nullptr, "double / pure-imag complex should fold to SSANumberNode.");
    expect(std::holds_alternative<std::complex<double>>(double_complex->value()),
           "double / pure-imag complex should produce complex constant.");
    expect(std::get<std::complex<double>>(double_complex->value()) ==
               std::complex<double>(0.0, -0.5),
           "1 / complex(0, 2) should fold to -0.5i.");

    const auto* complex_double = dynamic_cast<const SSANumberNode*>(entry->instructions()[10]);
    expect(complex_double != nullptr, "complex / double should fold to SSANumberNode.");
    expect(std::holds_alternative<std::complex<double>>(complex_double->value()),
           "complex / double should produce complex constant.");
    expect(std::get<std::complex<double>>(complex_double->value()) ==
               std::complex<double>(1.0, 2.0),
           "complex(2, 4) / 2 should fold to complex(1, 2).");

    const auto* complex_complex = dynamic_cast<const SSANumberNode*>(entry->instructions()[13]);
    expect(complex_complex != nullptr, "complex / pure-imag complex should fold to SSANumberNode.");
    expect(std::holds_alternative<std::complex<double>>(complex_complex->value()),
           "complex / pure-imag complex should produce complex constant.");
    expect(std::get<std::complex<double>>(complex_complex->value()) ==
               std::complex<double>(2.0, -1.0),
           "complex(2, 4) / complex(0, 2) should fold to complex(2, -1).");

    const auto* inf_double = dynamic_cast<const SSANumberNode*>(entry->instructions()[15]);
    expect(inf_double != nullptr, "1 / 0 should fold to SSANumberNode.");
    expect(std::holds_alternative<double>(inf_double->value()),
           "1 / 0 should produce double constant.");
    expect(std::isinf(std::get<double>(inf_double->value())),
           "1 / 0 should fold to inf.");

    const auto* nan_double = dynamic_cast<const SSANumberNode*>(entry->instructions()[16]);
    expect(nan_double != nullptr, "0 / 0 should fold to SSANumberNode.");
    expect(std::holds_alternative<double>(nan_double->value()),
           "0 / 0 should produce double constant.");
    expect(std::isnan(std::get<double>(nan_double->value())),
           "0 / 0 should fold to nan.");
}

void test_integer_constant_accessors() {
    const IntegerConstant minus_one{std::int8_t{-1}};
    expect(minus_one.as_int8().has_value() && *minus_one.as_int8() == static_cast<std::int8_t>(-1),
           "signed 8-bit constant should decode as int8.");
    expect(!minus_one.as_int64().has_value(),
           "signed 8-bit constant should not decode as int64.");
    expect(!minus_one.as_uint8().has_value(),
           "negative signed constant should not decode as uint8.");
    expect(minus_one.type() == IntegerConstant::Type::Int8,
           "signed 8-bit constant should report Int8 type.");
    expect(minus_one.as_double() == -1.0,
           "signed 8-bit constant should convert to double.");
    expect(!minus_one.is_zero(), "minus_one should not be zero.");
    expect(minus_one.negated().as_int8().has_value() &&
               *minus_one.negated().as_int8() == static_cast<std::int8_t>(1),
           "minus_one negated should become +1.");

    const IntegerConstant two_fifty_five{std::uint8_t{255}};
    expect(two_fifty_five.as_uint8().has_value() &&
               *two_fifty_five.as_uint8() == static_cast<std::uint8_t>(255),
           "unsigned 8-bit constant should decode as uint8.");
    expect(!two_fifty_five.as_int16().has_value(),
           "unsigned 8-bit constant should not decode as int16.");
    expect(!two_fifty_five.as_int8().has_value(),
           "unsigned 8-bit constant should not decode as int8.");
    expect(two_fifty_five.type() == IntegerConstant::Type::UInt8,
           "unsigned 8-bit constant should report UInt8 type.");
    expect(two_fifty_five.as_double() == 255.0,
           "unsigned 8-bit constant should convert to double.");
    expect(!two_fifty_five.is_zero(), "255 should not be zero.");
    expect(two_fifty_five.negated().as_uint8().has_value() &&
               *two_fifty_five.negated().as_uint8() == static_cast<std::uint8_t>(0),
           "unsigned negated should fold to zero.");

    const IntegerConstant zero{std::uint16_t{0}};
    expect(zero.is_zero(), "zero should report zero.");

    const IntegerConstant min_int8{std::int8_t{-128}};
    expect(min_int8.negated().as_int8().has_value() &&
               *min_int8.negated().as_int8() == std::numeric_limits<std::int8_t>::max(),
           "signed minimum negated should become signed maximum.");
}

}  // namespace

int main() {
    try {
        test_uminus_folds_to_constant();
        test_copy_chain_and_ctranspose_fold();
        test_not_folds_zero_to_true();
        test_not_on_complex_stays_unary();
        test_bool_uplus_and_uminus_fold_to_double();
        test_integer_uminus_handles_signed_and_unsigned();
        test_signed_integer_uminus_min_value_folds_to_max();
        test_add_bool_and_bool_folds_to_double();
        test_add_bool_integer_and_invalid_integer_combos_stay_binary();
        test_add_integer_same_type_and_double_fold();
        test_add_double_and_complex_variants_fold();
        test_subtract_bool_and_bool_folds_to_double();
        test_subtract_bool_integer_and_invalid_integer_combos_stay_binary();
        test_subtract_integer_same_type_and_double_fold();
        test_subtract_double_and_complex_variants_fold();
        test_times_bool_and_bool_folds_to_double();
        test_times_invalid_integer_combos_stay_binary();
        test_times_integer_same_type_and_double_fold();
        test_multiply_double_and_complex_variants_fold();
        test_divide_direction_and_matrix_variants_fold();
        test_divide_invalid_integer_combos_and_integer_zero_stay_binary();
        test_rdivide_integer_same_type_and_double_fold();
        test_divide_double_and_complex_variants_fold();
        test_integer_constant_accessors();
    } catch (const std::exception& ex) {
        std::cerr << "constant_fold_test FAILED: " << ex.what() << '\n';
        return 1;
    }

    std::cout << "constant_fold_test PASSED\n";
    return 0;
}
