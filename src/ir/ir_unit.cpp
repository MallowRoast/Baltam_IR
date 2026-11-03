#include "ir/ir_unit.h"

#include <limits>
#include <algorithm>
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

Value& ValueTable::reserve(ValueId id) {
    while (values_.size() <= id.index) {
        const ValueId next{static_cast<std::uint32_t>(values_.size())};
        values_.push_back(std::unique_ptr<Value>(new Value(next)));
    }
    return *values_[id.index];
}

const Value& ValueTable::find(ValueId id) const {
    if (id.index >= values_.size())
        throw std::out_of_range("值编号越界");
    return *values_[id.index];
}

Value& ValueTable::find(ValueId id) {
    return const_cast<Value&>(std::as_const(*this).find(id));
}

void ValueTable::define(ValueId id, Instruction& definition, std::uint32_t result_index,
                        Value::Form form, bool constant) {
    if (!definition.parent() || definition.parent()->version_ != version_)
        throw std::invalid_argument("值定义指令不属于当前阶段版本");
    auto& value = reserve(id);
    if (value.definition_)
        throw std::invalid_argument("值定义编号重复");
    value.definition_ = &definition;
    value.result_index_ = result_index;
    value.form_ = form;
    value.is_constant_ = constant;
}

IRStageVersion::~IRStageVersion() {
    // 循环 PHI 可能跨块互相引用，先断开所有输入，再释放指令和值。
    for (const auto& block : blocks_)
        for (const auto& instruction : block->instructions())
            instruction->drop_all_references();
}

ValueId IRStageVersion::add_value(Instruction& definition, std::uint32_t result_index,
                                  Value::Form form, bool constant) {
    if (values_.size() > std::numeric_limits<std::uint32_t>::max())
        throw std::overflow_error("值编号已用尽");
    const ValueId id{static_cast<std::uint32_t>(values_.size())};
    values_.define(id, definition, result_index, form, constant);
    return id;
}

Instruction& BasicBlock::insert_instruction(std::size_t position,
                                            std::unique_ptr<Instruction> instruction) {
    if (position > instructions_.size())
        throw std::out_of_range("指令插入位置越界");
    if (!instruction)
        throw std::invalid_argument("插入的指令不能为空");
    if (terminator() && (position == instructions_.size() || instruction->is_terminator()))
        throw std::logic_error("不能在终结指令之后插入指令或添加第二条终结指令");
    if (instruction->is_terminator() && position != instructions_.size())
        throw std::logic_error("终结指令只能插入基本块末尾");
    if (instruction->type() == Instruction::Type::Phi) {
        for (std::size_t i = 0; i < position; ++i)
            if (instructions_[i]->type() != Instruction::Type::Phi)
                throw std::logic_error("PHI 指令必须连续位于基本块开头");
    } else if (position < instructions_.size() &&
               instructions_[position]->type() == Instruction::Type::Phi) {
        throw std::logic_error("普通指令不能插在 PHI 指令之前");
    }
    instruction->bind(*version_);
    auto* added = instruction.get();
    instructions_.insert(instructions_.begin() + position, std::move(instruction));
    added->parent_ = this;
    return *added;
}

void BasicBlock::erase_instruction(std::size_t position) {
    version_->erase_instruction(*this, position);
}

std::size_t IRStageVersion::replace_all_uses(ValueId from, ValueId to) {
    if (from == to)
        return 0;
    auto& source = values_.find(from);
    (void)values_.find(to);
    std::size_t changed = 0;
    while (source.first_use_) {
        source.first_use_->set(to);
        ++changed;
    }
    return changed;
}

void IRStageVersion::erase_instruction(BasicBlock& block, std::size_t position) {
    if (position >= block.instructions_.size())
        throw std::out_of_range("指令删除位置越界");
    auto* removed = block.instructions_[position].get();
    for (const auto& value : values_.values_)
        if (value->definition_ == removed && !value->use_empty())
            throw std::logic_error("不能删除仍有结果被引用的指令，请先替换所有使用");
    removed->drop_all_references();
    for (const auto& value : values_.values_) {
        if (value->definition_ != removed)
            continue;
        value->definition_ = nullptr;
        value->result_index_ = 0;
        value->form_ = Value::Form::Single;
        value->is_constant_ = false;
    }
    block.instructions_.erase(block.instructions_.begin() + position);
}

std::shared_ptr<IRStageVersion> IRStageVersion::clone(IRUnit& owner) const {
    auto copy = std::make_shared<IRStageVersion>();
    if (values_.size())
        (void)copy->values_.reserve(ValueId{static_cast<std::uint32_t>(values_.size() - 1)});
    std::unordered_map<const BasicBlock*, BasicBlock*> blocks;
    std::unordered_map<const Instruction*, Instruction*> instructions;
    for (const auto& block : blocks_)
        blocks.emplace(block.get(), &copy->add_block(owner, block->label()));
    for (const auto& block : blocks_) {
        auto* target = blocks.at(block.get());
        for (const auto& instruction : block->instructions()) {
            auto cloned = instruction->clone();
            if (cloned->type() == Instruction::Type::Goto) {
                auto& jump = static_cast<GotoInst&>(*cloned);
                jump.target = blocks.at(jump.target);
            } else if (cloned->type() == Instruction::Type::Branch) {
                auto& branch = static_cast<BranchInst&>(*cloned);
                branch.true_target = blocks.at(branch.true_target);
                branch.false_target = blocks.at(branch.false_target);
            } else if (cloned->type() == Instruction::Type::Phi) {
                auto& phi = static_cast<PhiInst&>(*cloned);
                for (std::size_t i = 0; i < phi.operand_count(); ++i)
                    phi.set_predecessor(i, blocks.at(phi.predecessors()[i]));
            }
            auto& added =
                target->insert_instruction(target->instructions().size(), std::move(cloned));
            instructions.emplace(instruction.get(), &added);
        }
    }
    for (const auto& value : values_.values_)
        if (value->definition_)
            copy->values_.define(value->id(), *instructions.at(value->definition_),
                                 value->result_index_, value->form_, value->is_constant_);
    return copy;
}

void IRStageVersion::compact_values() {
    // 先确认所有存活引用均有定义，再移动拥有者；Value 地址始终不变。
    for (const auto& value : values_.values_)
        if (!value->definition_ && !value->use_empty())
            throw std::logic_error("压缩值表时发现未定义的值引用");
    std::vector<std::unique_ptr<Value>> values;
    values.reserve(values_.size());
    for (auto& value : values_.values_)
        if (value->definition_)
            values.push_back(std::move(value));
    for (std::size_t i = 0; i < values.size(); ++i) {
        auto& info = *values[i];
        const ValueId id{static_cast<std::uint32_t>(i)};
        info.id_ = id;
        switch (info.definition_->type()) {
        case Instruction::Type::Const:
            static_cast<ConstInst&>(*info.definition_).result = id;
            break;
        case Instruction::Type::Phi:
            static_cast<PhiInst&>(*info.definition_).result = id;
            break;
        case Instruction::Type::Concat:
            static_cast<ConcatInst&>(*info.definition_).result = id;
            break;
        case Instruction::Type::CellConstruct:
            static_cast<CellConstructInst&>(*info.definition_).result = id;
            break;
        case Instruction::Type::Colon:
            static_cast<ColonInst&>(*info.definition_).result = id;
            break;
        case Instruction::Type::MagicColon:
            static_cast<MagicColonInst&>(*info.definition_).result = id;
            break;
        case Instruction::Type::MagicEnd:
            static_cast<MagicEndInst&>(*info.definition_).result = id;
            break;
        case Instruction::Type::Apply:
            static_cast<ApplyInst&>(*info.definition_).results.at(info.result_index_) = id;
            break;
        case Instruction::Type::ValueApply:
            static_cast<ValueApplyInst&>(*info.definition_).results.at(info.result_index_) = id;
            break;
        case Instruction::Type::Call:
            static_cast<CallInst&>(*info.definition_).results.at(info.result_index_) = id;
            break;
        case Instruction::Type::Intrinsic:
            static_cast<IntrinsicInst&>(*info.definition_).result = id;
            break;
        case Instruction::Type::ParenReference:
            static_cast<ParenReferenceInst&>(*info.definition_).result = id;
            break;
        case Instruction::Type::BraceReference:
            static_cast<BraceReferenceInst&>(*info.definition_).result = id;
            break;
        case Instruction::Type::DotAccess:
            static_cast<DotAccessInst&>(*info.definition_).result = id;
            break;
        case Instruction::Type::DotReference:
            static_cast<DotReferenceInst&>(*info.definition_).result = id;
            break;
        case Instruction::Type::ParenAssign:
            static_cast<ParenAssignInst&>(*info.definition_).result = id;
            break;
        case Instruction::Type::ParenDelete:
            static_cast<ParenDeleteInst&>(*info.definition_).result = id;
            break;
        case Instruction::Type::BraceAssign:
            static_cast<BraceAssignInst&>(*info.definition_).result = id;
            break;
        case Instruction::Type::DotAssign:
            static_cast<DotAssignInst&>(*info.definition_).result = id;
            break;
        case Instruction::Type::Unary:
            static_cast<UnaryInst&>(*info.definition_).result = id;
            break;
        case Instruction::Type::Binary:
            static_cast<BinaryInst&>(*info.definition_).result = id;
            break;
        case Instruction::Type::LoadSlot:
            static_cast<LoadSlotInst&>(*info.definition_).result = id;
            break;
        default:
            throw std::logic_error("值表引用了不产生结果的指令");
        }
    }
    values_.values_ = std::move(values);
}

void FunctionUnit::set_stage_version(IRStage stage, std::shared_ptr<IRStageVersion> version) {
    if (!version)
        throw std::invalid_argument("阶段版本不能为空");
    for (const auto& block : version->blocks())
        if (block->parent() != this)
            throw std::invalid_argument("阶段版本不属于当前函数");
    switch (stage) {
    case IRStage::NonSSA:
        non_ssa_version_ = std::move(version);
        break;
    case IRStage::SSA:
        ssa_version_ = std::move(version);
        break;
    case IRStage::OptimizedSSA:
        optimized_ssa_version_ = std::move(version);
        break;
    default:
        throw std::invalid_argument("无效的 IR 阶段");
    }
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

} // 匿名命名空间

FunctionUnit::FunctionUnit(std::string name, std::vector<std::string> parameters,
                           std::vector<std::string> returns)
    : name_(std::move(name)) {
    std::unordered_map<std::string, SlotId> declared_slots;
    parameter_slots_.reserve(parameters.size());
    return_slots_.reserve(returns.size());

    // 同名入参和出参属于同一个工作区变量，按首次声明顺序分配槽位。
    auto register_slot = [this, &declared_slots](const std::string& variable_name) {
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

FunctionUnit& FunctionUnit::append_nested_function(std::unique_ptr<FunctionUnit> function) {
    if (!function) {
        throw std::invalid_argument("嵌套函数不能为空");
    }
    nested_functions_.push_back(std::move(function));
    bind_module(*nested_functions_.back(), parent());
    return *nested_functions_.back();
}

std::shared_ptr<const IRStageVersion> FunctionUnit::stage_version(IRStage stage) const {
    switch (stage) {
    case IRStage::NonSSA:
        return non_ssa_version_;
    case IRStage::SSA:
        return ssa_version_;
    case IRStage::OptimizedSSA:
        return optimized_ssa_version_;
    }
    throw std::invalid_argument("无效的 IR 阶段");
}

std::shared_ptr<IRStageVersion> FunctionUnit::stage_version(IRStage stage) {
    return std::const_pointer_cast<IRStageVersion>(std::as_const(*this).stage_version(stage));
}

std::shared_ptr<IRStageVersion> FunctionUnit::current_version() {
    return stage_version(current_stage_);
}

std::shared_ptr<const IRStageVersion> FunctionUnit::current_version() const {
    return stage_version(current_stage_);
}

IRModule::IRModule(SourceType source_type, ModuleType module_type,
                   std::optional<std::filesystem::path> source_path)
    : source_type_(source_type), module_type_(module_type), path_(std::move(source_path)) {
    if (path_) {
        path_ = (source_type_ == SourceType::File ? std::filesystem::absolute(*path_) : *path_)
                    .lexically_normal();
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

void IRModule::set_functions(
    std::vector<std::pair<std::string, std::unique_ptr<FunctionUnit>>>&& functions) {
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
