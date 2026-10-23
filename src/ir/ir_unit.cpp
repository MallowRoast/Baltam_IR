#include "ir/ir_unit.h"

#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace baltam {

SlotId SlotTable::add_slot(InternedString name, SlotTag tag,
                           SlotValueType fixed_type) {
    if (slots_.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::overflow_error("槽位编号已用尽");
    }
    const SlotId id{static_cast<std::uint32_t>(slots_.size())};
    slots_.push_back(SlotInfo{std::move(name), tag, fixed_type});
    return id;
}

const SlotInfo* SlotTable::find(SlotId id) const noexcept {
    return id.index < slots_.size() ? &slots_[id.index] : nullptr;
}

ValueId ValueTable::reserve_results(std::size_t count) {
    if (count == 0) {
        throw std::invalid_argument("预留的结果数量必须大于零");
    }
    constexpr std::uint64_t capacity =
        std::uint64_t{std::numeric_limits<std::uint32_t>::max()} + 1;
    if (count > capacity - values_.size()) {
        throw std::overflow_error("值编号已用尽");
    }
    const ValueId first{static_cast<std::uint32_t>(values_.size())};
    values_.resize(values_.size() + count);
    return first;
}

void ValueTable::bind(ValueId id, Instruction& definition,
                      std::uint32_t result_index, ValueInfo::Form form) {
    if (id.index >= values_.size()) {
        throw std::out_of_range("值编号未预留");
    }
    ValueInfo& info = values_[id.index];
    if (info.definition) {
        throw std::logic_error("值编号已经绑定定义指令");
    }
    info.definition = &definition;
    info.result_index = result_index;
    info.form = form;
    if (definition.type() == Instruction::Type::Const) {
        info.constant_kind = ValueInfo::ConstantKind::Object;
    } else if (definition.type() == Instruction::Type::MagicColon) {
        info.constant_kind = ValueInfo::ConstantKind::IndexColon;
    }
}

void ValueTable::discard_last(ValueId first, std::size_t count) {
    if (count == 0 || first.index > values_.size() ||
        count != values_.size() - first.index) {
        throw std::invalid_argument("只能撤销末尾连续预留的值编号");
    }
    for (std::size_t i = first.index; i < values_.size(); ++i) {
        if (values_[i].definition) {
            throw std::logic_error("已经绑定的值编号不能撤销");
        }
    }
    values_.resize(first.index);
}

const ValueInfo* ValueTable::find(ValueId id) const noexcept {
    if (id.index >= values_.size()) {
        return nullptr;
    }
    const ValueInfo& info = values_[id.index];
    return info.definition ? &info : nullptr;
}

bool ValueTable::all_defined() const noexcept {
    for (const ValueInfo& info : values_) {
        if (!info.definition) {
            return false;
        }
    }
    return true;
}

namespace {

template <typename Pointer>
Pointer& select_version(IRStage stage, Pointer& non_ssa, Pointer& ssa,
                        Pointer& optimized_ssa) {
    switch (stage) {
        case IRStage::NonSSA: return non_ssa;
        case IRStage::SSA: return ssa;
        case IRStage::OptimizedSSA: return optimized_ssa;
    }
    throw std::invalid_argument("无效的 IR 阶段");
}

void replace_version(IRStage stage, std::shared_ptr<IRStageVersion> version,
                     std::shared_ptr<IRStageVersion>& non_ssa,
                     std::shared_ptr<IRStageVersion>& ssa,
                     std::shared_ptr<IRStageVersion>& optimized_ssa) {
    if (stage == IRStage::NonSSA) {
        throw std::invalid_argument("Non-SSA 阶段不能替换");
    }
    if (!version || version->stage() != stage) {
        throw std::invalid_argument("替换的 IR 阶段版本无效");
    }
    if (!version->values().all_defined()) {
        throw std::invalid_argument("IR 阶段版本存在未绑定的值编号");
    }
    select_version(stage, non_ssa, ssa, optimized_ssa) = std::move(version);
}

} // 匿名命名空间

FunctionUnit::FunctionUnit(std::string name,
                           std::vector<std::string> parameters,
                           std::vector<std::string> returns)
    : name_(std::move(name)) {
    std::unordered_map<std::string, SlotId> declared_slots;
    parameter_slots_.reserve(parameters.size());
    return_slots_.reserve(returns.size());

    // 同名入参和出参属于同一个工作区变量，按首次声明顺序分配槽位。
    auto register_slot = [&](const std::string& variable_name) {
        if (variable_name.empty()) {
            throw std::invalid_argument("函数的入参或出参名称不能为空");
        }
        auto found = declared_slots.find(variable_name);
        if (found != declared_slots.end()) {
            return found->second;
        }
        const SlotId id = slot_table().add_slot(variable_name, SlotTag::Local);
        declared_slots.emplace(variable_name, id);
        return id;
    };

    for (const std::string& parameter : parameters) {
        parameter_slots_.push_back(register_slot(parameter));
    }
    for (const std::string& output : returns) {
        return_slots_.push_back(register_slot(output));
    }
}

FunctionUnit& FunctionUnit::add_nested_function(
    std::string name, std::vector<std::string> parameters,
    std::vector<std::string> returns) {
    if (name.empty()) {
        throw std::invalid_argument("函数名称不能为空");
    }
    auto function = std::unique_ptr<FunctionUnit>(
        new FunctionUnit(std::move(name), std::move(parameters), std::move(returns)));
    nested_functions_.push_back(std::move(function));
    return *nested_functions_.back();
}

BasicBlock& FunctionUnit::add_block(IRStage stage) {
    return select_version(stage, non_ssa_version_, ssa_version_,
                          optimized_ssa_version_)->add_block();
}

std::shared_ptr<const IRStageVersion>
FunctionUnit::stage_version(IRStage stage) const {
    return select_version(stage, non_ssa_version_, ssa_version_,
                          optimized_ssa_version_);
}

std::shared_ptr<IRStageVersion>
FunctionUnit::stage_version(IRStage stage) {
    return select_version(stage, non_ssa_version_, ssa_version_,
                          optimized_ssa_version_);
}

void FunctionUnit::replace_stage_version(
    IRStage stage, std::shared_ptr<IRStageVersion> version) {
    replace_version(stage, std::move(version), non_ssa_version_, ssa_version_,
                    optimized_ssa_version_);
}

BasicBlock& AnonymousFunctionUnit::add_block(IRStage stage) {
    return select_version(stage, non_ssa_version_, ssa_version_,
                          optimized_ssa_version_)->add_block();
}

std::shared_ptr<const IRStageVersion>
AnonymousFunctionUnit::stage_version(IRStage stage) const {
    return select_version(stage, non_ssa_version_, ssa_version_,
                          optimized_ssa_version_);
}

std::shared_ptr<IRStageVersion>
AnonymousFunctionUnit::stage_version(IRStage stage) {
    return select_version(stage, non_ssa_version_, ssa_version_,
                          optimized_ssa_version_);
}

void AnonymousFunctionUnit::replace_stage_version(
    IRStage stage, std::shared_ptr<IRStageVersion> version) {
    replace_version(stage, std::move(version), non_ssa_version_, ssa_version_,
                    optimized_ssa_version_);
}

} // baltam 命名空间
