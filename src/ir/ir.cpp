#include "ir/ir.h"

#include <algorithm>
#include <utility>

namespace baltam {

bool ValueRef::is_valid() const {
    return id != InvalidValueId;
}

bool InstValue::is_valid() const {
    return id != InvalidValueId;
}

Instruction::Instruction(Type type, std::optional<SourceLocation> location)
    : type_(type), source_location_(std::move(location)) {}

Instruction::Type Instruction::type() const {
    return type_;
}

BasicBlock* Instruction::parent() const {
    return parent_;
}

const std::optional<SourceLocation>& Instruction::source_location() const {
    return source_location_;
}

const std::vector<InstValue>& Instruction::value_defs() const {
    return value_defs_;
}

std::size_t Instruction::value_count() const {
    return value_defs_.size();
}

const InstValue* Instruction::value_def(std::size_t index) const {
    if (index >= value_defs_.size()) {
        return nullptr;
    }
    return &value_defs_[index];
}

ValueRef Instruction::value_ref(std::size_t index) const {
    const InstValue* value = value_def(index);
    if (value == nullptr) {
        return {};
    }
    return ValueRef{value->id};
}

bool Instruction::has_values() const {
    return !value_defs_.empty();
}

void Instruction::set_parent(BasicBlock* block) {
    parent_ = block;
}

void Instruction::set_value_defs(std::vector<InstValue> values) {
    value_defs_ = std::move(values);
}

TextInstruction::TextInstruction(std::string text, std::optional<SourceLocation> location)
    : Instruction(Text, std::move(location)), text_(std::move(text)) {}

const std::string& TextInstruction::text() const {
    return text_;
}

BindingInstruction::BindingInstruction(std::string name, std::optional<SourceLocation> location)
    : Instruction(Binding, std::move(location)), name_(std::move(name)) {}

const std::string& BindingInstruction::name() const {
    return name_;
}

NumberInstruction::NumberInstruction(bool value, std::optional<SourceLocation> location)
    : Instruction(Number, std::move(location)), value_(value) {}

NumberInstruction::NumberInstruction(std::int64_t value, std::optional<SourceLocation> location)
    : Instruction(Number, std::move(location)), value_(value) {}

NumberInstruction::NumberInstruction(std::uint64_t value, std::optional<SourceLocation> location)
    : Instruction(Number, std::move(location)), value_(value) {}

NumberInstruction::NumberInstruction(double value, std::optional<SourceLocation> location)
    : Instruction(Number, std::move(location)), value_(value) {}

NumberInstruction::NumberInstruction(std::complex<double> value,
                                     std::optional<SourceLocation> location)
    : Instruction(Number, std::move(location)), value_(std::move(value)) {}

const NumberInstruction::NumberValue& NumberInstruction::value() const {
    return value_;
}

UndefInstruction::UndefInstruction(std::optional<SourceLocation> location)
    : Instruction(Undef, std::move(location)) {}

UnaryOpInstruction::UnaryOpInstruction(Type op, ValueRef operand_ref,
                                       std::optional<SourceLocation> location)
    : Instruction(Instruction::UnaryOp, std::move(location)), op_(op), operand_ref_(operand_ref) {}

UnaryOpInstruction::Type UnaryOpInstruction::op() const {
    return op_;
}

ValueRef UnaryOpInstruction::operand_ref() const {
    return operand_ref_;
}

BinOpInstruction::BinOpInstruction(Type op, ValueRef lhs_ref, ValueRef rhs_ref,
                                   std::optional<SourceLocation> location)
    : Instruction(Instruction::BinOp, std::move(location)),
      op_(op),
      lhs_ref_(lhs_ref),
      rhs_ref_(rhs_ref) {}

BinOpInstruction::Type BinOpInstruction::op() const {
    return op_;
}

ValueRef BinOpInstruction::lhs_ref() const {
    return lhs_ref_;
}

ValueRef BinOpInstruction::rhs_ref() const {
    return rhs_ref_;
}

AssignInstruction::AssignInstruction(std::string name, ValueRef value_ref,
                                     std::optional<SourceLocation> location)
    : Instruction(Asgn, std::move(location)), name_(std::move(name)), value_ref_(value_ref) {}

const std::string& AssignInstruction::name() const {
    return name_;
}

ValueRef AssignInstruction::value_ref() const {
    return value_ref_;
}

PhiInstruction::PhiInstruction(std::vector<Incoming> incomings,
                               std::optional<SourceLocation> location)
    : Instruction(Phi, std::move(location)), incomings_(std::move(incomings)) {}

const std::vector<PhiInstruction::Incoming>& PhiInstruction::incomings() const {
    return incomings_;
}

std::size_t PhiInstruction::incoming_count() const {
    return incomings_.size();
}

const PhiInstruction::Incoming* PhiInstruction::incoming(std::size_t index) const {
    return index < incomings_.size() ? &incomings_[index] : nullptr;
}

void PhiInstruction::append_incoming(Incoming incoming) {
    incomings_.push_back(std::move(incoming));
}

CallInstruction::CallInstruction(std::string name, std::size_t output_count,
                                 std::vector<ValueRef> in_arg_refs,
                                 std::optional<SourceLocation> location)
    : Instruction(Call, std::move(location)),
      name_(std::move(name)),
      output_count_(output_count),
      in_arg_refs_(std::move(in_arg_refs)) {}

CallInstruction::CallInstruction(ValueRef callee_ref, std::size_t output_count,
                                 std::vector<ValueRef> in_arg_refs,
                                 std::optional<SourceLocation> location)
    : Instruction(Call, std::move(location)),
      callee_ref_(callee_ref),
      output_count_(output_count),
      in_arg_refs_(std::move(in_arg_refs)) {}

const std::string& CallInstruction::name() const {
    return name_;
}

ValueRef CallInstruction::callee_ref() const {
    return callee_ref_;
}

bool CallInstruction::is_indirect() const {
    return callee_ref_.is_valid();
}

std::size_t CallInstruction::output_count() const {
    return output_count_;
}

std::size_t CallInstruction::input_count() const {
    return in_arg_refs_.size();
}

ValueRef CallInstruction::input_ref(std::size_t index) const {
    return index < in_arg_refs_.size() ? in_arg_refs_[index] : ValueRef{};
}

const std::vector<ValueRef>& CallInstruction::in_arg_refs() const {
    return in_arg_refs_;
}

CondJumpInstruction::CondJumpInstruction(ValueRef cond_ref, BasicBlock* true_block,
                                         BasicBlock* false_block,
                                         std::optional<SourceLocation> location)
    : Instruction(Instruction::CondJump, std::move(location)),
      true_block_(true_block),
      false_block_(false_block),
      cond_ref_(cond_ref) {}

BasicBlock* CondJumpInstruction::true_block() const {
    return true_block_;
}

BasicBlock* CondJumpInstruction::false_block() const {
    return false_block_;
}

ValueRef CondJumpInstruction::cond_ref() const {
    return cond_ref_;
}

JumpInstruction::JumpInstruction(BasicBlock* target, std::optional<SourceLocation> location)
    : Instruction(Jump, std::move(location)), target_(target) {}

BasicBlock* JumpInstruction::target() const {
    return target_;
}

ReturnInstruction::ReturnInstruction(std::vector<ValueRef> value_refs,
                                     std::optional<SourceLocation> location)
    : Instruction(Return, std::move(location)), value_refs_(std::move(value_refs)) {}

std::size_t ReturnInstruction::return_value_count() const {
    return value_refs_.size();
}

ValueRef ReturnInstruction::return_value_ref(std::size_t index) const {
    return index < value_refs_.size() ? value_refs_[index] : ValueRef{};
}

const std::vector<ValueRef>& ReturnInstruction::value_refs() const {
    return value_refs_;
}

BasicBlock::BasicBlock(std::string name): name_(std::move(name)) {}

bool BasicBlock::contains_block(const std::vector<BasicBlock*>& blocks, const BasicBlock* target) {
    return std::find(blocks.begin(), blocks.end(), target) != blocks.end();
}

Function* BasicBlock::parent() const {
    return parent_;
}

const std::string& BasicBlock::name() const {
    return name_;
}

const std::vector<Instruction*>& BasicBlock::instructions() const {
    return instructions_;
}

const std::vector<BasicBlock*>& BasicBlock::predecessors() const {
    return predecessors_;
}

const std::vector<BasicBlock*>& BasicBlock::successors() const {
    return successors_;
}

Instruction* BasicBlock::terminal() const {
    return terminal_;
}

void BasicBlock::append_instruction(Instruction* instruction) {
    if (instruction == nullptr) {
        return;
    }

    instruction->set_parent(this);
    instructions_.push_back(instruction);
}

void BasicBlock::add_successor(BasicBlock* successor) {
    if (successor == nullptr) {
        return;
    }

    if (!BasicBlock::contains_block(successors_, successor)) {
        successors_.push_back(successor);
    }

    if (!BasicBlock::contains_block(successor->predecessors_, this)) {
        successor->predecessors_.push_back(this);
    }
}

void BasicBlock::set_terminal(Instruction* instruction) {
    if (instruction == nullptr) {
        terminal_ = nullptr;
        return;
    }

    instruction->set_parent(this);
    terminal_ = instruction;
}

void BasicBlock::set_parent(Function* function) {
    parent_ = function;
}

Function::Function(std::string name, Type type)
    : name_(std::move(name)), type_(type) {}

const std::string& Function::name() const {
    return name_;
}

Module* Function::parent() const {
    return parent_;
}

Function::Type Function::type() const {
    return type_;
}

const std::vector<std::string>& Function::input_names() const {
    return input_names_;
}

const std::vector<InstValue>& Function::input_values() const {
    return input_values_;
}

const std::vector<std::string>& Function::output_names() const {
    return output_names_;
}

BasicBlock* Function::entry_block() const {
    return entry_block_;
}

const std::vector<std::unique_ptr<BasicBlock>>& Function::blocks() const {
    return block_storage_;
}

BasicBlock* Function::create_block(std::string name) {
    auto block = std::make_unique<BasicBlock>(std::move(name));
    BasicBlock* raw = block.get();
    raw->set_parent(this);
    block_storage_.push_back(std::move(block));
    return raw;
}

void Function::set_entry_block(BasicBlock* block) {
    entry_block_ = block;
}

void Function::set_input_names(std::vector<std::string> names) {
    input_names_ = std::move(names);
    input_values_.clear();
    input_values_.reserve(input_names_.size());
    for (const std::string& input_name : input_names_) {
        input_values_.push_back(create_value(input_name));
    }
}

void Function::set_output_names(std::vector<std::string> names) {
    output_names_ = std::move(names);
}

ValueRef Function::input_ref(std::size_t index) const {
    return index < input_values_.size() ? ValueRef{input_values_[index].id} : ValueRef{};
}

InstValue Function::create_value(std::string debug_name,
                                 std::optional<SourceLocation> location) {
    InstValue value;
    value.id = next_value_id_++;
    value.debug_name = std::move(debug_name);
    value.source_location = std::move(location);
    return value;
}

void Function::attach_value_defs(Instruction& instruction, std::vector<InstValue> values) {
    instruction.set_value_defs(std::move(values));
}

InstValue Function::attach_single_value(Instruction& instruction, std::string debug_name,
                                        std::optional<SourceLocation> location) {
    InstValue value = create_value(std::move(debug_name), std::move(location));
    std::vector<InstValue> values;
    values.push_back(value);
    instruction.set_value_defs(std::move(values));
    return value;
}

const InstValue* Function::find_value(ValueId id) const {
    if (id == InvalidValueId) {
        return nullptr;
    }

    for (const InstValue& value : input_values_) {
        if (value.id == id) {
            return &value;
        }
    }

    for (const std::unique_ptr<Instruction>& instruction : instruction_storage_) {
        for (const InstValue& value : instruction->value_defs()) {
            if (value.id == id) {
                return &value;
            }
        }
    }
    return nullptr;
}

bool Function::set_value_debug_name(ValueId id, std::string debug_name) {
    if (id == InvalidValueId || debug_name.empty()) {
        return false;
    }

    for (InstValue& value : input_values_) {
        if (value.id == id) {
            value.debug_name = std::move(debug_name);
            return true;
        }
    }

    for (const std::unique_ptr<Instruction>& instruction : instruction_storage_) {
        std::vector<InstValue> values = instruction->value_defs();
        for (InstValue& value : values) {
            if (value.id == id) {
                value.debug_name = std::move(debug_name);
                instruction->set_value_defs(std::move(values));
                return true;
            }
        }
    }

    return false;
}

Instruction* Function::find_value_owner(ValueId id) {
    if (id == InvalidValueId) {
        return nullptr;
    }

    for (const std::unique_ptr<Instruction>& instruction : instruction_storage_) {
        for (const InstValue& value : instruction->value_defs()) {
            if (value.id == id) {
                return instruction.get();
            }
        }
    }
    return nullptr;
}

const Instruction* Function::find_value_owner(ValueId id) const {
    return const_cast<Function*>(this)->find_value_owner(id);
}

void Function::set_parent(Module* module) {
    parent_ = module;
}

Module::Module(std::string name, std::string source_path, Type type)
    : name_(std::move(name)), source_path_(std::move(source_path)), type_(type) {}

const std::string& Module::name() const {
    return name_;
}

Module::Type Module::type() const {
    return type_;
}

const std::string& Module::source_path() const {
    return source_path_;
}

Function* Module::entry_function() const {
    return entry_function_;
}

const std::vector<std::unique_ptr<Function>>& Module::functions() const {
    return function_storage_;
}

Function* Module::create_function(std::string name, Function::Type type) {
    auto function = std::make_unique<Function>(std::move(name), type);
    Function* raw = function.get();
    raw->set_parent(this);
    function_storage_.push_back(std::move(function));
    return raw;
}

void Module::set_entry_function(Function* function) {
    entry_function_ = function;
}

}  // namespace baltam
