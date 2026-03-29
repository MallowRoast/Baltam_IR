#include "ir/ir.h"

#include <algorithm>
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
        case BinOpInstruction::Type::Add:
            return "+";
        case BinOpInstruction::Type::Gt:
            return ">";
        case BinOpInstruction::Type::Multiply:
            return "*";
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

std::string expr_text(const Instruction* instruction) {
    if (instruction == nullptr) {
        return "<null>";
    }

    switch (instruction->type()) {
        case Instruction::Text:
            return static_cast<const TextInstruction*>(instruction)->text();
        case Instruction::Name:
            return static_cast<const NameInstruction*>(instruction)->name();
        case Instruction::Number: {
            const auto* number = static_cast<const NumberInstruction*>(instruction);
            return format_number(number->value());
        }
        case Instruction::BinOp: {
            const auto* binop = static_cast<const BinOpInstruction*>(instruction);
            return "(" + expr_text(binop->lhs()) + " " + binop_symbol(binop->op()) + " " +
                   expr_text(binop->rhs()) + ")";
        }
        case Instruction::Asgn: {
            const auto* asgn = static_cast<const AssignInstruction*>(instruction);
            return asgn->name() + " = " + expr_text(asgn->value());
        }
        case Instruction::Call: {
            const auto* call = static_cast<const CallInstruction*>(instruction);
            std::string text;
            if (!call->out_args().empty()) {
                text += "[";
                for (std::size_t i = 0; i < call->out_args().size(); ++i) {
                    if (i != 0) {
                        text += ", ";
                    }
                    text += expr_text(call->out_args()[i]);
                }
                text += "] = ";
            }

            text += call->name() + "(";
            for (std::size_t i = 0; i < call->in_args().size(); ++i) {
                if (i != 0) {
                    text += ", ";
                }
                text += expr_text(call->in_args()[i]);
            }
            text += ")";
            return text;
        }
        case Instruction::CondJump: {
            const auto* cond_jump = static_cast<const CondJumpInstruction*>(instruction);
            return "cond_jump " + expr_text(cond_jump->cond()) + " ? " +
                   cond_jump->true_block()->name() + " : " + cond_jump->false_block()->name();
        }
        case Instruction::Jump: {
            const auto* jump = static_cast<const JumpInstruction*>(instruction);
            return "jump " + jump->target()->name();
        }
        case Instruction::Return: {
            return "return";
        }
    }

    return "<inst>";
}

void print_instruction(std::ostream& os, const Instruction& instruction) {
    os << "    " << expr_text(&instruction);

    if (instruction.source_location().has_value()) {
        const SourceLocation& location = *instruction.source_location();
        os << "    ; " << location.filename << ":" << location.begin_line << ":"
           << location.begin_column;
    }

    os << "\n";
}

}  // namespace

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

void Instruction::set_parent(BasicBlock* block) {
    parent_ = block;
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

BinOpInstruction::BinOpInstruction(Type op, Instruction* lhs, Instruction* rhs,
                                   std::optional<SourceLocation> location)
    : Instruction(Instruction::BinOp, std::move(location)), op_(op), lhs_(lhs), rhs_(rhs) {}

BinOpInstruction::Type BinOpInstruction::op() const {
    return op_;
}

Instruction* BinOpInstruction::lhs() const {
    return lhs_;
}

Instruction* BinOpInstruction::rhs() const {
    return rhs_;
}

AssignInstruction::AssignInstruction(std::string name, Instruction* value,
                                     std::optional<SourceLocation> location)
    : Instruction(Asgn, std::move(location)), name_(std::move(name)), value_(value) {}

const std::string& AssignInstruction::name() const {
    return name_;
}

Instruction* AssignInstruction::value() const {
    return value_;
}

CallInstruction::CallInstruction(std::string name, std::vector<Instruction*> out_args,
                                 std::vector<Instruction*> in_args,
                                 std::optional<SourceLocation> location)
    : Instruction(Call, std::move(location)),
      name_(std::move(name)),
      out_args_(std::move(out_args)),
      in_args_(std::move(in_args)) {}

const std::string& CallInstruction::name() const {
    return name_;
}

const std::vector<Instruction*>& CallInstruction::out_args() const {
    return out_args_;
}

const std::vector<Instruction*>& CallInstruction::in_args() const {
    return in_args_;
}

CondJumpInstruction::CondJumpInstruction(Instruction* cond, BasicBlock* true_block,
                                         BasicBlock* false_block,
                                         std::optional<SourceLocation> location)
    : Instruction(Instruction::CondJump, std::move(location)),
      cond_(cond),
      true_block_(true_block),
      false_block_(false_block) {}

Instruction* CondJumpInstruction::cond() const {
    return cond_;
}

BasicBlock* CondJumpInstruction::true_block() const {
    return true_block_;
}

BasicBlock* CondJumpInstruction::false_block() const {
    return false_block_;
}

JumpInstruction::JumpInstruction(BasicBlock* target, std::optional<SourceLocation> location)
    : Instruction(Jump, std::move(location)), target_(target) {}

BasicBlock* JumpInstruction::target() const {
    return target_;
}

ReturnInstruction::ReturnInstruction(std::optional<SourceLocation> location)
    : Instruction(Return, std::move(location)) {}

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
    os << "module " << module.name() << "\n";
    os << "  type: " << module_type_name(module.type()) << "\n";
    os << "  source: " << module.source_path() << "\n";
    os << "  entry_function: ";
    if (module.entry_function() != nullptr) {
        os << module.entry_function()->name();
    } else {
        os << "<null>";
    }
    os << "\n";

    for (const auto& function : module.functions()) {
        os << "\nfunction @" << function->name() << " {\n";
        os << "  type: " << function_type_name(function->type()) << "\n";
        os << "  entry: ";
        if (function->entry_block() != nullptr) {
            os << function->entry_block()->name();
        } else {
            os << "<null>";
        }
        os << "\n";

        os << "  blocks:\n";
        for (const auto& block : function->blocks()) {
            os << "    " << block->name() << " preds=[";
            for (std::size_t i = 0; i < block->predecessors().size(); ++i) {
                if (i != 0) {
                    os << ", ";
                }
                os << block->predecessors()[i]->name();
            }
            os << "] succs=[";
            for (std::size_t i = 0; i < block->successors().size(); ++i) {
                if (i != 0) {
                    os << ", ";
                }
                os << block->successors()[i]->name();
            }
            os << "]\n";
        }

        os << "\n";
        for (const auto& block : function->blocks()) {
            os << "  block " << block->name() << ":\n";
            for (const Instruction* instruction : block->instructions()) {
                print_instruction(os, *instruction);
            }
            if (block->terminal() != nullptr) {
                os << "    terminal:\n";
                print_instruction(os, *block->terminal());
            }
            os << "\n";
        }

        os << "}\n";
    }
}

}  // namespace baltam
