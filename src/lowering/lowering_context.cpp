#include "lowering/lowering_context.h"

#include <sstream>
#include <stdexcept>
#include <utility>

namespace baltam {
namespace {

ValueRef value_ref_from_instruction(Instruction* instruction, std::size_t index = 0) {
    return instruction == nullptr ? ValueRef{} : instruction->value_ref(index);
}

std::vector<ValueRef> collect_value_refs(const std::vector<Instruction*>& instructions) {
    std::vector<ValueRef> refs;
    refs.reserve(instructions.size());
    for (Instruction* instruction : instructions) {
        refs.push_back(value_ref_from_instruction(instruction));
    }
    return refs;
}

}  // namespace

Function& LoweringContext::function() {
    if (current_block == nullptr || current_block->parent() == nullptr) {
        throw std::runtime_error("IR lower 时找不到当前函数。");
    }
    return *current_block->parent();
}

const Function& LoweringContext::function() const {
    return const_cast<LoweringContext*>(this)->function();
}

BasicBlock* LoweringContext::create_block(const std::string& prefix) {
    std::ostringstream oss;
    oss << prefix << "_" << next_block_id++;
    return function().create_block(oss.str());
}

std::string LoweringContext::create_hidden_name(const std::string& prefix) {
    std::ostringstream oss;
    oss << "__" << prefix << "_" << next_hidden_id++;
    return oss.str();
}

ValueRef LoweringContext::lookup_symbol_value_ref(const std::string& name) const {
    const auto it = symbol_table.find(name);
    return it == symbol_table.end() ? ValueRef{} : it->second;
}

void LoweringContext::bind_symbol_value(const std::string& name, ValueRef ref) {
    if (name.empty() || !ref.is_valid()) {
        return;
    }
    symbol_table[name] = ref;
}

void LoweringContext::erase_symbol_value(const std::string& name) {
    symbol_table.erase(name);
}

void LoweringContext::append_instruction(Instruction* instruction) {
    if (current_block == nullptr) {
        throw std::runtime_error("IR lower 指令时找不到当前基本块。");
    }
    current_block->append_instruction(instruction);
}

Instruction* LoweringContext::append_valued_instruction(Instruction* instruction,
                                                        std::string debug_name) {
    if (instruction == nullptr) {
        return nullptr;
    }
    function().attach_single_value(*instruction, std::move(debug_name),
                                   instruction->source_location());
    append_instruction(instruction);
    return instruction;
}

Instruction* LoweringContext::append_valued_instruction(
    Instruction* instruction, const std::vector<std::string>& debug_names) {
    if (instruction == nullptr) {
        return nullptr;
    }

    std::vector<InstValue> values;
    const std::size_t value_count = debug_names.empty() ? 1 : debug_names.size();
    values.reserve(value_count);
    for (std::size_t i = 0; i < value_count; ++i) {
        values.push_back(function().create_value(
            debug_names.empty() ? std::string{} : debug_names[i], instruction->source_location()));
    }

    function().attach_value_defs(*instruction, std::move(values));
    append_instruction(instruction);
    return instruction;
}

void LoweringContext::append_bound_assignment_from_ref(const std::string& name, ValueRef value_ref,
                                                       std::optional<SourceLocation> location) {
    bind_symbol_value(name, value_ref);
    append_instruction(create_instruction<AssignInstruction>(name, value_ref, std::move(location)));
}

void LoweringContext::append_bound_assignment(const std::string& name, Instruction* value,
                                              std::optional<SourceLocation> location) {
    append_bound_assignment_from_ref(name, value == nullptr ? ValueRef{} : value->value_ref(),
                                     std::move(location));
}

Instruction* LoweringContext::append_binding_instruction(const std::string& name,
                                                         std::optional<SourceLocation> location) {
    Instruction* instruction = create_instruction<BindingInstruction>(name, std::move(location));
    return append_valued_instruction(instruction, name);
}

UnaryOpInstruction* LoweringContext::create_unaryop_instruction(UnaryOpInstruction::Type op,
                                                                Instruction* operand,
                                                                std::optional<SourceLocation> location) {
    return create_unaryop_instruction(op, value_ref_from_instruction(operand), std::move(location));
}

UnaryOpInstruction* LoweringContext::create_unaryop_instruction(
    UnaryOpInstruction::Type op, ValueRef operand_ref, std::optional<SourceLocation> location) {
    return create_instruction<UnaryOpInstruction>(op, operand_ref, std::move(location));
}

BinOpInstruction* LoweringContext::create_binop_instruction(BinOpInstruction::Type op,
                                                            Instruction* lhs, Instruction* rhs,
                                                            std::optional<SourceLocation> location) {
    return create_binop_instruction(op, value_ref_from_instruction(lhs), value_ref_from_instruction(rhs),
                                    std::move(location));
}

BinOpInstruction* LoweringContext::create_binop_instruction(BinOpInstruction::Type op,
                                                            ValueRef lhs_ref, ValueRef rhs_ref,
                                                            std::optional<SourceLocation> location) {
    return create_instruction<BinOpInstruction>(op, lhs_ref, rhs_ref, std::move(location));
}

CallInstruction* LoweringContext::create_call_instruction(std::string name, std::size_t output_count,
                                                          std::vector<Instruction*> in_args,
                                                          std::optional<SourceLocation> location) {
    std::vector<ValueRef> in_arg_refs = collect_value_refs(in_args);
    return create_call_instruction(std::move(name), output_count, std::move(in_arg_refs),
                                   std::move(location));
}

CallInstruction* LoweringContext::create_call_instruction(std::string name, std::size_t output_count,
                                                          std::vector<ValueRef> in_arg_refs,
                                                          std::optional<SourceLocation> location) {
    return create_instruction<CallInstruction>(std::move(name), output_count, std::move(in_arg_refs),
                                               std::move(location));
}

CallInstruction* LoweringContext::create_call_instruction(ValueRef callee_ref,
                                                          std::size_t output_count,
                                                          std::vector<ValueRef> in_arg_refs,
                                                          std::optional<SourceLocation> location) {
    return create_instruction<CallInstruction>(callee_ref, output_count, std::move(in_arg_refs),
                                               std::move(location));
}

CondJumpInstruction* LoweringContext::create_cond_jump_instruction(
    Instruction* cond, BasicBlock* true_block, BasicBlock* false_block,
    std::optional<SourceLocation> location) {
    return create_cond_jump_instruction(value_ref_from_instruction(cond), true_block, false_block,
                                        std::move(location));
}

CondJumpInstruction* LoweringContext::create_cond_jump_instruction(
    ValueRef cond_ref, BasicBlock* true_block, BasicBlock* false_block,
    std::optional<SourceLocation> location) {
    return create_instruction<CondJumpInstruction>(cond_ref, true_block, false_block,
                                                   std::move(location));
}

ReturnInstruction* LoweringContext::create_return_instruction(std::vector<Instruction*> values,
                                                              std::optional<SourceLocation> location) {
    std::vector<ValueRef> value_refs = collect_value_refs(values);
    return create_return_instruction(std::move(value_refs), std::move(location));
}

ReturnInstruction* LoweringContext::create_return_instruction(std::vector<ValueRef> value_refs,
                                                              std::optional<SourceLocation> location) {
    return create_instruction<ReturnInstruction>(std::move(value_refs), std::move(location));
}

}  // namespace baltam
