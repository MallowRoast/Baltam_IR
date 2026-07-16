#include "pass/load_forwarding_pass.h"

#include <algorithm>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace baltam {
namespace {

struct SlotState {
    SlotTag tag = SlotTag::Local;
    ValueId value = InvalidValueId;
};

using SlotStateMap = std::unordered_map<SlotId, SlotState>;
using ValueRewriteMap = std::unordered_map<ValueId, ValueId>;

[[nodiscard]] const SlotInfo* find_slot_info(const CodeUnit& unit, Slot slot) {
    return unit.slot_table.find_slot(slot);
}

[[nodiscard]] bool is_environment_slot_tag(SlotTag tag) noexcept {
    switch (tag) {
        case SlotTag::ScriptVar:
        case SlotTag::BaseVar:
        case SlotTag::Global:
        case SlotTag::Persistent:
            return true;
        case SlotTag::Local:
        case SlotTag::Arg:
        case SlotTag::Ret:
        case SlotTag::Capture:
        case SlotTag::InternalLocal:
        case SlotTag::Nargin:
        case SlotTag::Nargout:
        case SlotTag::Varargin:
        case SlotTag::Varargout:
            return false;
    }
    return false;
}

[[nodiscard]] ValueId resolve_value(ValueId value, const ValueRewriteMap& rewrites) {
    std::unordered_set<ValueId> seen;
    while (value.is_valid()) {
        const auto it = rewrites.find(value);
        if (it == rewrites.end()) {
            break;
        }
        if (!seen.insert(value).second) {
            break;
        }
        value = it->second;
    }
    return value;
}

void invalidate_environment_states(SlotStateMap& slot_states) {
    for (auto it = slot_states.begin(); it != slot_states.end();) {
        if (is_environment_slot_tag(it->second.tag)) {
            it = slot_states.erase(it);
        } else {
            ++it;
        }
    }
}

void rewrite_value(ValueId& value, const ValueRewriteMap& rewrites) {
    value = resolve_value(value, rewrites);
}

void rewrite_operand(Operand& operand, const ValueRewriteMap& rewrites) {
    if (auto* value = std::get_if<ValueId>(&operand)) {
        rewrite_value(*value, rewrites);
    }
}

void rewrite_operands(std::vector<Operand>& operands, const ValueRewriteMap& rewrites) {
    for (Operand& operand : operands) {
        rewrite_operand(operand, rewrites);
    }
}

void rewrite_values(std::vector<ValueId>& values, const ValueRewriteMap& rewrites) {
    for (ValueId& value : values) {
        rewrite_value(value, rewrites);
    }
}

void rewrite_instruction_uses(Instruction& instruction, const ValueRewriteMap& rewrites) {
    switch (instruction.type()) {
        case Instruction::StoreSlot:
            rewrite_value(static_cast<StoreSlotInst&>(instruction).value, rewrites);
            break;
        case Instruction::CreateAnonymousFunctionHandle:
            for (auto& capture :
                 static_cast<CreateAnonymousFunctionHandleInst&>(instruction).captures) {
                rewrite_value(capture.captured_value, rewrites);
            }
            break;
        case Instruction::Apply: {
            auto& inst = static_cast<ApplyInst&>(instruction);
            rewrite_operand(inst.callee_or_base, rewrites);
            rewrite_operands(inst.arguments, rewrites);
            break;
        }
        case Instruction::ValueApply: {
            auto& inst = static_cast<ValueApplyInst&>(instruction);
            rewrite_value(inst.base, rewrites);
            rewrite_operands(inst.arguments, rewrites);
            break;
        }
        case Instruction::MagicEnd:
            for (auto& context : static_cast<MagicEndInst&>(instruction).candidate_contexts) {
                rewrite_operand(context.callee_or_base, rewrites);
            }
            break;
        case Instruction::Call: {
            auto& inst = static_cast<CallInst&>(instruction);
            rewrite_operand(inst.callee, rewrites);
            rewrite_operands(inst.arguments, rewrites);
            break;
        }
        case Instruction::Copy:
            rewrite_operand(static_cast<CopyInst&>(instruction).value, rewrites);
            break;
        case Instruction::Unary:
            rewrite_operand(static_cast<UnaryInst&>(instruction).operand, rewrites);
            break;
        case Instruction::Binary: {
            auto& inst = static_cast<BinaryInst&>(instruction);
            rewrite_operand(inst.lhs, rewrites);
            rewrite_operand(inst.rhs, rewrites);
            break;
        }
        case Instruction::Branch:
            rewrite_operand(static_cast<BranchInst&>(instruction).condition, rewrites);
            break;
        case Instruction::Return:
            rewrite_values(static_cast<ReturnInst&>(instruction).values, rewrites);
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

void rewrite_uses(CodeUnit& unit, const ValueRewriteMap& rewrites) {
    if (rewrites.empty()) {
        return;
    }

    for (const auto& block_ptr : unit.basic_blocks) {
        if (block_ptr == nullptr) {
            continue;
        }

        for (const auto& inst_ptr : block_ptr->instructions) {
            if (inst_ptr != nullptr) {
                rewrite_instruction_uses(*inst_ptr, rewrites);
            }
        }
    }
}

void remove_forwarded_loads(
    CodeUnit& unit,
    const std::unordered_set<LoadSlotInst*>& loads_to_remove) {
    if (loads_to_remove.empty()) {
        return;
    }

    for (const auto& block_ptr : unit.basic_blocks) {
        if (block_ptr == nullptr) {
            continue;
        }

        auto& instructions = block_ptr->instructions;
        instructions.erase(
            std::remove_if(
                instructions.begin(),
                instructions.end(),
                [&loads_to_remove](const std::unique_ptr<Instruction>& inst) {
                    return inst != nullptr &&
                        inst->type() == Instruction::LoadSlot &&
                        loads_to_remove.find(
                            static_cast<LoadSlotInst*>(inst.get())) != loads_to_remove.end();
                }),
            instructions.end());
    }
}

} // namespace

IRPassResult LoadForwardingPass::run(CodeUnit& unit, IRPassContext& context) {
    (void)context;

    IRPassResult result;
    ValueRewriteMap rewrites;
    std::unordered_set<LoadSlotInst*> loads_to_remove;

    for (const auto& block_ptr : unit.basic_blocks) {
        if (block_ptr == nullptr) {
            continue;
        }

        SlotStateMap slot_states;
        for (const auto& inst_ptr : block_ptr->instructions) {
            if (inst_ptr == nullptr) {
                continue;
            }

            Instruction& instruction = *inst_ptr;
            switch (instruction.type()) {
                case Instruction::StoreSlot: {
                    auto& inst = static_cast<StoreSlotInst&>(instruction);
                    const SlotInfo* slot_info = find_slot_info(unit, inst.slot);
                    if (slot_info == nullptr) {
                        slot_states.erase(inst.slot.id);
                        break;
                    }

                    const ValueId resolved = resolve_value(inst.value, rewrites);
                    if (resolved.is_valid()) {
                        slot_states[inst.slot.id] = {
                            slot_info->slot.tag,
                            resolved,
                        };
                    } else {
                        slot_states.erase(inst.slot.id);
                    }
                    break;
                }
                case Instruction::LoadSlot: {
                    auto& inst = static_cast<LoadSlotInst&>(instruction);
                    const SlotInfo* slot_info = find_slot_info(unit, inst.slot);
                    if (slot_info == nullptr) {
                        break;
                    }

                    const auto state_it = slot_states.find(inst.slot.id);
                    if (state_it == slot_states.end() ||
                        !state_it->second.value.is_valid() ||
                        inst.result == state_it->second.value) {
                        break;
                    }

                    rewrites[inst.result] = state_it->second.value;
                    loads_to_remove.insert(&inst);
                    result.changed = true;
                    break;
                }
                default:
                    if (instruction.effect == Heap ||
                        instruction.effect == Env ||
                        instruction.effect == Opaque) {
                        invalidate_environment_states(slot_states);
                    }
                    break;
            }
        }
    }

    if (loads_to_remove.empty()) {
        return result;
    }

    for (LoadSlotInst* load : loads_to_remove) {
        if (load == nullptr) {
            continue;
        }

        if (ValueInfo* value_info = unit.value_table.find(load->result)) {
            value_info->def = nullptr;
        }
    }

    rewrite_uses(unit, rewrites);
    remove_forwarded_loads(unit, loads_to_remove);
    return result;
}

} // namespace baltam
