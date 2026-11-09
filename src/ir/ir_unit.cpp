#include "ir/ir_unit.h"

#include "ba_obj/ba_type.h"
#include "ba_obj/ba_obj.h"
#include "ba_obj/type_base.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace baltam {

using IRStage = FunctionUnit::IRStage;

SlotId SlotTable::add_slot(InternedString name, bool is_internal) {
    // 用户与内部槽位分别去重，内部名称不会占用用户变量名。
    for (std::size_t i = 0; i < slots_.size(); ++i)
        if (slots_[i].name == name && slots_[i].is_internal == is_internal)
            return SlotId{static_cast<std::uint32_t>(i)};
    if (slots_.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::overflow_error("槽位编号已用尽");
    }
    const SlotId id{static_cast<std::uint32_t>(slots_.size())};
    slots_.push_back(SlotInfo{std::move(name), is_internal});
    return id;
}

const SlotInfo& SlotTable::find(SlotId id) const {
    return slots_.at(id.index);
}

SlotInfo& SlotTable::find(SlotId id) {
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

void ValueTable::define_parameter(ValueId id, std::uint32_t index) {
    auto& value = reserve(id);
    if (value.has_definition())
        throw std::invalid_argument("参数值编号重复");
    value.is_parameter_ = true;
    value.result_index_ = index;
}

void ValueTable::define(ValueId id, Instruction& definition, std::uint32_t result_index,
                        Value::Form form, bool constant) {
    if (!definition.parent() || definition.parent()->version_ != version_)
        throw std::invalid_argument("值定义指令不属于当前阶段版本");
    auto& value = reserve(id);
    if (value.has_definition())
        throw std::invalid_argument("值定义编号重复");
    value.definition_ = &definition;
    value.result_index_ = result_index;
    value.form_ = form;
    value.is_constant_ = constant;
    if (definition.type() == Instruction::Type::Const) {
        const auto& inst = static_cast<const ConstInst&>(definition);
        if (!inst.is_magic_colon()) {
            value.type_ = static_cast<ba_type>(inst.value->type());
            value.shape_.emplace();
            for (const auto dim : inst.value->cget<type_base>()->dim_vec())
                value.shape_->push_back(static_cast<std::size_t>(dim));
        }
    }
    if (definition.type() == Instruction::Type::CellConstruct) {
        const auto& inst = static_cast<const CellConstructInst&>(definition);
        value.type_ = ba_cell;
        value.shape_ = std::vector<std::optional<std::size_t>>{inst.rows, inst.cols};
    }
    if (definition.type() == Instruction::Type::LoadSlot) {
        const auto slot = static_cast<const LoadSlotInst&>(definition).slot;
        const auto& info = definition.parent()->parent()->slot_table().find(slot);
        if (info.type)
            value.type_ = info.type;
        if (info.shape)
            value.shape_ = info.shape;
    }
    if (definition.type() == Instruction::Type::Intrinsic) {
        const auto id = static_cast<const IntrinsicInst&>(definition).id;
        if (id == IntrinsicId::ForeachInit || id == IntrinsicId::AddDouble ||
            id == IntrinsicId::GtDouble) {
            value.type_ = id == IntrinsicId::GtDouble ? ba_bool_mat : ba_double_mat;
            value.shape_ = std::vector<std::optional<std::size_t>>{1, 1};
        }
    }
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

ValueId IRStageVersion::add_parameter(std::uint32_t index) {
    if (values_.size() > std::numeric_limits<std::uint32_t>::max())
        throw std::overflow_error("值编号已用尽");
    const ValueId id{static_cast<std::uint32_t>(values_.size())};
    values_.define_parameter(id, index);
    return id;
}

std::vector<ValueId> IRStageVersion::parameters() const {
    std::vector<ValueId> parameters;
    for (const auto& value : values_.values_)
        if (value->is_parameter())
            parameters.push_back(value->id());
    std::sort(parameters.begin(), parameters.end(), [this](ValueId lhs, ValueId rhs) {
        return values_.find(lhs).result_index() < values_.find(rhs).result_index();
    });
    return parameters;
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
    // 声明的静态属性在所有插入路径统一登记；实际绑定仍由声明指令执行。
    const std::vector<SlotId>* declared_slots = nullptr;
    const bool global = instruction->type() == Instruction::Type::GlobalDecl;
    if (global)
        declared_slots = &static_cast<const GlobalDeclInst&>(*instruction).slots;
    else if (instruction->type() == Instruction::Type::PersistentDecl)
        declared_slots = &static_cast<const PersistentDeclInst&>(*instruction).slots;
    if (declared_slots)
        for (const auto slot : *declared_slots)
            (void)parent_->slot_table().find(slot);
    instruction->bind(*version_);
    auto* added = instruction.get();
    instructions_.insert(instructions_.begin() + position, std::move(instruction));
    added->parent_ = this;
    if (declared_slots) {
        for (const auto slot : *declared_slots) {
            auto& info = parent_->slot_table().find(slot);
            if (global)
                info.has_global_decl = true;
            else
                info.has_persistent_decl = true;
        }
    }
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
    if (deopt_baseline_) {
        deopt_replacements_.resize(values_.size());
        deopt_replacements_[from.index] = to;
    }
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
        value->type_.reset();
        value->shape_.reset();
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
    for (const auto& value : values_.values_) {
        if (value->is_parameter())
            copy->values_.define_parameter(value->id(), value->result_index());
        else if (value->definition_)
            copy->values_.define(value->id(), *instructions.at(value->definition_),
                                 value->result_index_, value->form_, value->is_constant_);
        if (value->has_definition()) {
            auto& cloned = copy->values_.find(value->id());
            cloned.form_ = value->form_;
            cloned.is_constant_ = value->is_constant_;
            cloned.type_ = value->type_;
            cloned.shape_ = value->shape_;
        }
    }
    copy->return_count_ = return_count_;
    copy->deopt_baseline_ = deopt_baseline_;
    copy->baseline_to_ssa_ = baseline_to_ssa_;
    copy->deopt_replacements_ = deopt_replacements_;
    return copy;
}

void IRStageVersion::initialize_deopt(const IRStageVersion* baseline) {
    if (!baseline || baseline == this || deopt_baseline_)
        throw std::invalid_argument("回退基线必须是独立且尚未关联的版本");
    deopt_baseline_ = baseline;
    baseline_to_ssa_.resize(deopt_baseline_->values().size());
    for (std::size_t i = 0; i < baseline_to_ssa_.size(); ++i) {
        const ValueId id{static_cast<std::uint32_t>(i)};
        if (deopt_baseline_->values().find(id).has_definition())
            baseline_to_ssa_[i] = id;
    }
    for (std::size_t b = 0; b < blocks_.size(); ++b) {
        const auto& originals = deopt_baseline_->blocks()[b]->instructions();
        for (std::size_t i = 0; i < originals.size(); ++i)
            blocks_[b]->instructions_[i]->baseline_instruction_ = originals[i].get();
    }
}

std::optional<ValueId> IRStageVersion::deopt_source(ValueId baseline_value) const {
    auto source = baseline_to_ssa_.at(baseline_value.index);
    std::size_t remaining = deopt_replacements_.size();
    while (source && source->index < deopt_replacements_.size() &&
           deopt_replacements_[source->index]) {
        if (!remaining--)
            throw std::logic_error("回退寄存器替换关系存在循环");
        source = deopt_replacements_[source->index];
    }
    return source;
}

void IRStageVersion::compact_values() {
    // 先确认所有存活引用均有定义，再移动拥有者；Value 地址始终不变。
    for (const auto& value : values_.values_)
        if (!value->has_definition() && !value->use_empty())
            throw std::logic_error("压缩值表时发现未定义的值引用");
    // 入参、各返回位置引用的结果、函数体结果依次编号，同一值只登记一次。
    std::vector<ValueId> order;
    std::vector<bool> seen(values_.size(), false);
    const auto append = [&order, &seen](ValueId id) {
        if (!seen[id.index]) {
            seen[id.index] = true;
            order.push_back(id);
        }
    };
    for (const auto parameter : parameters())
        append(parameter);
    for (const auto& block : blocks_)
        for (const auto& inst : block->instructions())
            if (inst->type() == Instruction::Type::Return)
                for (const auto value : static_cast<const ReturnInst&>(*inst).values())
                    append(value);
    std::unordered_map<const Instruction*, std::vector<ValueId>> results;
    for (const auto& value : values_.values_)
        if (value->definition_)
            results[value->definition_].push_back(value->id());
    for (const auto& block : blocks_) {
        for (const auto& inst : block->instructions()) {
            auto& ids = results[inst.get()];
            std::sort(ids.begin(), ids.end(), [this](ValueId lhs, ValueId rhs) {
                return values_.find(lhs).result_index() < values_.find(rhs).result_index();
            });
            for (const auto id : ids)
                append(id);
        }
    }
    std::vector<std::optional<ValueId>> renumber(values_.size());
    for (std::size_t i = 0; i < order.size(); ++i)
        renumber[order[i].index] = ValueId{static_cast<std::uint32_t>(i)};
    // 统一压缩替换链，避免每个基线值重复遍历长链。
    std::vector<std::size_t> path;
    for (std::size_t i = 0; i < deopt_replacements_.size(); ++i) {
        if (!deopt_replacements_[i])
            continue;
        path.clear();
        ValueId source{static_cast<std::uint32_t>(i)};
        while (source.index < deopt_replacements_.size() && deopt_replacements_[source.index]) {
            if (path.size() == deopt_replacements_.size())
                throw std::logic_error("回退寄存器替换关系存在循环");
            path.push_back(source.index);
            source = *deopt_replacements_[source.index];
        }
        for (const auto index : path)
            deopt_replacements_[index] = source;
    }
    auto sources = baseline_to_ssa_;
    for (std::size_t i = 0; i < sources.size(); ++i) {
        const auto source = deopt_source(ValueId{static_cast<std::uint32_t>(i)});
        if (source) {
            sources[i] = renumber.at(source->index);
            if (!sources[i])
                throw std::logic_error("回退所需的寄存器来源已被删除");
        }
    }
    std::vector<std::unique_ptr<Value>> values;
    values.reserve(values_.size());
    for (const auto id : order)
        values.push_back(std::move(values_.values_[id.index]));
    for (std::size_t i = 0; i < values.size(); ++i) {
        auto& info = *values[i];
        const ValueId id{static_cast<std::uint32_t>(i)};
        info.id_ = id;
        if (info.is_parameter())
            continue;
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
    baseline_to_ssa_ = std::move(sources);
    deopt_replacements_.clear();
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
    nested_functions_.back()->lexical_parent_ = this;
    bind_module(*nested_functions_.back(), parent());
    return *nested_functions_.back();
}

void FunctionUnit::bind_shared_slot(SlotId local, FunctionUnit& owner, SlotId target) {
    auto* ancestor = lexical_parent_;
    while (ancestor && ancestor != &owner)
        ancestor = ancestor->lexical_parent_;
    if (!ancestor)
        throw std::invalid_argument("共享槽位必须指向词法外层函数");

    auto& local_info = slot_table().find(local);
    const auto& target_info = owner.slot_table().find(target);
    const auto reference = target_info.shared_target.value_or(SharedSlotRef{&owner, target});
    if (local_info.shared_by_nested)
        throw std::logic_error("已有嵌套引用的拥有者槽位不能改为共享引用，请从外到内登记");
    if (local_info.shared_target && (local_info.shared_target->function != reference.function ||
                                     local_info.shared_target->slot != reference.slot))
        throw std::logic_error("共享槽位已经绑定到其他变量");

    auto& shared = reference.function->slot_table().find(reference.slot);
    local_info.shared_target = reference;
    shared.shared_by_nested = true;
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
