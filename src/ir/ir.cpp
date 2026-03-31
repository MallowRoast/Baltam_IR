#include "ir/ir.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <type_traits>
#include <utility>

namespace baltam {
namespace {

std::string format_double(double value) {
    std::ostringstream oss;
    oss << value;
    return oss.str();
}

std::string format_complex(const std::complex<double>& value) {
    std::ostringstream oss;
    oss << value.real();
    if (value.imag() >= 0) {
        oss << "+";
    }
    oss << value.imag() << "i";
    return oss.str();
}

std::string format_number(const NumberInstruction::NumberValue& value) {
    return std::visit(
        [](const auto& item) -> std::string {
            using T = std::decay_t<decltype(item)>;

            if constexpr (std::is_same_v<T, bool>) {
                return item ? "true" : "false";
            } else if constexpr (std::is_same_v<T, std::int64_t>) {
                return std::to_string(item);
            } else if constexpr (std::is_same_v<T, std::uint64_t>) {
                return std::to_string(item);
            } else if constexpr (std::is_same_v<T, double>) {
                return format_double(item);
            } else if constexpr (std::is_same_v<T, std::complex<double>>) {
                return format_complex(item);
            }

            return "<number>";
        },
        value);
}

std::string binop_symbol(BinOpInstruction::Type op) {
    switch (op) {
        case BinOpInstruction::Add:
            return "+";
        case BinOpInstruction::Subtract:
            return "-";
        case BinOpInstruction::Eq:
            return "==";
        case BinOpInstruction::Gt:
            return ">";
        case BinOpInstruction::Lt:
            return "<";
        case BinOpInstruction::Ne:
            return "~=";
        case BinOpInstruction::Or:
            return "|";
        case BinOpInstruction::MPower:
            return "^";
        case BinOpInstruction::Multiply:
            return "*";
    }

    return "?";
}

std::string unaryop_symbol(UnaryOpInstruction::Type op) {
    switch (op) {
        case UnaryOpInstruction::UMinus:
            return "-";
    }

    return "?";
}

const char* function_type_name(Function::Type type) {
    switch (type) {
        case Function::Script:
            return "script";
        case Function::PrimaryFunction:
            return "primary_function";
        case Function::LocalFunction:
            return "local_function";
    }

    return "unknown_function_type";
}

const char* module_type_name(Module::Type type) {
    switch (type) {
        case Module::M_Script:
            return "script";
        case Module::M_Function:
            return "function";
    }

    return "unknown_module_type";
}

std::string format_value_ref(const Instruction* context, ValueRef ref);
std::string format_inst_values(const std::vector<InstValue>& values);
std::string format_name_list(const std::vector<std::string>& names);
std::string format_block_list(const std::vector<BasicBlock*>& blocks);
std::string format_source_location(const std::optional<SourceLocation>& location);
std::size_t decimal_width(std::size_t value);

std::string expr_text(const Instruction* instruction) {
    if (instruction == nullptr) {
        return "<null>";
    }

    switch (instruction->type()) {
        case Instruction::Text:
            return "'" + static_cast<const TextInstruction*>(instruction)->text() + "'";
        case Instruction::Name:
            return "load " + static_cast<const NameInstruction*>(instruction)->name();
        case Instruction::Number: {
            const auto* number = static_cast<const NumberInstruction*>(instruction);
            return format_number(number->value());
        }
        case Instruction::UnaryOp: {
            const auto* unaryop = static_cast<const UnaryOpInstruction*>(instruction);
            const ValueRef operand_ref = unaryop->operand_ref();
            return "(" + unaryop_symbol(unaryop->op()) +
                   (operand_ref.is_valid() ? format_value_ref(instruction, operand_ref)
                                           : "<null>") +
                   ")";
        }
        case Instruction::BinOp: {
            const auto* binop = static_cast<const BinOpInstruction*>(instruction);
            const ValueRef lhs_ref = binop->lhs_ref();
            const ValueRef rhs_ref = binop->rhs_ref();
            const std::string lhs_text =
                lhs_ref.is_valid() ? format_value_ref(instruction, lhs_ref) : "<null>";
            const std::string rhs_text =
                rhs_ref.is_valid() ? format_value_ref(instruction, rhs_ref) : "<null>";
            return "(" + lhs_text + " " + binop_symbol(binop->op()) + " " + rhs_text + ")";
        }
        case Instruction::Phi: {
            const auto* phi = static_cast<const PhiInstruction*>(instruction);
            std::string text = "phi(";
            for (std::size_t i = 0; i < phi->incoming_count(); ++i) {
                if (i != 0) {
                    text += ", ";
                }
                const PhiIncoming* incoming = phi->incoming(i);
                if (incoming == nullptr) {
                    text += "<null>";
                    continue;
                }
                text += incoming->predecessor != nullptr ? incoming->predecessor->name() : "<entry>";
                text += " -> ";
                text += incoming->value_ref.is_valid() ? format_value_ref(instruction, incoming->value_ref)
                                                       : "<null>";
            }
            text += ")";
            return text;
        }
        case Instruction::Asgn: {
            const auto* asgn = static_cast<const AssignInstruction*>(instruction);
            const ValueRef value_ref = asgn->value_ref();
            return "store " + asgn->name() + " <- " +
                   (value_ref.is_valid() ? format_value_ref(instruction, value_ref) : "<null>");
        }
        case Instruction::Call: {
            const auto* call = static_cast<const CallInstruction*>(instruction);
            std::string text = "call " + call->name() + "(";
            const std::size_t in_arg_count = call->input_count();
            for (std::size_t i = 0; i < in_arg_count; ++i) {
                if (i != 0) {
                    text += ", ";
                }
                const ValueRef in_arg_ref = call->input_ref(i);
                text += in_arg_ref.is_valid() ? format_value_ref(instruction, in_arg_ref)
                                              : "<null>";
            }
            text += ")";
            return text;
        }
        case Instruction::CondJump: {
            const auto* cond_jump = static_cast<const CondJumpInstruction*>(instruction);
            const ValueRef cond_ref = cond_jump->cond_ref();
            return "br " +
                   (cond_ref.is_valid() ? format_value_ref(instruction, cond_ref) : "<null>") +
                   ", " + cond_jump->true_block()->name() + ", " + cond_jump->false_block()->name();
        }
        case Instruction::Jump: {
            const auto* jump = static_cast<const JumpInstruction*>(instruction);
            return "jmp " + jump->target()->name();
        }
        case Instruction::Return: {
            const auto* ret = static_cast<const ReturnInstruction*>(instruction);
            std::string text = "ret";
            const std::size_t value_count = ret->return_value_count();
            if (value_count != 0) {
                text += " ";
                for (std::size_t i = 0; i < value_count; ++i) {
                    if (i != 0) {
                        text += ", ";
                    }
                    const ValueRef value_ref = ret->return_value_ref(i);
                    text += value_ref.is_valid() ? format_value_ref(instruction, value_ref)
                                                 : "<null>";
                }
            }
            return text;
        }
    }

    return "<inst>";
}

std::string format_inst_value(const InstValue& value) {
    std::string text = "%";
    if (!value.debug_name.empty()) {
        text += value.debug_name;
        text += ".";
    }
    text += std::to_string(value.id);
    return text;
}

const Function* parent_function_from_instruction(const Instruction* instruction) {
    if (instruction == nullptr || instruction->parent() == nullptr) {
        return nullptr;
    }
    return instruction->parent()->parent();
}

std::string format_value_ref(const Instruction* context, ValueRef ref) {
    if (!ref.is_valid()) {
        return "<invalid>";
    }
    if (const Function* function = parent_function_from_instruction(context)) {
        if (const InstValue* value = function->find_value(ref.id)) {
            return format_inst_value(*value);
        }
    }
    return "%" + std::to_string(ref.id);
}

std::string format_inst_values(const std::vector<InstValue>& values) {
    std::ostringstream oss;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            oss << ", ";
        }
        oss << format_inst_value(values[i]);
    }
    return oss.str();
}

std::string format_name_list(const std::vector<std::string>& names) {
    std::ostringstream oss;
    oss << "(";
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (i != 0) {
            oss << ", ";
        }
        oss << names[i];
    }
    oss << ")";
    return oss.str();
}

std::string format_block_list(const std::vector<BasicBlock*>& blocks) {
    if (blocks.empty()) {
        return "-";
    }

    std::ostringstream oss;
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        if (i != 0) {
            oss << ", ";
        }
        oss << blocks[i]->name();
    }
    return oss.str();
}

std::string format_source_location(const std::optional<SourceLocation>& location) {
    if (!location.has_value()) {
        return {};
    }
    std::ostringstream oss;
    oss << "  @" << location->filename << ":" << location->begin_line << ":" << location->begin_column;
    return oss.str();
}

std::size_t decimal_width(std::size_t value) {
    std::size_t width = 1;
    while (value >= 10) {
        value /= 10;
        ++width;
    }
    return width;
}

void print_instruction(std::ostream& os, const Instruction& instruction, std::size_t index_width,
                       std::optional<std::size_t> index = std::nullopt, bool is_terminal = false) {
    os << "    ";
    if (is_terminal) {
        os << "T:";
    } else if (index.has_value()) {
        os << std::setw(static_cast<int>(index_width)) << *index << ":";
    } else {
        os << " :";
    }
    os << " ";

    if (instruction.has_values()) {
        os << format_inst_values(instruction.value_defs()) << " = ";
    }
    os << expr_text(&instruction);
    os << format_source_location(instruction.source_location());
    os << "\n";
}

}  // namespace

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

NameInstruction::NameInstruction(std::string name, std::optional<SourceLocation> location)
    : Instruction(Name, std::move(location)), name_(std::move(name)) {}

const std::string& NameInstruction::name() const {
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

PhiInstruction::PhiInstruction(std::vector<PhiIncoming> incomings,
                               std::optional<SourceLocation> location)
    : Instruction(Phi, std::move(location)), incomings_(std::move(incomings)) {}

const std::vector<PhiIncoming>& PhiInstruction::incomings() const {
    return incomings_;
}

std::size_t PhiInstruction::incoming_count() const {
    return incomings_.size();
}

const PhiIncoming* PhiInstruction::incoming(std::size_t index) const {
    return index < incomings_.size() ? &incomings_[index] : nullptr;
}

CallInstruction::CallInstruction(std::string name, std::size_t output_count,
                                 std::vector<ValueRef> in_arg_refs,
                                 std::optional<SourceLocation> location)
    : Instruction(Call, std::move(location)),
      name_(std::move(name)),
      output_count_(output_count),
      in_arg_refs_(std::move(in_arg_refs)) {}

const std::string& CallInstruction::name() const {
    return name_;
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
}

void Function::set_output_names(std::vector<std::string> names) {
    output_names_ = std::move(names);
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

    for (const std::unique_ptr<Instruction>& instruction : instruction_storage_) {
        for (const InstValue& value : instruction->value_defs()) {
            if (value.id == id) {
                return &value;
            }
        }
    }
    return nullptr;
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

void print_ir(std::ostream& os, const Module& module) {
    os << "module " << module.name() << " [" << module_type_name(module.type()) << "]\n";
    os << "  source: " << module.source_path() << "\n";
    os << "  entry : ";
    if (module.entry_function() != nullptr) {
        os << module.entry_function()->name();
    } else {
        os << "<null>";
    }
    os << "\n";

    for (const auto& function : module.functions()) {
        os << "\nfunction @" << function->name() << " "
           << format_name_list(function->input_names()) << " -> "
           << format_name_list(function->output_names())
           << " [" << function_type_name(function->type()) << "] {\n";
        os << "  entry: ";
        if (function->entry_block() != nullptr) {
            os << function->entry_block()->name();
        } else {
            os << "<null>";
        }
        os << "\n";
        for (const auto& block : function->blocks()) {
            os << "\n";
            os << "  block " << block->name()
               << "  [preds: " << format_block_list(block->predecessors())
               << "; succs: " << format_block_list(block->successors()) << "]\n";
            const std::size_t index_width = decimal_width(
                std::max<std::size_t>(block->instructions().size(), 1) - 1);
            for (std::size_t i = 0; i < block->instructions().size(); ++i) {
                print_instruction(os, *block->instructions()[i], index_width, i, false);
            }
            if (block->terminal() != nullptr) {
                print_instruction(os, *block->terminal(), index_width, std::nullopt, true);
            }
        }

        os << "\n";
        os << "}\n";
    }
}

}  // namespace baltam
