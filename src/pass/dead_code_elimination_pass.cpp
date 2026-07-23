#include "pass/dead_code_elimination_pass.h"

#include <algorithm>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace baltam {
namespace {

using UsedValueSet = std::unordered_set<ValueId>;
using RemovedValueSet = std::unordered_set<ValueId>;
using StoreSet = std::unordered_set<const StoreSlotInst*>;

[[nodiscard]] bool is_dse_slot_tag(SlotTag tag) noexcept {
    switch (tag) {
        case SlotTag::Local:
        case SlotTag::Ret:
        case SlotTag::InternalLocal:
            return true;
        case SlotTag::BaseVar:
        case SlotTag::ScriptVar:
        case SlotTag::Arg:
        case SlotTag::Capture:
        case SlotTag::Global:
        case SlotTag::Persistent:
        case SlotTag::Nargin:
        case SlotTag::Nargout:
        case SlotTag::Varargin:
        case SlotTag::Varargout:
            return false;
    }

    return false;
}

[[nodiscard]] bool is_dse_slot(const CodeUnit& unit, Slot slot) {
    const SlotInfo* slot_info = unit.slot_table.find_slot(slot);
    return slot_info != nullptr && is_dse_slot_tag(slot_info->slot.tag);
}

[[nodiscard]] bool is_store_barrier(const Instruction& instruction) noexcept {
    switch (instruction.type()) {
        case Instruction::GlobalDecl:
        case Instruction::PersistentDecl:
        case Instruction::CreateNamedFunctionHandle:
        case Instruction::CreateAnonymousFunctionHandle:
        case Instruction::Apply:
        case Instruction::ValueApply:
        case Instruction::MagicEnd:
        case Instruction::Call:
            return true;
        case Instruction::Unary:
            return static_cast<const UnaryInst&>(instruction).dispatch_type != Internal;
        case Instruction::Binary:
            return static_cast<const BinaryInst&>(instruction).dispatch_type != Internal;
        case Instruction::Const:
        case Instruction::LoadSlot:
        case Instruction::StoreSlot:
        case Instruction::Goto:
        case Instruction::Branch:
        case Instruction::Return:
            return false;
    }

    return false;
}

void mark_overwritten_stores(
    const CodeUnit& unit,
    const BasicBlock& block,
    StoreSet& stores_to_remove) {
    std::unordered_map<SlotId, const StoreSlotInst*> pending_stores;

    for (const auto& inst_ptr : block.instructions) {
        if (inst_ptr == nullptr) {
            continue;
        }

        const Instruction& instruction = *inst_ptr;
        if (instruction.type() == Instruction::LoadSlot) {
            const auto& load = static_cast<const LoadSlotInst&>(instruction);
            if (is_dse_slot(unit, load.slot)) {
                pending_stores.erase(load.slot.id);
            }
            continue;
        }

        if (instruction.type() == Instruction::StoreSlot) {
            const auto& store = static_cast<const StoreSlotInst&>(instruction);
            if (is_dse_slot(unit, store.slot)) {
                const auto previous = pending_stores.find(store.slot.id);
                if (previous != pending_stores.end() && previous->second != nullptr) {
                    stores_to_remove.insert(previous->second);
                }
                pending_stores[store.slot.id] = &store;
            }
            continue;
        }

        if (is_store_barrier(instruction)) {
            pending_stores.clear();
        }
    }
}

[[nodiscard]] StoreSet collect_overwritten_stores(const CodeUnit& unit) {
    StoreSet stores_to_remove;

    for (const auto& block_ptr : unit.basic_blocks) {
        if (block_ptr != nullptr) {
            mark_overwritten_stores(unit, *block_ptr, stores_to_remove);
        }
    }

    return stores_to_remove;
}

[[nodiscard]] bool remove_stores(CodeUnit& unit, const StoreSet& stores_to_remove) {
    if (stores_to_remove.empty()) {
        return false;
    }

    bool changed = false;
    for (const auto& block_ptr : unit.basic_blocks) {
        if (block_ptr == nullptr) {
            continue;
        }

        auto& instructions = block_ptr->instructions;
        const auto old_size = instructions.size();
        instructions.erase(
            std::remove_if(
                instructions.begin(),
                instructions.end(),
                [&stores_to_remove](const std::unique_ptr<Instruction>& inst) {
                    if (inst == nullptr || inst->type() != Instruction::StoreSlot) {
                        return false;
                    }

                    return stores_to_remove.find(
                        static_cast<const StoreSlotInst*>(inst.get())) != stores_to_remove.end();
                }),
            instructions.end());
        changed = changed || instructions.size() != old_size;
    }

    return changed;
}

void mark_value_used(ValueId value, UsedValueSet& values) {
    if (value.is_valid()) {
        values.insert(value);
    }
}

void collect_operand_values(const Operand& operand, UsedValueSet& values) {
    if (const auto* value = std::get_if<ValueId>(&operand)) {
        mark_value_used(*value, values);
    }
}

void collect_operand_values(const std::vector<Operand>& operands, UsedValueSet& values) {
    for (const Operand& operand : operands) {
        collect_operand_values(operand, values);
    }
}

void collect_instruction_uses(const Instruction& instruction, UsedValueSet& values) {
    switch (instruction.type()) {
        case Instruction::StoreSlot:
            mark_value_used(static_cast<const StoreSlotInst&>(instruction).value, values);
            break;
        case Instruction::CreateAnonymousFunctionHandle:
            for (const auto& capture :
                 static_cast<const CreateAnonymousFunctionHandleInst&>(instruction).captures) {
                mark_value_used(capture.captured_value, values);
            }
            break;
        case Instruction::Apply: {
            const auto& inst = static_cast<const ApplyInst&>(instruction);
            collect_operand_values(inst.callee_or_base, values);
            collect_operand_values(inst.arguments, values);
            break;
        }
        case Instruction::ValueApply: {
            const auto& inst = static_cast<const ValueApplyInst&>(instruction);
            mark_value_used(inst.base, values);
            collect_operand_values(inst.arguments, values);
            break;
        }
        case Instruction::MagicEnd:
            for (const auto& context :
                 static_cast<const MagicEndInst&>(instruction).candidate_contexts) {
                collect_operand_values(context.callee_or_base, values);
            }
            break;
        case Instruction::Call: {
            const auto& inst = static_cast<const CallInst&>(instruction);
            collect_operand_values(inst.callee, values);
            collect_operand_values(inst.arguments, values);
            break;
        }
        case Instruction::Unary:
            collect_operand_values(static_cast<const UnaryInst&>(instruction).operand, values);
            break;
        case Instruction::Binary: {
            const auto& inst = static_cast<const BinaryInst&>(instruction);
            collect_operand_values(inst.lhs, values);
            collect_operand_values(inst.rhs, values);
            break;
        }
        case Instruction::Branch:
            collect_operand_values(static_cast<const BranchInst&>(instruction).condition, values);
            break;
        case Instruction::Return:
            for (ValueId value : static_cast<const ReturnInst&>(instruction).values) {
                mark_value_used(value, values);
            }
            break;
        case Instruction::Const:
        case Instruction::LoadSlot:
        case Instruction::GlobalDecl:
        case Instruction::PersistentDecl:
        case Instruction::CreateNamedFunctionHandle:
        case Instruction::Goto:
            break;
    }
}

[[nodiscard]] UsedValueSet collect_used_values(const CodeUnit& unit) {
    UsedValueSet values;

    for (const auto& block_ptr : unit.basic_blocks) {
        if (block_ptr == nullptr) {
            continue;
        }

        for (const auto& inst_ptr : block_ptr->instructions) {
            if (inst_ptr != nullptr) {
                collect_instruction_uses(*inst_ptr, values);
            }
        }
    }

    return values;
}

void clear_removed_defs(CodeUnit& unit, const RemovedValueSet& removed_values) {
    for (ValueInfo& value_info : unit.value_table.values) {
        if (removed_values.find(value_info.value_id) != removed_values.end()) {
            value_info.def = nullptr;
        }
    }
}

[[nodiscard]] bool remove_unused_constants(CodeUnit& unit) {
    const UsedValueSet used_values = collect_used_values(unit);
    RemovedValueSet removed_values;
    bool changed = false;

    for (const auto& block_ptr : unit.basic_blocks) {
        if (block_ptr == nullptr) {
            continue;
        }

        auto& instructions = block_ptr->instructions;
        instructions.erase(
            std::remove_if(
                instructions.begin(),
                instructions.end(),
                [&used_values, &removed_values, &changed](
                    const std::unique_ptr<Instruction>& inst) {
                    if (inst == nullptr || inst->type() != Instruction::Const) {
                        return false;
                    }

                    const auto& constant = static_cast<const ConstInst&>(*inst);
                    if (!constant.result.is_valid() ||
                        used_values.find(constant.result) != used_values.end()) {
                        return false;
                    }

                    removed_values.insert(constant.result);
                    changed = true;
                    return true;
                }),
            instructions.end());
    }

    if (!removed_values.empty()) {
        clear_removed_defs(unit, removed_values);
    }

    return changed;
}

} // namespace

IRPassResult DeadCodeEliminationPass::run(CodeUnit& unit, IRPassContext& context) {
    (void)context;

    IRPassResult result;

    const StoreSet overwritten_stores = collect_overwritten_stores(unit);
    result.changed = remove_stores(unit, overwritten_stores);
    result.changed = remove_unused_constants(unit) || result.changed;
    return result;
}

} // namespace baltam
