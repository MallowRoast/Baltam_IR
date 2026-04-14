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

NumberNode::NumberNode(NamedValue result, bool value, std::optional<SourceLocation> location)
    : NumberNode(std::move(result), NumberValue{value}, std::move(location)) {}

NumberNode::NumberNode(NamedValue result, std::int8_t value,
                       std::optional<SourceLocation> location)
    : NumberNode(std::move(result), IntegerConstant(value), std::move(location)) {}

NumberNode::NumberNode(NamedValue result, std::int16_t value,
                       std::optional<SourceLocation> location)
    : NumberNode(std::move(result), IntegerConstant(value), std::move(location)) {}

NumberNode::NumberNode(NamedValue result, std::int32_t value,
                       std::optional<SourceLocation> location)
    : NumberNode(std::move(result), IntegerConstant(value), std::move(location)) {}

NumberNode::NumberNode(NamedValue result, std::int64_t value,
                       std::optional<SourceLocation> location)
    : NumberNode(std::move(result), IntegerConstant(value), std::move(location)) {}

NumberNode::NumberNode(NamedValue result, std::uint8_t value,
                       std::optional<SourceLocation> location)
    : NumberNode(std::move(result), IntegerConstant(value), std::move(location)) {}

NumberNode::NumberNode(NamedValue result, std::uint16_t value,
                       std::optional<SourceLocation> location)
    : NumberNode(std::move(result), IntegerConstant(value), std::move(location)) {}

NumberNode::NumberNode(NamedValue result, std::uint32_t value,
                       std::optional<SourceLocation> location)
    : NumberNode(std::move(result), IntegerConstant(value), std::move(location)) {}

NumberNode::NumberNode(NamedValue result, std::uint64_t value,
                       std::optional<SourceLocation> location)
    : NumberNode(std::move(result), IntegerConstant(value), std::move(location)) {}

NumberNode::NumberNode(NamedValue result, double value, std::optional<SourceLocation> location)
    : NumberNode(std::move(result), NumberValue{value}, std::move(location)) {}

NumberNode::NumberNode(NamedValue result, std::complex<double> value,
                       std::optional<SourceLocation> location)
    : NumberNode(std::move(result), NumberValue{std::move(value)}, std::move(location)) {}

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

GlobalLoadNode::GlobalLoadNode(NamedValue result, std::string symbol,
                               std::optional<SourceLocation> location)
    : NonSSANode(NonSSANode::GlobalLoad, std::move(location)),
      result_(std::move(result)),
      symbol_(std::move(symbol)) {}

const NamedValue& GlobalLoadNode::result() const {
    return result_;
}

const std::string& GlobalLoadNode::symbol() const {
    return symbol_;
}

GlobalStoreNode::GlobalStoreNode(std::string symbol, NamedValue value,
                                 std::optional<SourceLocation> location)
    : NonSSANode(NonSSANode::GlobalStore, std::move(location)),
      symbol_(std::move(symbol)),
      value_(std::move(value)) {}

const std::string& GlobalStoreNode::symbol() const {
    return symbol_;
}

const NamedValue& GlobalStoreNode::value() const {
    return value_;
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

bool ValueRef::valid() const {
    return id != InvalidValueId;
}

Function* BasicBlock::parent() const {
    return parent_;
}

const std::string& BasicBlock::name() const {
    return name_;
}

const std::vector<IRNode*>& BasicBlock::phi_nodes() const {
    return phi_nodes_;
}

const std::vector<IRNode*>& BasicBlock::instructions() const {
    return instructions_;
}

const std::vector<BasicBlock*>& BasicBlock::predecessors() const {
    return predecessors_;
}

const std::vector<BasicBlock*>& BasicBlock::successors() const {
    return successors_;
}

IRNode* BasicBlock::terminal() const {
    return terminal_;
}

void BasicBlock::append_phi(IRNode* node) {
    if (node == nullptr) {
        return;
    }
    node->set_parent(this);
    phi_nodes_.push_back(node);
}

bool BasicBlock::erase_phi(IRNode* node) {
    if (node == nullptr) {
        return false;
    }

    auto it = std::find(phi_nodes_.begin(), phi_nodes_.end(), node);
    if (it == phi_nodes_.end()) {
        return false;
    }

    node->set_parent(nullptr);
    phi_nodes_.erase(it);
    return true;
}

void BasicBlock::prepend_instruction(IRNode* node) {
    if (node == nullptr) {
        return;
    }
    node->set_parent(this);
    instructions_.insert(instructions_.begin(), node);
}

void BasicBlock::append_instruction(IRNode* node) {
    if (node == nullptr) {
        return;
    }
    node->set_parent(this);
    instructions_.push_back(node);
}

std::vector<IRNode*> BasicBlock::release_instructions() {
    std::vector<IRNode*> released = std::move(instructions_);
    instructions_.clear();
    for (IRNode* node : released) {
        if (node != nullptr) {
            node->set_parent(nullptr);
        }
    }
    return released;
}

bool BasicBlock::erase_instruction(IRNode* node) {
    if (node == nullptr) {
        return false;
    }

    auto it = std::find(instructions_.begin(), instructions_.end(), node);
    if (it == instructions_.end()) {
        return false;
    }

    node->set_parent(nullptr);
    instructions_.erase(it);
    return true;
}

bool BasicBlock::replace_instruction(IRNode* old_node, IRNode* new_node) {
    if (old_node == nullptr || new_node == nullptr) {
        return false;
    }

    auto it = std::find(instructions_.begin(), instructions_.end(), old_node);
    if (it == instructions_.end()) {
        return false;
    }

    new_node->set_parent(this);
    old_node->set_parent(nullptr);
    *it = new_node;
    return true;
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

bool BasicBlock::remove_successor(BasicBlock* successor) {
    if (successor == nullptr) {
        return false;
    }

    auto successor_it = std::find(successors_.begin(), successors_.end(), successor);
    if (successor_it == successors_.end()) {
        return false;
    }
    successors_.erase(successor_it);

    auto predecessor_it = std::find(successor->predecessors_.begin(),
                                    successor->predecessors_.end(), this);
    if (predecessor_it != successor->predecessors_.end()) {
        successor->predecessors_.erase(predecessor_it);
    }
    return true;
}

IRNode* BasicBlock::release_terminal() {
    IRNode* released = terminal_;
    terminal_ = nullptr;
    if (released != nullptr) {
        released->set_parent(nullptr);
    }
    return released;
}

void BasicBlock::set_terminal(IRNode* node) {
    if (terminal_ != nullptr) {
        terminal_->set_parent(nullptr);
    }
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

IRNode::Stage Function::stage() const {
    return stage_;
}

const std::vector<NamedValue>& Function::inputs() const {
    return inputs_;
}

const std::vector<NamedValue>& Function::outputs() const {
    return outputs_;
}

const std::vector<ValueId>& Function::argument_values() const {
    return argument_values_;
}

bool Function::has_varargin() const {
    return has_varargin_;
}

bool Function::has_varargout() const {
    return has_varargout_;
}

std::size_t Function::fixed_input_count() const {
    return inputs_.size() - ((has_varargin_ && !inputs_.empty()) ? 1u : 0u);
}

std::size_t Function::fixed_output_count() const {
    return outputs_.size() - ((has_varargout_ && !outputs_.empty()) ? 1u : 0u);
}

std::size_t Function::value_count() const {
    return value_debug_names_.size();
}

bool Function::has_value(ValueId id) const {
    if (id == InvalidValueId) {
        return false;
    }

    const std::size_t index = static_cast<std::size_t>(id - 1);
    return index < value_debug_names_.size() && value_debug_names_[index].has_value();
}

bool Function::is_user_visible_value(ValueId id) const {
    if (!has_value(id)) {
        return false;
    }

    const std::size_t index = static_cast<std::size_t>(id - 1);
    return index < value_user_visible_flags_.size() && value_user_visible_flags_[index];
}

const std::string* Function::find_value_debug_name(ValueId id) const {
    if (!has_value(id)) {
        return nullptr;
    }

    const std::size_t index = static_cast<std::size_t>(id - 1);
    return &value_debug_names_[index].value();
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

bool Function::erase_block(BasicBlock* block) {
    if (block == nullptr) {
        return false;
    }

    auto block_it = std::find_if(block_storage_.begin(), block_storage_.end(),
                                 [block](const std::unique_ptr<BasicBlock>& item) {
                                     return item.get() == block;
                                 });
    if (block_it == block_storage_.end()) {
        return false;
    }

    while (!block->successors_.empty()) {
        block->remove_successor(block->successors_.back());
    }
    while (!block->predecessors_.empty()) {
        block->predecessors_.back()->remove_successor(block);
    }

    while (!block->phi_nodes_.empty()) {
        block->erase_phi(block->phi_nodes_.back());
    }
    while (!block->instructions_.empty()) {
        block->erase_instruction(block->instructions_.back());
    }
    block->set_terminal(nullptr);
    block->set_parent(nullptr);

    if (entry_block_ == block) {
        entry_block_ = nullptr;
    }

    block_storage_.erase(block_it);
    return true;
}

void Function::set_entry_block(BasicBlock* block) {
    entry_block_ = block;
}

void Function::set_stage(IRNode::Stage stage) {
    stage_ = stage;
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

void Function::set_has_varargin(bool has_varargin) {
    has_varargin_ = has_varargin;
}

void Function::set_has_varargout(bool has_varargout) {
    has_varargout_ = has_varargout;
}

void Function::set_argument_values(std::vector<ValueId> argument_values) {
    argument_values_ = std::move(argument_values);
}

ValueId Function::create_value(std::string debug_name, bool is_user_visible) {
    const ValueId id = static_cast<ValueId>(value_debug_names_.size() + 1);
    value_debug_names_.push_back(std::move(debug_name));
    value_user_visible_flags_.push_back(is_user_visible);
    return id;
}

bool Function::erase_value(ValueId id) {
    if (!has_value(id)) {
        return false;
    }

    if (std::find(argument_values_.begin(), argument_values_.end(), id) != argument_values_.end()) {
        return false;
    }

    const std::size_t index = static_cast<std::size_t>(id - 1);
    value_debug_names_[index] = std::nullopt;
    value_user_visible_flags_[index] = false;
    return true;
}

void Function::set_value_debug_name(ValueId id, std::string debug_name) {
    const std::size_t index = static_cast<std::size_t>(id - 1);
    if (id == InvalidValueId || index >= value_debug_names_.size() ||
        !value_debug_names_[index].has_value()) {
        return;
    }
    value_debug_names_[index] = std::move(debug_name);
}

void Function::set_parent(Module* module) {
    parent_ = module;
}

UntypedSSANode::UntypedSSANode(Type type, std::optional<SourceLocation> location)
    : SSANode(IRNode::UntypedSSA, std::move(location)), type_(type) {}

UntypedSSANode::Type UntypedSSANode::type() const {
    return type_;
}

SSANumberNode::SSANumberNode(ValueId result, NumberValue value,
                             std::optional<SourceLocation> location)
    : UntypedSSANode(UntypedSSANode::SSA_Number, std::move(location)),
      result_(result),
      value_(std::move(value)) {}

SSANumberNode::SSANumberNode(ValueId result, bool value, std::optional<SourceLocation> location)
    : SSANumberNode(result, NumberValue{value}, std::move(location)) {}

SSANumberNode::SSANumberNode(ValueId result, std::int8_t value,
                             std::optional<SourceLocation> location)
    : SSANumberNode(result, IntegerConstant(value), std::move(location)) {}

SSANumberNode::SSANumberNode(ValueId result, std::int16_t value,
                             std::optional<SourceLocation> location)
    : SSANumberNode(result, IntegerConstant(value), std::move(location)) {}

SSANumberNode::SSANumberNode(ValueId result, std::int32_t value,
                             std::optional<SourceLocation> location)
    : SSANumberNode(result, IntegerConstant(value), std::move(location)) {}

SSANumberNode::SSANumberNode(ValueId result, std::int64_t value,
                             std::optional<SourceLocation> location)
    : SSANumberNode(result, IntegerConstant(value), std::move(location)) {}

SSANumberNode::SSANumberNode(ValueId result, std::uint8_t value,
                             std::optional<SourceLocation> location)
    : SSANumberNode(result, IntegerConstant(value), std::move(location)) {}

SSANumberNode::SSANumberNode(ValueId result, std::uint16_t value,
                             std::optional<SourceLocation> location)
    : SSANumberNode(result, IntegerConstant(value), std::move(location)) {}

SSANumberNode::SSANumberNode(ValueId result, std::uint32_t value,
                             std::optional<SourceLocation> location)
    : SSANumberNode(result, IntegerConstant(value), std::move(location)) {}

SSANumberNode::SSANumberNode(ValueId result, std::uint64_t value,
                             std::optional<SourceLocation> location)
    : SSANumberNode(result, IntegerConstant(value), std::move(location)) {}

SSANumberNode::SSANumberNode(ValueId result, double value, std::optional<SourceLocation> location)
    : SSANumberNode(result, NumberValue{value}, std::move(location)) {}

SSANumberNode::SSANumberNode(ValueId result, std::complex<double> value,
                             std::optional<SourceLocation> location)
    : SSANumberNode(result, NumberValue{std::move(value)}, std::move(location)) {}

ValueId SSANumberNode::result() const {
    return result_;
}

const SSANumberNode::NumberValue& SSANumberNode::value() const {
    return value_;
}

SSATextNode::SSATextNode(ValueId result, std::string text, std::optional<SourceLocation> location)
    : UntypedSSANode(UntypedSSANode::SSA_Text, std::move(location)),
      result_(result),
      text_(std::move(text)) {}

ValueId SSATextNode::result() const {
    return result_;
}

const std::string& SSATextNode::text() const {
    return text_;
}

SSAUndefNode::SSAUndefNode(ValueId result, std::optional<SourceLocation> location)
    : UntypedSSANode(UntypedSSANode::SSA_Undef, std::move(location)), result_(result) {}

ValueId SSAUndefNode::result() const {
    return result_;
}

SSAPhiNode::SSAPhiNode(ValueId result, std::vector<Incoming> incomings,
                       std::optional<SourceLocation> location)
    : UntypedSSANode(UntypedSSANode::SSA_Phi, std::move(location)),
      result_(result),
      incomings_(std::move(incomings)) {}

ValueId SSAPhiNode::result() const {
    return result_;
}

const std::vector<SSAPhiNode::Incoming>& SSAPhiNode::incomings() const {
    return incomings_;
}

void SSAPhiNode::add_incoming(BasicBlock* predecessor, ValueRef value) {
    incomings_.push_back(Incoming{predecessor, value});
}

bool SSAPhiNode::remove_incoming(BasicBlock* predecessor) {
    if (predecessor == nullptr) {
        return false;
    }

    const auto old_size = incomings_.size();
    incomings_.erase(std::remove_if(incomings_.begin(), incomings_.end(),
                                    [predecessor](const Incoming& incoming) {
                                        return incoming.predecessor == predecessor;
                                    }),
                     incomings_.end());
    return incomings_.size() != old_size;
}

bool SSAPhiNode::replace_predecessor(BasicBlock* old_predecessor, BasicBlock* new_predecessor) {
    if (old_predecessor == nullptr || new_predecessor == nullptr) {
        return false;
    }

    bool changed = false;
    for (Incoming& incoming : incomings_) {
        if (incoming.predecessor == old_predecessor) {
            incoming.predecessor = new_predecessor;
            changed = true;
        }
    }
    return changed;
}

bool SSAPhiNode::replace_value(ValueRef old_value, ValueRef new_value) {
    if (!old_value.valid() || !new_value.valid() || old_value.id == new_value.id) {
        return false;
    }

    bool changed = false;
    for (Incoming& incoming : incomings_) {
        if (incoming.value.id == old_value.id) {
            incoming.value = new_value;
            changed = true;
        }
    }
    return changed;
}

SSACopyNode::SSACopyNode(ValueId result, ValueRef src, std::optional<SourceLocation> location)
    : UntypedSSANode(UntypedSSANode::SSA_Copy, std::move(location)),
      result_(result),
      src_(src) {}

ValueId SSACopyNode::result() const {
    return result_;
}

ValueRef SSACopyNode::src() const {
    return src_;
}

void SSACopyNode::set_src(ValueRef src) {
    src_ = src;
}

SSAGlobalLoadNode::SSAGlobalLoadNode(ValueId result, std::string symbol,
                                     std::optional<SourceLocation> location)
    : UntypedSSANode(UntypedSSANode::SSA_GlobalLoad, std::move(location)),
      result_(result),
      symbol_(std::move(symbol)) {}

ValueId SSAGlobalLoadNode::result() const {
    return result_;
}

const std::string& SSAGlobalLoadNode::symbol() const {
    return symbol_;
}

SSAGlobalStoreNode::SSAGlobalStoreNode(std::string symbol, ValueRef value,
                                       std::optional<SourceLocation> location)
    : UntypedSSANode(UntypedSSANode::SSA_GlobalStore, std::move(location)),
      symbol_(std::move(symbol)),
      value_(value) {}

const std::string& SSAGlobalStoreNode::symbol() const {
    return symbol_;
}

ValueRef SSAGlobalStoreNode::value() const {
    return value_;
}

void SSAGlobalStoreNode::set_value(ValueRef value) {
    value_ = value;
}

SSAUnaryOpNode::SSAUnaryOpNode(Op op, ValueId result, ValueRef operand,
                               std::optional<SourceLocation> location)
    : UntypedSSANode(UntypedSSANode::SSA_UnaryOp, std::move(location)),
      op_(op),
      result_(result),
      operand_(operand) {}

SSAUnaryOpNode::Op SSAUnaryOpNode::op() const {
    return op_;
}

ValueId SSAUnaryOpNode::result() const {
    return result_;
}

ValueRef SSAUnaryOpNode::operand() const {
    return operand_;
}

void SSAUnaryOpNode::set_operand(ValueRef operand) {
    operand_ = operand;
}

SSABinOpNode::SSABinOpNode(Op op, ValueId result, ValueRef lhs, ValueRef rhs,
                           std::optional<SourceLocation> location)
    : UntypedSSANode(UntypedSSANode::SSA_BinOp, std::move(location)),
      op_(op),
      result_(result),
      lhs_(lhs),
      rhs_(rhs) {}

SSABinOpNode::Op SSABinOpNode::op() const {
    return op_;
}

ValueId SSABinOpNode::result() const {
    return result_;
}

ValueRef SSABinOpNode::lhs() const {
    return lhs_;
}

ValueRef SSABinOpNode::rhs() const {
    return rhs_;
}

void SSABinOpNode::set_lhs(ValueRef lhs) {
    lhs_ = lhs;
}

void SSABinOpNode::set_rhs(ValueRef rhs) {
    rhs_ = rhs;
}

SSACallNode::SSACallNode(Callee callee, std::vector<ValueId> results,
                         std::vector<ValueRef> inputs, std::optional<SourceLocation> location)
    : UntypedSSANode(UntypedSSANode::SSA_Call, std::move(location)),
      callee_(std::move(callee)),
      results_(std::move(results)),
      inputs_(std::move(inputs)) {}

const SSACallNode::Callee& SSACallNode::callee() const {
    return callee_;
}

const std::vector<ValueId>& SSACallNode::results() const {
    return results_;
}

const std::vector<ValueRef>& SSACallNode::inputs() const {
    return inputs_;
}

void SSACallNode::set_callee_indirect_value(ValueRef indirect_value) {
    callee_.indirect_value = indirect_value;
}

void SSACallNode::set_input(std::size_t index, ValueRef input) {
    if (index >= inputs_.size()) {
        return;
    }
    inputs_[index] = input;
}

SSACondJumpNode::SSACondJumpNode(ValueRef cond, BasicBlock* true_block, BasicBlock* false_block,
                                 std::optional<SourceLocation> location)
    : UntypedSSANode(UntypedSSANode::SSA_CondJump, std::move(location)),
      cond_(cond),
      true_block_(true_block),
      false_block_(false_block) {}

ValueRef SSACondJumpNode::cond() const {
    return cond_;
}

BasicBlock* SSACondJumpNode::true_block() const {
    return true_block_;
}

BasicBlock* SSACondJumpNode::false_block() const {
    return false_block_;
}

void SSACondJumpNode::set_cond(ValueRef cond) {
    cond_ = cond;
}

SSAJumpNode::SSAJumpNode(BasicBlock* target, std::optional<SourceLocation> location)
    : UntypedSSANode(UntypedSSANode::SSA_Jump, std::move(location)), target_(target) {}

BasicBlock* SSAJumpNode::target() const {
    return target_;
}

SSAReturnNode::SSAReturnNode(std::vector<ValueRef> values, std::optional<SourceLocation> location)
    : UntypedSSANode(UntypedSSANode::SSA_Return, std::move(location)),
      values_(std::move(values)) {}

const std::vector<ValueRef>& SSAReturnNode::values() const {
    return values_;
}

void SSAReturnNode::set_value(std::size_t index, ValueRef value) {
    if (index >= values_.size()) {
        return;
    }
    values_[index] = value;
}

Module::Module(std::string name, std::string source_path, Type type)
    : name_(std::move(name)), source_path_(std::move(source_path)), type_(type) {}

Module::Module(Module&& other) noexcept
    : name_(std::move(other.name_)),
      source_path_(std::move(other.source_path_)),
      type_(other.type_),
      function_storage_(std::move(other.function_storage_)),
      entry_function_(other.entry_function_) {
    rebind_function_parents();
    other.entry_function_ = nullptr;
}

Module& Module::operator=(Module&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    name_ = std::move(other.name_);
    source_path_ = std::move(other.source_path_);
    type_ = other.type_;
    function_storage_ = std::move(other.function_storage_);
    entry_function_ = other.entry_function_;
    rebind_function_parents();
    other.entry_function_ = nullptr;
    return *this;
}

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

void Module::rebind_function_parents() {
    for (const auto& function : function_storage_) {
        if (function != nullptr) {
            function->set_parent(this);
        }
    }
}

}  // namespace baltam
