#include "ir/ir_builder.h"

#include <algorithm>
#include <string>
#include <type_traits>
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

TypeFact fixed_slot_type_fact(SlotValueType value_type) noexcept {
    switch (value_type) {
        case SlotValueType::Unknown:
            return unknown_type_fact();
        case SlotValueType::Int64Scalar:
            return scalar_type_fact(TypeSet::int64());
        case SlotValueType::LogicalScalar:
            return scalar_type_fact(TypeSet::logical());
    }

    return unknown_type_fact();
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

TypeFact internal_call_result_type_fact(const CallInst& inst, std::size_t result_index) noexcept {
    if (inst.dispatch_type != Internal ||
        inst.callee_kind != CallInst::Direct ||
        !std::holds_alternative<InternedString>(inst.callee)) {
        return unknown_type_fact();
    }

    const InternedString& callee = std::get<InternedString>(inst.callee);
    if (callee == "switch_match") {
        if (result_index == 0U) {
            return scalar_type_fact(TypeSet::logical());
        }
    }

    if (callee == "foreach_init") {
        if (result_index == 0U) {
            return scalar_type_fact(TypeSet::external_object());
        }
        if (result_index == 1U) {
            return scalar_type_fact(TypeSet::int64());
        }
    }

    if (callee == "end_index") {
        if (result_index == 0U) {
            return scalar_type_fact(TypeSet::int64());
        }
    }

    return unknown_type_fact();
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

TypeFact matching_add_result_type_fact(
    const ValueTable& value_table,
    const BinaryInst& inst) {
    const TypeFact lhs_fact = operand_type_fact(value_table, inst.lhs);
    const TypeFact rhs_fact = operand_type_fact(value_table, inst.rhs);

    // Internal add is not always an int64 index op. Preserve the operand type only
    // when both sides already carry the same concrete type fact.
    if (lhs_fact.is_unknown ||
        rhs_fact.is_unknown ||
        lhs_fact.types != rhs_fact.types ||
        lhs_fact.is_scalar != rhs_fact.is_scalar) {
        return unknown_type_fact();
    }

    return lhs_fact;
}

TypeFact binary_result_type_fact(
    const ValueTable& value_table,
    const BinaryInst& inst) {
    if (inst.dispatch_type != Internal) {
        return unknown_type_fact();
    }

    switch (inst.op) {
        case Add:
            return matching_add_result_type_fact(value_table, inst);
        case Gt:
            return scalar_type_fact(TypeSet::logical());
        case Sub:
        case Mul:
        case Rdiv:
        case Ldiv:
        case Pow:
        case ElemMul:
        case ElemRdiv:
        case ElemLdiv:
        case ElemPow:
        case And:
        case Or:
        case Lt:
        case Le:
        case Ge:
        case Eq:
        case Ne:
            break;
    }

    return unknown_type_fact();
}

TypeFact instruction_result_type_fact(
    const CodeUnit* unit,
    const ValueTable& value_table,
    const Instruction* instruction) {
    if (instruction == nullptr) {
        return unknown_type_fact();
    }

    switch (instruction->type()) {
        case Instruction::Const:
            return constant_type_fact(static_cast<const ConstInst*>(instruction)->value);
        case Instruction::LoadSlot: {
            if (unit == nullptr) {
                return unknown_type_fact();
            }
            const auto* inst = static_cast<const LoadSlotInst*>(instruction);
            const SlotInfo* slot = unit->slot_table.find_slot(inst->slot);
            return slot != nullptr
                ? fixed_slot_type_fact(slot->value_type)
                : unknown_type_fact();
        }
        case Instruction::Apply:
        case Instruction::ValueApply:
        case Instruction::Call:
        case Instruction::MagicEnd:
            return unknown_type_fact();
        case Instruction::CreateNamedFunctionHandle:
        case Instruction::CreateAnonymousFunctionHandle:
            return scalar_type_fact(TypeSet::function_handle());
        case Instruction::Unary:
            return unknown_type_fact();
        case Instruction::Binary:
            return binary_result_type_fact(
                value_table,
                static_cast<const BinaryInst&>(*instruction));
        case Instruction::StoreSlot:
        case Instruction::GlobalDecl:
        case Instruction::PersistentDecl:
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

void bind_instruction_results(CodeUnit* unit, Instruction* instruction) {
    if (instruction == nullptr) {
        return;
    }

    if (unit == nullptr) {
        return;
    }

    ValueTable& value_table = unit->value_table;
    const TypeFact result_type_fact = instruction_result_type_fact(
        unit,
        value_table,
        instruction);

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
        case Instruction::CreateNamedFunctionHandle: {
            const auto* inst = static_cast<const CreateNamedFunctionHandleInst*>(instruction);
            bind_value_def(value_table, inst->result, 0, instruction, result_type_fact);
            break;
        }
        case Instruction::CreateAnonymousFunctionHandle: {
            const auto* inst =
                static_cast<const CreateAnonymousFunctionHandleInst*>(instruction);
            bind_value_def(value_table, inst->result, 0, instruction, result_type_fact);
            break;
        }
        case Instruction::Apply: {
            const auto* inst = static_cast<const ApplyInst*>(instruction);
            for (std::size_t i = 0; i < inst->results.size(); ++i) {
                if (!inst->results[i].is_valid()) {
                    continue;
                }
                bind_value_def(value_table, inst->results[i], i, instruction, result_type_fact);
            }
            break;
        }
        case Instruction::ValueApply: {
            const auto* inst = static_cast<const ValueApplyInst*>(instruction);
            for (std::size_t i = 0; i < inst->results.size(); ++i) {
                if (!inst->results[i].is_valid()) {
                    continue;
                }
                bind_value_def(value_table, inst->results[i], i, instruction, result_type_fact);
            }
            break;
        }
        case Instruction::MagicEnd: {
            const auto* inst = static_cast<const MagicEndInst*>(instruction);
            bind_value_def(
                value_table,
                inst->result,
                0,
                instruction,
                unknown_type_fact());
            break;
        }
        case Instruction::Call: {
            const auto* inst = static_cast<const CallInst*>(instruction);
            for (std::size_t i = 0; i < inst->results.size(); ++i) {
                if (!inst->results[i].is_valid()) {
                    continue;
                }
                const TypeFact call_result_type_fact = inst->dispatch_type == Internal
                    ? internal_call_result_type_fact(*inst, i)
                    : result_type_fact;
                bind_value_def(value_table, inst->results[i], i, instruction, call_result_type_fact);
            }
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
        case Instruction::GlobalDecl:
        case Instruction::PersistentDecl:
        case Instruction::Goto:
        case Instruction::Branch:
        case Instruction::Return:
            break;
    }
}

} // namespace

void IRBuilder::reset() noexcept {
    owned_module_.reset();
    current_file_ = nullptr;
    unit_states_.clear();
    current_unit_state_ = nullptr;
    next_anonymous_function_ = 0;
    diagnostics_.clear();
}

MFileUnit& IRBuilder::begin_file(NormalizedPath path) {
    reset();

    owned_module_ = std::make_unique<IRModule>();
    auto file = std::make_unique<MFileUnit>();
    file->module = owned_module_.get();
    file->path = std::move(path);

    current_file_ = file.get();
    owned_module_->files.push_back(std::move(file));

    return *current_file_;
}

template <typename UnitT>
UnitT& IRBuilder::begin_unit(
    std::string_view name,
    SourceSpan source_span,
    std::string_view missing_file_message) {
    if (current_file_ == nullptr) {
        report(
            IRBuildDiagnostic::Error,
            missing_file_message,
            source_span);
        if (owned_module_ == nullptr) {
            owned_module_ = std::make_unique<IRModule>();
        }
        auto file = std::make_unique<MFileUnit>();
        file->module = owned_module_.get();
        current_file_ = file.get();
        owned_module_->files.push_back(std::move(file));
    }

    auto unit = std::make_unique<UnitT>();
    if constexpr (std::is_same_v<UnitT, ScriptUnit> ||
                  std::is_same_v<UnitT, FunctionUnit>) {
        unit->file = current_file_;
    }
    if (!name.empty()) {
        unit->name.assign(name.data(), name.size());
    } else if (current_file_ != nullptr) {
        unit->name = current_file_->file_stem().string();
    } else {
        unit->name.clear();
    }
    unit->source_span = source_span;

    UnitT* unit_ptr = unit.get();
    current_file_->code_units.push_back(std::move(unit));

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

AnonymousFunctionUnit& IRBuilder::begin_anonymous_function_unit(SourceSpan source_span) {
    if (owned_module_ == nullptr) {
        report(
            IRBuildDiagnostic::Error,
            "创建匿名函数体单元前必须先创建 module",
            source_span);
        owned_module_ = std::make_unique<IRModule>();
    }

    CodeUnit* lexical_parent = current_unit();
    if (lexical_parent == nullptr) {
        report(
            IRBuildDiagnostic::Error,
            "创建匿名函数体单元前必须先有词法父级代码单元",
            source_span);
    }

    auto unit = std::make_unique<AnonymousFunctionUnit>();
    unit->lexical_parent = lexical_parent;
    unit->id = create_anonymous_function_id();
    unit->name = "__anon" + std::to_string(unit->id.value());
    unit->source_span = source_span;

    AnonymousFunctionUnit* unit_ptr = unit.get();
    owned_module_->anonymous_functions.functions.push_back(std::move(unit));

    auto state = std::make_unique<IRUnitBuildState>();
    state->unit = unit_ptr;
    current_unit_state_ = state.get();
    unit_states_[unit_ptr] = std::move(state);

    return *unit_ptr;
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

Slot IRBuilder::create_slot(
    SlotTag tag,
    std::string_view name,
    SourceSpan source_span,
    SlotValueType value_type) {
    if (current_unit_state_ == nullptr || current_unit_state_->unit == nullptr) {
        report(
            IRBuildDiagnostic::Error,
            "没有活动代码单元，无法创建槽位",
            source_span);
        return InvalidSlot;
    }

    SlotInfo info;
    info.slot.id = current_unit_state_->ids.allocate_slot();
    info.slot.tag = tag;
    info.name = InternedString(name);
    info.source_span = source_span;
    info.value_type = value_type;

    current_unit_state_->unit->slot_table.slots.push_back(info);

    if (current_unit_state_->unit->is_function()) {
        auto* function = static_cast<FunctionUnit*>(current_unit_state_->unit);
        if (tag == SlotTag::Arg) {
            function->param_slots.push_back(info.slot);
        } else if (tag == SlotTag::Ret) {
            function->return_slots.push_back(info.slot);
        }
    } else if (current_unit_state_->unit->is_anonymous_function()) {
        auto* function = static_cast<AnonymousFunctionUnit*>(current_unit_state_->unit);
        if (tag == SlotTag::Arg) {
            function->param_slots.push_back(info.slot);
        } else if (tag == SlotTag::Capture) {
            function->capture_slots.push_back(info.slot);
        }
    }

    return info.slot;
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

AnonymousFunctionId IRBuilder::create_anonymous_function_id() {
    if (owned_module_ == nullptr) {
        report(
            IRBuildDiagnostic::Error,
            "没有活动 module，无法创建匿名函数 ID",
            SourceSpan::invalid());
        return InvalidAnonymousFunctionId;
    }

    return AnonymousFunctionId(next_anonymous_function_++);
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
    bind_instruction_results(current_unit_state_->unit, instruction.get());
    current_unit_state_->current_block->instructions.push_back(std::move(instruction));
}

IRBuildResult IRBuilder::finish() {
    IRBuildResult result;
    result.mfile = current_file_;
    result.module = std::move(owned_module_);
    result.diagnostics = std::move(diagnostics_);

    unit_states_.clear();
    current_unit_state_ = nullptr;
    current_file_ = nullptr;
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
