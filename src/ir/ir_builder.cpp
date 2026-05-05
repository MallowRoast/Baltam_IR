#include "ir/ir_builder.h"

#include <algorithm>
#include <utility>
#include <variant>

namespace baltam {
namespace {

TypeFact unknown_type_fact() noexcept {
    return {};
}

TypeFact type_fact(TypeSet types, bool is_scalar) noexcept {
    TypeFact fact;
    fact.is_unknown = false;
    fact.is_scalar = is_scalar;
    fact.types = types;
    return fact;
}

TypeFact scalar_type_fact(TypeSet types) noexcept {
    return type_fact(types, true);
}

TypeFact constant_type_fact(const LogicalConstant&) noexcept {
    return scalar_type_fact(TypeSet::logical());
}

TypeFact constant_type_fact(const Int64Constant&) noexcept {
    return scalar_type_fact(TypeSet::int64());
}

TypeFact constant_type_fact(const UInt64Constant&) noexcept {
    return scalar_type_fact(TypeSet::uint64());
}

TypeFact constant_type_fact(const Float64Constant&) noexcept {
    return scalar_type_fact(TypeSet::float64());
}

TypeFact constant_type_fact(const Complex128Constant&) noexcept {
    return scalar_type_fact(TypeSet::complex());
}

TypeFact constant_type_fact(const CharLiteralConstant&) noexcept {
    return type_fact(TypeSet::char_array(), false);
}

TypeFact constant_type_fact(const StringLiteralConstant&) noexcept {
    return scalar_type_fact(TypeSet::string_scalar());
}

TypeFact constant_type_fact(const EmptyDoubleMatrixConstant&) noexcept {
    return type_fact(TypeSet::float64(), false);
}

TypeFact constant_type_fact(const Constant& constant) {
    return std::visit(
        [](const auto& value) {
            return constant_type_fact(value);
        },
        constant);
}

TypeFact operand_type_fact(const ValueTable& value_table, const Operand& operand) {
    const auto* value_id = std::get_if<ValueId>(&operand);
    if (value_id == nullptr) {
        return unknown_type_fact();
    }

    const ValueInfo* value_info = value_table.find(*value_id);
    return value_info != nullptr ? value_info->type_fact : unknown_type_fact();
}

TypeFact instruction_result_type_fact(
    const ValueTable& value_table,
    const Instruction* instruction) {
    if (instruction == nullptr) {
        return unknown_type_fact();
    }

    switch (instruction->type()) {
        case Instruction::Const:
            return constant_type_fact(static_cast<const ConstInst*>(instruction)->value);
        case Instruction::LoadSlot:
        case Instruction::LoadWorkspace:
        case Instruction::Apply:
        case Instruction::Call:
            return unknown_type_fact();
        case Instruction::Copy:
            return operand_type_fact(
                value_table,
                static_cast<const CopyInst*>(instruction)->value);
        case Instruction::Unary:
        case Instruction::Binary:
            return unknown_type_fact();
        case Instruction::StoreSlot:
        case Instruction::StoreWorkspace:
        case Instruction::Goto:
        case Instruction::Branch:
        case Instruction::Return:
            break;
    }

    return unknown_type_fact();
}

void bind_value_def(
    ValueTable& value_table,
    ValueId value_id,
    std::size_t result_index,
    Instruction* def,
    TypeFact type_fact) {
    ValueInfo* value_info = value_table.find(value_id);
    if (value_info == nullptr) {
        return;
    }

    value_info->result_index = result_index;
    value_info->def = def;
    value_info->type_fact = type_fact;
}

void bind_instruction_results(ValueTable& value_table, Instruction* instruction) {
    if (instruction == nullptr) {
        return;
    }

    const TypeFact result_type_fact = instruction_result_type_fact(value_table, instruction);

    switch (instruction->type()) {
        case Instruction::Const: {
            const auto* inst = static_cast<const ConstInst*>(instruction);
            bind_value_def(value_table, inst->result, 0, instruction, result_type_fact);
            break;
        }
        case Instruction::LoadSlot: {
            const auto* inst = static_cast<const LoadSlotInst*>(instruction);
            bind_value_def(value_table, inst->result, 0, instruction, result_type_fact);
            break;
        }
        case Instruction::LoadWorkspace: {
            const auto* inst = static_cast<const LoadWorkspaceInst*>(instruction);
            bind_value_def(value_table, inst->result, 0, instruction, result_type_fact);
            break;
        }
        case Instruction::Apply: {
            const auto* inst = static_cast<const ApplyInst*>(instruction);
            for (std::size_t i = 0; i < inst->results.size(); ++i) {
                bind_value_def(value_table, inst->results[i], i, instruction, result_type_fact);
            }
            break;
        }
        case Instruction::Call: {
            const auto* inst = static_cast<const CallInst*>(instruction);
            for (std::size_t i = 0; i < inst->results.size(); ++i) {
                bind_value_def(value_table, inst->results[i], i, instruction, result_type_fact);
            }
            break;
        }
        case Instruction::Copy: {
            const auto* inst = static_cast<const CopyInst*>(instruction);
            bind_value_def(value_table, inst->result, 0, instruction, result_type_fact);
            break;
        }
        case Instruction::Unary: {
            const auto* inst = static_cast<const UnaryInst*>(instruction);
            bind_value_def(value_table, inst->result, 0, instruction, result_type_fact);
            break;
        }
        case Instruction::Binary: {
            const auto* inst = static_cast<const BinaryInst*>(instruction);
            bind_value_def(value_table, inst->result, 0, instruction, result_type_fact);
            break;
        }
        case Instruction::StoreSlot:
        case Instruction::StoreWorkspace:
        case Instruction::Goto:
        case Instruction::Branch:
        case Instruction::Return:
            break;
    }
}

} // namespace

void IRBuilder::reset() noexcept {
    owned_file_.reset();
    unit_states_.clear();
    current_unit_state_ = nullptr;
    diagnostics_.clear();
}

MFileUnit& IRBuilder::begin_file(NormalizedPath path) {
    reset();

    owned_file_ = std::make_unique<MFileUnit>();
    owned_file_->path = std::move(path);

    return *owned_file_;
}

template <typename UnitT>
UnitT& IRBuilder::begin_unit(
    std::string_view name,
    SourceSpan source_span,
    std::string_view missing_file_message) {
    if (owned_file_ == nullptr) {
        report(
            IRBuildDiagnostic::Error,
            missing_file_message,
            source_span);
        owned_file_ = std::make_unique<MFileUnit>();
    }

    auto unit = std::make_unique<UnitT>();
    unit->parent = owned_file_.get();
    if (!name.empty()) {
        unit->name.assign(name.data(), name.size());
    } else if (owned_file_ != nullptr) {
        unit->name = owned_file_->file_stem().string();
    } else {
        unit->name.clear();
    }
    unit->source_span = source_span;

    UnitT* unit_ptr = unit.get();
    owned_file_->code_units.push_back(std::move(unit));

    auto state = std::make_unique<IRUnitBuildState>();
    state->unit = unit_ptr;
    current_unit_state_ = state.get();
    unit_states_[unit_ptr] = std::move(state);

    return *unit_ptr;
}

ScriptUnit& IRBuilder::begin_script_unit(std::string_view name, SourceSpan source_span) {
    return begin_unit<ScriptUnit>(
        name,
        source_span,
        "创建脚本代码单元前必须先创建文件");
}

FunctionUnit& IRBuilder::begin_function_unit(std::string_view name, SourceSpan source_span) {
    return begin_unit<FunctionUnit>(
        name,
        source_span,
        "创建函数代码单元前必须先创建文件");
}

void IRBuilder::set_current_unit(CodeUnit* unit) {
    if (unit == nullptr) {
        current_unit_state_ = nullptr;
        return;
    }

    const auto it = unit_states_.find(unit);
    if (it == unit_states_.end()) {
        report(
            IRBuildDiagnostic::Error,
            "切换活动代码单元失败：该单元不属于当前 builder",
            unit->source_span);
        return;
    }

    current_unit_state_ = it->second.get();
}

void IRBuilder::set_insert_point(BasicBlock* block) {
    if (current_unit_state_ == nullptr) {
        report(
            IRBuildDiagnostic::Error,
            "没有活动代码单元，无法设置插入点",
            SourceSpan::invalid());
        return;
    }

    if (block == nullptr) {
        current_unit_state_->current_block = nullptr;
        return;
    }

    if (block->parent != current_unit_state_->unit) {
        report(
            IRBuildDiagnostic::Error,
            "插入点基本块不属于当前活动代码单元",
            block->source_span);
        return;
    }

    current_unit_state_->current_block = block;
}

CodeUnit* IRBuilder::current_unit() noexcept {
    return const_cast<CodeUnit*>(std::as_const(*this).current_unit());
}

const CodeUnit* IRBuilder::current_unit() const noexcept {
    return current_unit_state_ != nullptr ? current_unit_state_->unit : nullptr;
}

BasicBlock* IRBuilder::current_block() noexcept {
    return const_cast<BasicBlock*>(std::as_const(*this).current_block());
}

const BasicBlock* IRBuilder::current_block() const noexcept {
    return current_unit_state_ != nullptr ? current_unit_state_->current_block : nullptr;
}

SlotId IRBuilder::create_slot(
    Slot::Type type,
    std::string_view name,
    SourceSpan source_span,
    SlotAttrs attrs) {
    if (current_unit_state_ == nullptr || current_unit_state_->unit == nullptr) {
        report(
            IRBuildDiagnostic::Error,
            "没有活动代码单元，无法创建槽位",
            source_span);
        return InvalidSlotId;
    }

    Slot slot;
    slot.slot_id = current_unit_state_->ids.allocate_slot();
    slot.type = type;
    slot.name = InternedString(name);
    slot.source_span = source_span;
    slot.attrs = attrs;

    current_unit_state_->unit->slot_table.slots.push_back(slot);

    if (current_unit_state_->unit->is_function()) {
        auto* function = static_cast<FunctionUnit*>(current_unit_state_->unit);
        if (type == Slot::Arg) {
            function->param_slots.push_back(slot.slot_id);
        } else if (type == Slot::Ret) {
            function->return_slots.push_back(slot.slot_id);
        }
    }

    return slot.slot_id;
}

SlotId IRBuilder::create_hidden_slot(
    std::string_view name,
    SlotAttrs::HiddenRole role,
    SourceSpan source_span) {
    if (current_unit_state_ == nullptr || current_unit_state_->unit == nullptr) {
        report(
            IRBuildDiagnostic::Error,
            "没有活动代码单元，无法创建隐藏槽位",
            source_span);
        return InvalidSlotId;
    }

    if (role != SlotAttrs::None) {
        if (const Slot* existing = current_unit_state_->unit->find_hidden_slot(role)) {
            report(
                IRBuildDiagnostic::Error,
                "同一个代码单元中出现了重复的隐藏槽位角色",
                source_span);
            return existing->slot_id;
        }
    }

    if (role == SlotAttrs::WorkspaceHandle && !current_unit_state_->unit->is_script()) {
        report(
            IRBuildDiagnostic::Error,
            "工作区句柄隐藏槽位只能出现在脚本代码单元中",
            source_span);
        return InvalidSlotId;
    }

    if ((role == SlotAttrs::Nargin ||
         role == SlotAttrs::Nargout ||
         role == SlotAttrs::Varargin ||
         role == SlotAttrs::Varargout) &&
        !current_unit_state_->unit->is_function()) {
        report(
            IRBuildDiagnostic::Error,
            "函数专用的隐藏槽位角色不能出现在脚本代码单元中",
            source_span);
        return InvalidSlotId;
    }

    SlotAttrs attrs;
    attrs.hidden_role = role;
    attrs.is_user_visible = 0;
    attrs.is_mutable = 0;

    return create_slot(Slot::Hidden, name, source_span, attrs);
}

ValueId IRBuilder::create_value() {
    if (current_unit_state_ == nullptr || current_unit_state_->unit == nullptr) {
        report(
            IRBuildDiagnostic::Error,
            "没有活动代码单元，无法创建值",
            SourceSpan::invalid());
        return InvalidValueId;
    }

    const ValueId value_id = current_unit_state_->ids.allocate_value();

    ValueInfo value_info;
    value_info.value_id = value_id;
    current_unit_state_->unit->value_table.values.push_back(value_info);

    return value_id;
}

void IRBuilder::bind_name(std::string_view name, SlotId slot_id) {
    if (current_unit_state_ == nullptr || current_unit_state_->unit == nullptr) {
        report(
            IRBuildDiagnostic::Error,
            "没有活动代码单元，无法绑定名字",
            SourceSpan::invalid());
        return;
    }

    if (!slot_id.is_valid()) {
        report(
            IRBuildDiagnostic::Error,
            "当前名字绑定缺少有效槽位",
            SourceSpan::invalid());
        return;
    }

    current_unit_state_->name_bindings[InternedString(name)] = slot_id;
}

SlotId* IRBuilder::find_name(std::string_view name) noexcept {
    return const_cast<SlotId*>(std::as_const(*this).find_name(name));
}

const SlotId* IRBuilder::find_name(std::string_view name) const noexcept {
    if (current_unit_state_ == nullptr) {
        return nullptr;
    }

    const auto it = current_unit_state_->name_bindings.find(InternedString(name));
    return it != current_unit_state_->name_bindings.end() ? &it->second : nullptr;
}

void IRBuilder::append_instruction(std::unique_ptr<Instruction> instruction) {
    if (current_unit_state_ == nullptr || current_unit_state_->unit == nullptr) {
        report(
            IRBuildDiagnostic::Error,
            "没有活动代码单元，无法追加指令",
            instruction != nullptr ? instruction->source_span : SourceSpan::invalid());
        return;
    }

    if (instruction == nullptr) {
        report(
            IRBuildDiagnostic::Error,
            "不能追加空指令",
            SourceSpan::invalid());
        return;
    }

    if (current_unit_state_->current_block == nullptr) {
        report(
            IRBuildDiagnostic::Error,
            "没有活动插入点，无法追加指令",
            instruction->source_span);
        return;
    }

    if (current_unit_state_->current_block->parent != current_unit_state_->unit) {
        report(
            IRBuildDiagnostic::Error,
            "当前插入点基本块不属于活动代码单元",
            instruction->source_span);
        return;
    }

    if (current_unit_state_->current_block->has_terminator()) {
        report(
            IRBuildDiagnostic::Error,
            "基本块终结指令之后不能继续追加指令",
            instruction->source_span);
        return;
    }

    switch (instruction->type()) {
    case Instruction::Goto: {
        const auto* goto_inst = static_cast<const GotoInst*>(instruction.get());
        if (goto_inst->target != nullptr) {
            if (std::find(
                    current_unit_state_->current_block->successors.begin(),
                    current_unit_state_->current_block->successors.end(),
                    goto_inst->target) == current_unit_state_->current_block->successors.end()) {
                current_unit_state_->current_block->successors.push_back(goto_inst->target);
            }

            if (std::find(
                    goto_inst->target->predecessors.begin(),
                    goto_inst->target->predecessors.end(),
                    current_unit_state_->current_block) == goto_inst->target->predecessors.end()) {
                goto_inst->target->predecessors.push_back(current_unit_state_->current_block);
            }
        }
        break;
    }
    case Instruction::Branch: {
        const auto* branch_inst = static_cast<const BranchInst*>(instruction.get());
        if (branch_inst->true_target != nullptr) {
            if (std::find(
                    current_unit_state_->current_block->successors.begin(),
                    current_unit_state_->current_block->successors.end(),
                    branch_inst->true_target) ==
                current_unit_state_->current_block->successors.end()) {
                current_unit_state_->current_block->successors.push_back(branch_inst->true_target);
            }

            if (std::find(
                    branch_inst->true_target->predecessors.begin(),
                    branch_inst->true_target->predecessors.end(),
                    current_unit_state_->current_block) ==
                branch_inst->true_target->predecessors.end()) {
                branch_inst->true_target->predecessors.push_back(current_unit_state_->current_block);
            }
        }

        if (branch_inst->false_target != nullptr) {
            if (std::find(
                    current_unit_state_->current_block->successors.begin(),
                    current_unit_state_->current_block->successors.end(),
                    branch_inst->false_target) ==
                current_unit_state_->current_block->successors.end()) {
                current_unit_state_->current_block->successors.push_back(branch_inst->false_target);
            }

            if (std::find(
                    branch_inst->false_target->predecessors.begin(),
                    branch_inst->false_target->predecessors.end(),
                    current_unit_state_->current_block) ==
                branch_inst->false_target->predecessors.end()) {
                branch_inst->false_target->predecessors.push_back(current_unit_state_->current_block);
            }
        }

        break;
    }
    default:
        break;
    }

    instruction->parent = current_unit_state_->current_block;
    bind_instruction_results(current_unit_state_->unit->value_table, instruction.get());
    current_unit_state_->current_block->instructions.push_back(std::move(instruction));
}

IRBuildResult IRBuilder::finish() {
    IRBuildResult result;
    result.mfile = std::move(owned_file_);
    result.diagnostics = std::move(diagnostics_);

    unit_states_.clear();
    current_unit_state_ = nullptr;
    diagnostics_.clear();

    return result;
}

void IRBuilder::report(
    IRBuildDiagnostic::Severity severity,
    std::string_view message,
    SourceSpan source_span) {
    diagnostics_.push_back({
        severity,
        InternedString(message),
        source_span,
    });
}

} // namespace baltam
