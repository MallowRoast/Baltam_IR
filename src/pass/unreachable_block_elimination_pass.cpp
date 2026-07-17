#include "pass/unreachable_block_elimination_pass.h"

#include <algorithm>
#include <memory>
#include <unordered_set>
#include <variant>
#include <vector>

namespace baltam {
namespace {

void mark_reachable(
    BasicBlock* block,
    const CodeUnit& unit,
    std::unordered_set<BasicBlock*>& reachable) {
    if (block == nullptr || block->parent != &unit) {
        return;
    }

    if (!reachable.insert(block).second) {
        return;
    }

    for (BasicBlock* successor : block->successors) {
        mark_reachable(successor, unit, reachable);
    }
}

void collect_defined_values(
    const Instruction& instruction,
    std::unordered_set<ValueId>& values) {
    switch (instruction.type()) {
        case Instruction::Const:
            values.insert(static_cast<const ConstInst&>(instruction).result);
            break;
        case Instruction::LoadSlot:
            values.insert(static_cast<const LoadSlotInst&>(instruction).result);
            break;
        case Instruction::CreateNamedFunctionHandle:
            values.insert(static_cast<const CreateNamedFunctionHandleInst&>(instruction).result);
            break;
        case Instruction::CreateAnonymousFunctionHandle:
            values.insert(static_cast<const CreateAnonymousFunctionHandleInst&>(instruction).result);
            break;
        case Instruction::Apply:
            for (ValueId value : static_cast<const ApplyInst&>(instruction).results) {
                values.insert(value);
            }
            break;
        case Instruction::ValueApply:
            for (ValueId value : static_cast<const ValueApplyInst&>(instruction).results) {
                values.insert(value);
            }
            break;
        case Instruction::MagicEnd:
            values.insert(static_cast<const MagicEndInst&>(instruction).result);
            break;
        case Instruction::Call:
            for (ValueId value : static_cast<const CallInst&>(instruction).results) {
                values.insert(value);
            }
            break;
        case Instruction::Unary:
            values.insert(static_cast<const UnaryInst&>(instruction).result);
            break;
        case Instruction::Binary:
            values.insert(static_cast<const BinaryInst&>(instruction).result);
            break;
        case Instruction::StoreSlot:
        case Instruction::GlobalDecl:
        case Instruction::PersistentDecl:
        case Instruction::Goto:
        case Instruction::Branch:
        case Instruction::Return:
            break;
    }
}

void collect_operand_values(const Operand& operand, std::unordered_set<ValueId>& values) {
    if (const auto* value = std::get_if<ValueId>(&operand)) {
        values.insert(*value);
    }
}

void collect_operand_values(
    const std::vector<Operand>& operands,
    std::unordered_set<ValueId>& values) {
    for (const Operand& operand : operands) {
        collect_operand_values(operand, values);
    }
}

void collect_used_values(
    const Instruction& instruction,
    std::unordered_set<ValueId>& values) {
    switch (instruction.type()) {
        case Instruction::StoreSlot:
            values.insert(static_cast<const StoreSlotInst&>(instruction).value);
            break;
        case Instruction::CreateAnonymousFunctionHandle:
            for (const auto& capture :
                 static_cast<const CreateAnonymousFunctionHandleInst&>(instruction).captures) {
                values.insert(capture.captured_value);
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
            values.insert(inst.base);
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
                values.insert(value);
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

bool has_reachable_use_of_removed_value(
    const CodeUnit& unit,
    const std::unordered_set<BasicBlock*>& reachable,
    const std::unordered_set<BasicBlock*>& unreachable,
    std::unordered_set<ValueId>& removed_values) {
    for (const auto& block_ptr : unit.basic_blocks) {
        if (block_ptr == nullptr || unreachable.find(block_ptr.get()) == unreachable.end()) {
            continue;
        }

        for (const auto& inst_ptr : block_ptr->instructions) {
            if (inst_ptr != nullptr) {
                collect_defined_values(*inst_ptr, removed_values);
            }
        }
    }

    if (removed_values.empty()) {
        return false;
    }

    for (const auto& block_ptr : unit.basic_blocks) {
        if (block_ptr == nullptr || reachable.find(block_ptr.get()) == reachable.end()) {
            continue;
        }

        std::unordered_set<ValueId> used_values;
        for (const auto& inst_ptr : block_ptr->instructions) {
            if (inst_ptr != nullptr) {
                collect_used_values(*inst_ptr, used_values);
            }
        }

        for (ValueId value : used_values) {
            if (value.is_valid() && removed_values.find(value) != removed_values.end()) {
                return true;
            }
        }
    }

    return false;
}

void clear_removed_defs(
    CodeUnit& unit,
    const std::unordered_set<ValueId>& removed_values) {
    for (ValueInfo& value_info : unit.value_table.values) {
        if (removed_values.find(value_info.value_id) != removed_values.end()) {
            value_info.def = nullptr;
        }
    }
}

void rebuild_predecessors(CodeUnit& unit) {
    for (const auto& block_ptr : unit.basic_blocks) {
        if (block_ptr != nullptr) {
            block_ptr->predecessors.clear();
        }
    }

    for (const auto& block_ptr : unit.basic_blocks) {
        if (block_ptr == nullptr) {
            continue;
        }

        BasicBlock* block = block_ptr.get();
        for (BasicBlock* successor : block->successors) {
            if (successor == nullptr || successor->parent != &unit) {
                continue;
            }

            if (std::find(
                    successor->predecessors.begin(),
                    successor->predecessors.end(),
                    block) == successor->predecessors.end()) {
                successor->predecessors.push_back(block);
            }
        }
    }
}

} // namespace

IRPassResult UnreachableBlockEliminationPass::run(
    CodeUnit& unit,
    IRPassContext& context) {
    (void)context;

    IRPassResult result;
    if (unit.entry_block == nullptr) {
        result.report(
            IRPassDiagnostic::Warning,
            "skip unreachable block elimination: entry block is null",
            unit.source_span);
        return result;
    }

    std::unordered_set<BasicBlock*> reachable;
    mark_reachable(unit.entry_block, unit, reachable);

    std::unordered_set<BasicBlock*> unreachable;
    for (const auto& block_ptr : unit.basic_blocks) {
        if (block_ptr != nullptr && reachable.find(block_ptr.get()) == reachable.end()) {
            unreachable.insert(block_ptr.get());
        }
    }

    if (unreachable.empty()) {
        return result;
    }

    std::unordered_set<ValueId> removed_values;
    if (has_reachable_use_of_removed_value(unit, reachable, unreachable, removed_values)) {
        result.report(
            IRPassDiagnostic::Warning,
            "skip unreachable block elimination: reachable code uses a value defined in an unreachable block",
            unit.source_span);
        return result;
    }

    for (const auto& block_ptr : unit.basic_blocks) {
        if (block_ptr == nullptr || reachable.find(block_ptr.get()) == reachable.end()) {
            continue;
        }

        auto& successors = block_ptr->successors;
        successors.erase(
            std::remove_if(
                successors.begin(),
                successors.end(),
                [&unreachable](BasicBlock* successor) {
                    return successor == nullptr ||
                        unreachable.find(successor) != unreachable.end();
                }),
            successors.end());
    }

    clear_removed_defs(unit, removed_values);
    unit.basic_blocks.erase(
        std::remove_if(
            unit.basic_blocks.begin(),
            unit.basic_blocks.end(),
            [&reachable](const std::unique_ptr<BasicBlock>& block) {
                return block == nullptr || reachable.find(block.get()) == reachable.end();
            }),
        unit.basic_blocks.end());

    rebuild_predecessors(unit);
    result.changed = true;
    return result;
}

} // namespace baltam
