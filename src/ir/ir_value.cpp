#include "ir/ir_value.h"
#include "ir/ir_unit.h"

#include <stdexcept>

namespace baltam {

Use::~Use() {
    unlink();
}

std::optional<ValueId> Use::value() const noexcept {
    return target_ ? std::optional<ValueId>{target_->id()} : pending_value_;
}

void Use::unlink() noexcept {
    if (previous_)
        previous_->next_ = next_;
    else if (target_)
        target_->first_use_ = next_;
    if (next_)
        next_->previous_ = previous_;
    target_ = nullptr;
    previous_ = nullptr;
    next_ = nullptr;
}

void Use::set(std::optional<ValueId> value) {
    // 先解析目标，分配失败时保留原有引用关系。
    Value* target = value && user_->version_ ? &user_->version_->values().reserve(*value) : nullptr;
    unlink();
    pending_value_ = user_->version_ ? std::nullopt : value;
    target_ = target;
    if (target_) {
        next_ = target_->first_use_;
        if (next_)
            next_->previous_ = this;
        target_->first_use_ = this;
    }
}

User::~User() = default;

User::User(const User& other) {
    for (std::size_t i = 0; i < other.operand_count(); ++i)
        append_operand(other.operand(i).value());
}

void User::set_operand(std::size_t index, std::optional<ValueId> value) {
    operands_.at(index)->set(value);
}

void User::append_operand(std::optional<ValueId> value) {
    insert_operand(operands_.size(), value);
}

void User::insert_operand(std::size_t index, std::optional<ValueId> value) {
    if (index > operands_.size())
        throw std::out_of_range("操作数插入位置越界");
    auto use = std::unique_ptr<Use>(new Use(*this, index));
    use->set(value);
    operands_.insert(operands_.begin() + index, std::move(use));
    for (std::size_t i = index + 1; i < operands_.size(); ++i)
        operands_[i]->operand_index_ = i;
}

void User::erase_operand(std::size_t index) {
    if (index >= operands_.size())
        throw std::out_of_range("操作数删除位置越界");
    operands_.erase(operands_.begin() + index);
    for (std::size_t i = index; i < operands_.size(); ++i)
        operands_[i]->operand_index_ = i;
}

void User::bind(IRStageVersion& version) {
    if (version_)
        throw std::logic_error("操作数所属阶段不能重复绑定");
    // 预先完成分配，避免绑定过程只挂入部分输入。
    for (const auto& use : operands_)
        if (use->pending_value_)
            (void)version.values().reserve(*use->pending_value_);
    version_ = &version;
    for (const auto& use : operands_)
        use->set(use->pending_value_);
}

void User::drop_all_references() noexcept {
    for (const auto& use : operands_) {
        use->unlink();
        use->pending_value_.reset();
    }
}

ValueId User::input(std::size_t index) const {
    const auto value = operand(index).value();
    if (!value)
        throw std::logic_error("普通输入不能为 Undefined");
    return *value;
}

void User::append_values(const std::vector<ValueId>& values) {
    for (const auto value : values)
        append_operand(value);
}

std::vector<ValueId> User::input_values(std::size_t offset) const {
    const auto size = operand_count() - offset;
    std::vector<ValueId> values;
    values.reserve(size);
    for (std::size_t i = 0; i < size; ++i)
        values.push_back(input(offset + i));
    return values;
}

User::NamedOperand User::append_named_operand(const Operand& value) {
    if (const auto* id = std::get_if<ValueId>(&value)) {
        const auto index = operand_count();
        append_operand(*id);
        return index;
    }
    return std::get<InternedString>(value);
}

Operand User::named_operand(const NamedOperand& value) const {
    if (const auto* index = std::get_if<std::size_t>(&value))
        return input(*index);
    return std::get<InternedString>(value);
}

} // baltam 命名空间
