#include "ir/ir_unit.h"

#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace baltam {

using IRStage = FunctionUnit::IRStage;

SlotId SlotTable::add_slot(InternedString name) {
    if (slots_.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::overflow_error("槽位编号已用尽");
    }
    const SlotId id{static_cast<std::uint32_t>(slots_.size())};
    slots_.push_back(SlotInfo{std::move(name)});
    return id;
}

const SlotInfo& SlotTable::find(SlotId id) const {
    return slots_.at(id.index);
}

ValueId ValueTable::add_value(ValueInfo info) {
    if (values_.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::overflow_error("值编号已用尽");
    }
    const ValueId id{static_cast<std::uint32_t>(values_.size())};
    values_.push_back(std::move(info));
    return id;
}

const ValueInfo& ValueTable::find(ValueId id) const {
    return values_.at(id.index);
}

ValueId IRStageVersion::add_value(ValueInfo info) {
    if (!info.def) {
        throw std::invalid_argument("值定义指令不能为空");
    }
    bool owned = false;
    for (const auto& block : blocks_) {
        for (const auto& instruction : block->instructions()) {
            if (instruction.get() == info.def) {
                owned = true;
                break;
            }
        }
        if (owned) {
            break;
        }
    }
    if (!owned) {
        throw std::invalid_argument("值定义指令不属于当前阶段版本");
    }
    return values_.add_value(std::move(info));
}

namespace {

void bind_module(FunctionUnit& function, IRModule* module) {
    function.set_parent(module);
    for (const auto& nested : function.nested_functions()) {
        if (nested) {
            bind_module(*nested, module);
        }
    }
}

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
        const SlotId id = slot_table().add_slot(variable_name);
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

FunctionUnit& FunctionUnit::append_nested_function(
    std::unique_ptr<FunctionUnit> function) {
    if (!function) {
        throw std::invalid_argument("嵌套函数不能为空");
    }
    nested_functions_.push_back(std::move(function));
    bind_module(*nested_functions_.back(), parent());
    return *nested_functions_.back();
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

std::shared_ptr<IRStageVersion> FunctionUnit::current_version() {
    return stage_version(current_stage_);
}

std::shared_ptr<const IRStageVersion> FunctionUnit::current_version() const {
    return stage_version(current_stage_);
}

IRModule::IRModule(SourceType source_type, ModuleType module_type,
                   std::optional<std::filesystem::path> source_path)
    : source_type_(source_type),
      module_type_(module_type),
      path_(std::move(source_path)) {
    if (path_) {
        path_ = path_->lexically_normal();
    }
}

const FunctionUnit* IRModule::find_function(std::string_view key) const {
    const auto it = function_map.find(key);
    return it == function_map.end() ? nullptr : it->second;
}

void IRModule::set_command_unit(std::unique_ptr<CommandUnit>&& unit) {
    command_unit_ = std::move(unit);
    if (command_unit_) {
        command_unit_->set_parent(this);
    }
}

void IRModule::set_functions(std::vector<std::pair<std::string, std::unique_ptr<FunctionUnit>>>&& functions) {
    std::map<std::string, FunctionUnit*, std::less<>> new_map;
    for (const auto& item : functions) {
        new_map.insert_or_assign(item.first, item.second.get());
    }
    std::vector<std::unique_ptr<FunctionUnit>> new_functions;
    new_functions.reserve(functions.size());
    for (auto& item : functions) {
        new_functions.push_back(std::move(item.second));
    }
    functions_.swap(new_functions);
    function_map.swap(new_map);
    for (const auto& function : functions_) {
        if (function) {
            bind_module(*function, this);
        }
    }
}

} // baltam 命名空间
