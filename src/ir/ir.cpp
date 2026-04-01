#include "ir/ir.h"

#include <algorithm>
#include <utility>

namespace baltam {

IRNode::IRNode(Stage stage, std::optional<SourceLocation> location)
    : stage_(stage), source_location_(std::move(location)) {}

IRNode::Stage IRNode::stage() const {
    return stage_;
}

BasicBlock* IRNode::parent() const {
    return parent_;
}

const std::optional<SourceLocation>& IRNode::source_location() const {
    return source_location_;
}

void IRNode::set_parent(BasicBlock* block) {
    parent_ = block;
}

NonSSANode::NonSSANode(Type type, std::optional<SourceLocation> location)
    : IRNode(IRNode::NonSSA, std::move(location)), type_(type) {}

NonSSANode::Type NonSSANode::type() const {
    return type_;
}

SSANode::SSANode(Stage stage, std::optional<SourceLocation> location)
    : IRNode(stage, std::move(location)) {}

NumberNode::NumberNode(NamedValue result, NumberValue value, std::optional<SourceLocation> location)
    : NonSSANode(NonSSANode::Number, std::move(location)),
      result_(std::move(result)),
      value_(std::move(value)) {}

const NamedValue& NumberNode::result() const {
    return result_;
}

const NumberNode::NumberValue& NumberNode::value() const {
    return value_;
}

TextNode::TextNode(NamedValue result, std::string text, std::optional<SourceLocation> location)
    : NonSSANode(NonSSANode::Text, std::move(location)),
      result_(std::move(result)),
      text_(std::move(text)) {}

const NamedValue& TextNode::result() const {
    return result_;
}

const std::string& TextNode::text() const {
    return text_;
}

AssignNode::AssignNode(NamedValue dst, NamedValue src, std::optional<SourceLocation> location)
    : NonSSANode(NonSSANode::Assign, std::move(location)),
      dst_(std::move(dst)),
      src_(std::move(src)) {}

const NamedValue& AssignNode::dst() const {
    return dst_;
}

const NamedValue& AssignNode::src() const {
    return src_;
}

UnaryOpNode::UnaryOpNode(Op op, NamedValue result, NamedValue operand,
                         std::optional<SourceLocation> location)
    : NonSSANode(NonSSANode::UnaryOp, std::move(location)),
      op_(op),
      result_(std::move(result)),
      operand_(std::move(operand)) {}

UnaryOpNode::Op UnaryOpNode::op() const {
    return op_;
}

const NamedValue& UnaryOpNode::result() const {
    return result_;
}

const NamedValue& UnaryOpNode::operand() const {
    return operand_;
}

BinOpNode::BinOpNode(Op op, NamedValue result, NamedValue lhs, NamedValue rhs,
                     std::optional<SourceLocation> location)
    : NonSSANode(NonSSANode::BinOp, std::move(location)),
      op_(op),
      result_(std::move(result)),
      lhs_(std::move(lhs)),
      rhs_(std::move(rhs)) {}

BinOpNode::Op BinOpNode::op() const {
    return op_;
}

const NamedValue& BinOpNode::result() const {
    return result_;
}

const NamedValue& BinOpNode::lhs() const {
    return lhs_;
}

const NamedValue& BinOpNode::rhs() const {
    return rhs_;
}

CallNode::CallNode(CalleeType callee_type, std::string callee, std::vector<NamedValue> outputs,
                   std::vector<NamedValue> inputs, std::optional<SourceLocation> location)
    : NonSSANode(NonSSANode::Call, std::move(location)),
      callee_type_(callee_type),
      callee_(std::move(callee)),
      outputs_(std::move(outputs)),
      inputs_(std::move(inputs)) {}

CallNode::CalleeType CallNode::callee_type() const {
    return callee_type_;
}

const std::string& CallNode::callee() const {
    return callee_;
}

const std::vector<NamedValue>& CallNode::outputs() const {
    return outputs_;
}

const std::vector<NamedValue>& CallNode::inputs() const {
    return inputs_;
}

CondJumpNode::CondJumpNode(NamedValue cond, BasicBlock* true_block, BasicBlock* false_block,
                           std::optional<SourceLocation> location)
    : NonSSANode(NonSSANode::CondJump, std::move(location)),
      cond_(std::move(cond)),
      true_block_(true_block),
      false_block_(false_block) {}

const NamedValue& CondJumpNode::cond() const {
    return cond_;
}

BasicBlock* CondJumpNode::true_block() const {
    return true_block_;
}

BasicBlock* CondJumpNode::false_block() const {
    return false_block_;
}

JumpNode::JumpNode(BasicBlock* target, std::optional<SourceLocation> location)
    : NonSSANode(NonSSANode::Jump, std::move(location)), target_(target) {}

BasicBlock* JumpNode::target() const {
    return target_;
}

ReturnNode::ReturnNode(std::vector<NamedValue> values, std::optional<SourceLocation> location)
    : NonSSANode(NonSSANode::Return, std::move(location)), values_(std::move(values)) {}

const std::vector<NamedValue>& ReturnNode::values() const {
    return values_;
}

BasicBlock::BasicBlock(std::string name) : name_(std::move(name)) {}

bool BasicBlock::contains_block(const std::vector<BasicBlock*>& blocks, const BasicBlock* target) {
    return std::find(blocks.begin(), blocks.end(), target) != blocks.end();
}

Function* BasicBlock::parent() const {
    return parent_;
}

const std::string& BasicBlock::name() const {
    return name_;
}

const std::vector<NonSSANode*>& BasicBlock::instructions() const {
    return instructions_;
}

const std::vector<BasicBlock*>& BasicBlock::predecessors() const {
    return predecessors_;
}

const std::vector<BasicBlock*>& BasicBlock::successors() const {
    return successors_;
}

NonSSANode* BasicBlock::terminal() const {
    return terminal_;
}

void BasicBlock::append_instruction(NonSSANode* node) {
    if (node == nullptr) {
        return;
    }
    node->set_parent(this);
    instructions_.push_back(node);
}

void BasicBlock::add_successor(BasicBlock* successor) {
    if (successor == nullptr || contains_block(successors_, successor)) {
        return;
    }
    successors_.push_back(successor);
    if (!contains_block(successor->predecessors_, this)) {
        successor->predecessors_.push_back(this);
    }
}

void BasicBlock::set_terminal(NonSSANode* node) {
    terminal_ = node;
    if (terminal_ != nullptr) {
        terminal_->set_parent(this);
    }
}

void BasicBlock::set_parent(Function* function) {
    parent_ = function;
}

Function::Function(std::string name, Type type) : name_(std::move(name)), type_(type) {}

const std::string& Function::name() const {
    return name_;
}

Module* Function::parent() const {
    return parent_;
}

Function::Type Function::type() const {
    return type_;
}

const std::vector<NamedValue>& Function::inputs() const {
    return inputs_;
}

const std::vector<NamedValue>& Function::outputs() const {
    return outputs_;
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
    inputs_.clear();
    inputs_.reserve(names.size());
    for (std::string& name : names) {
        inputs_.push_back(NamedValue{std::move(name), NamedValue::UserVariable});
    }
}

void Function::set_output_names(std::vector<std::string> names) {
    outputs_.clear();
    outputs_.reserve(names.size());
    for (std::string& name : names) {
        outputs_.push_back(NamedValue{std::move(name), NamedValue::UserVariable});
    }
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
