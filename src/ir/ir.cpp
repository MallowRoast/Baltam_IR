#include "ir/ir.h"

#include <algorithm>
#include <sstream>
#include <type_traits>
#include <utility>

namespace baltam {
namespace {

bool contains_block(const std::vector<BasicBlock*>& blocks, const BasicBlock* target) {
    return std::find(blocks.begin(), blocks.end(), target) != blocks.end();
}

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
        case Module::Script:
            return "script";
        case Module::Function:
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

    if (instruction.source_span().has_value()) {
        const SourceSpan& span = *instruction.source_span();
        os << "    ; " << span.filename << ":" << span.begin_line << ":" << span.begin_column;
    }

    os << "\n";
}

}  // namespace

Instruction::Instruction(Type type, std::optional<SourceSpan> span)
    : type_(type), source_span_(std::move(span)) {}

Instruction::Type Instruction::type() const {
    return type_;
}

BasicBlock* Instruction::parent() const {
    return parent_;
}

const std::optional<SourceSpan>& Instruction::source_span() const {
    return source_span_;
}

void Instruction::set_parent(BasicBlock* block) {
    parent_ = block;
}

TextInstruction::TextInstruction(std::string text, std::optional<SourceSpan> span)
    : Instruction(Text, std::move(span)), text_(std::move(text)) {}

const std::string& TextInstruction::text() const {
    return text_;
}

NameInstruction::NameInstruction(std::string name, std::optional<SourceSpan> span)
    : Instruction(Name, std::move(span)), name_(std::move(name)) {}

const std::string& NameInstruction::name() const {
    return name_;
}

NumberInstruction::NumberInstruction(bool value, std::optional<SourceSpan> span)
    : Instruction(Number, std::move(span)), value_(value) {}

NumberInstruction::NumberInstruction(std::int64_t value, std::optional<SourceSpan> span)
    : Instruction(Number, std::move(span)), value_(value) {}

NumberInstruction::NumberInstruction(double value, std::optional<SourceSpan> span)
    : Instruction(Number, std::move(span)), value_(value) {}

NumberInstruction::NumberInstruction(std::complex<double> value, std::optional<SourceSpan> span)
    : Instruction(Number, std::move(span)), value_(std::move(value)) {}

const NumberInstruction::NumberValue& NumberInstruction::value() const {
    return value_;
}

BinOpInstruction::BinOpInstruction(Type op, Instruction* lhs, Instruction* rhs,
                                   std::optional<SourceSpan> span)
    : Instruction(Instruction::BinOp, std::move(span)), op_(op), lhs_(lhs), rhs_(rhs) {}

BinOpInstruction::Type BinOpInstruction::op() const {
    return op_;
}

Instruction* BinOpInstruction::lhs() const {
    return lhs_;
}

Instruction* BinOpInstruction::rhs() const {
    return rhs_;
}

AssignInstruction::AssignInstruction(std::string name, Instruction* value, std::optional<SourceSpan> span)
    : Instruction(Asgn, std::move(span)), name_(std::move(name)), value_(value) {}

const std::string& AssignInstruction::name() const {
    return name_;
}

Instruction* AssignInstruction::value() const {
    return value_;
}

CallInstruction::CallInstruction(std::string name, std::vector<Instruction*> out_args,
                                 std::vector<Instruction*> in_args, std::optional<SourceSpan> span)
    : Instruction(Call, std::move(span)),
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
                                         BasicBlock* false_block, std::optional<SourceSpan> span)
    : Instruction(Instruction::CondJump, std::move(span)),
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

JumpInstruction::JumpInstruction(BasicBlock* target, std::optional<SourceSpan> span)
    : Instruction(Jump, std::move(span)), target_(target) {}

BasicBlock* JumpInstruction::target() const {
    return target_;
}

ReturnInstruction::ReturnInstruction(std::optional<SourceSpan> span)
    : Instruction(Return, std::move(span)) {}

BasicBlock::BasicBlock(std::string name): name_(std::move(name)) {}

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

    if (!contains_block(successors_, successor)) {
        successors_.push_back(successor);
    }

    if (!contains_block(successor->predecessors_, this)) {
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

Function::Type Function::type() const {
    return type_;
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
    auto function = std::make_unique<::baltam::Function>(std::move(name), type);
    ::baltam::Function* raw = function.get();
    function_storage_.push_back(std::move(function));
    return raw;
}

void Module::set_entry_function(::baltam::Function* function) {
    entry_function_ = function;
}

Module build_demo(const std::string& source_path) {
    Module module("simple_demo", source_path, Module::Script);
    Function* function = module.create_function("__script_main__", Function::Script);
    module.set_entry_function(function);

    BasicBlock* entry = function->create_block("entry");
    BasicBlock* if_true = function->create_block("if_true");
    BasicBlock* if_false = function->create_block("if_false");
    BasicBlock* exit = function->create_block("exit");
    function->set_entry_block(entry);

    entry->add_successor(if_true);
    entry->add_successor(if_false);
    if_true->add_successor(exit);
    if_false->add_successor(exit);

    Instruction* one = function->create_instruction<NumberInstruction>(
        std::int64_t{1}, SourceSpan{source_path, 1, 5, 1, 5});
    Instruction* two = function->create_instruction<NumberInstruction>(
        std::int64_t{2}, SourceSpan{source_path, 1, 9, 1, 9});
    Instruction* add = function->create_instruction<BinOpInstruction>(
        BinOpInstruction::Type::Add, one, two, SourceSpan{source_path, 1, 5, 1, 9});
    entry->append_instruction(one);
    entry->append_instruction(two);
    entry->append_instruction(add);
    entry->append_instruction(function->create_instruction<AssignInstruction>(
        "a", add, SourceSpan{source_path, 1, 1, 1, 10}));

    Instruction* a_ref = function->create_instruction<NameInstruction>(
        "a", SourceSpan{source_path, 2, 9, 2, 9});
    Instruction* b_out = function->create_instruction<NameInstruction>(
        "b", SourceSpan{source_path, 2, 1, 2, 1});
    Instruction* sin_call = function->create_instruction<CallInstruction>(
        "sin", std::vector<Instruction*>{b_out}, std::vector<Instruction*>{a_ref},
        SourceSpan{source_path, 2, 1, 2, 10});
    entry->append_instruction(a_ref);
    entry->append_instruction(b_out);
    entry->append_instruction(sin_call);

    Instruction* b_ref_for_if = function->create_instruction<NameInstruction>(
        "b", SourceSpan{source_path, 4, 4, 4, 4});
    Instruction* zero = function->create_instruction<NumberInstruction>(
        std::int64_t{0}, SourceSpan{source_path, 4, 8, 4, 8});
    Instruction* gt = function->create_instruction<BinOpInstruction>(
        BinOpInstruction::Type::Gt, b_ref_for_if, zero, SourceSpan{source_path, 4, 4, 4, 8});
    entry->append_instruction(b_ref_for_if);
    entry->append_instruction(zero);
    entry->append_instruction(gt);
    entry->set_terminal(function->create_instruction<CondJumpInstruction>(
        gt, if_true, if_false, SourceSpan{source_path, 4, 1, 8, 4}));

    Instruction* b_ref_for_mul = function->create_instruction<NameInstruction>(
        "b", SourceSpan{source_path, 5, 9, 5, 9});
    Instruction* two_again = function->create_instruction<NumberInstruction>(
        std::int64_t{2}, SourceSpan{source_path, 5, 13, 5, 13});
    Instruction* mul = function->create_instruction<BinOpInstruction>(
        BinOpInstruction::Type::Multiply, b_ref_for_mul, two_again,
        SourceSpan{source_path, 5, 9, 5, 13});
    if_true->append_instruction(b_ref_for_mul);
    if_true->append_instruction(two_again);
    if_true->append_instruction(mul);
    if_true->append_instruction(function->create_instruction<AssignInstruction>(
        "c", mul, SourceSpan{source_path, 5, 5, 5, 14}));
    if_true->set_terminal(function->create_instruction<JumpInstruction>(
        exit, SourceSpan{source_path, 5, 5, 5, 14}));

    Instruction* zero_else = function->create_instruction<NumberInstruction>(
        std::int64_t{0}, SourceSpan{source_path, 7, 9, 7, 9});
    if_false->append_instruction(zero_else);
    if_false->append_instruction(function->create_instruction<AssignInstruction>(
        "c", zero_else, SourceSpan{source_path, 7, 5, 7, 10}));
    if_false->set_terminal(function->create_instruction<JumpInstruction>(
        exit, SourceSpan{source_path, 7, 5, 7, 10}));

    exit->set_terminal(function->create_instruction<ReturnInstruction>());

    return module;
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
