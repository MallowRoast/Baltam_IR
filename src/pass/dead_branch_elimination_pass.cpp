#include "pass/dead_branch_elimination_pass.h"

#include "ba_obj/ba_obj.h"

#include <algorithm>
#include <exception>
#include <memory>
#include <optional>
#include <variant>

namespace baltam {
namespace {

[[nodiscard]] std::optional<ba_obj_ptr> constant_to_ba_obj(const Constant& constant) {
    if (const auto* value = std::get_if<LogicalConstant>(&constant)) {
        return std::make_shared<ba_obj>(value->value);
    }
    if (const auto* value = std::get_if<Int64Constant>(&constant)) {
        return std::make_shared<ba_obj>(value->value);
    }
    if (const auto* value = std::get_if<UInt64Constant>(&constant)) {
        return std::make_shared<ba_obj>(value->value);
    }
    if (const auto* value = std::get_if<Float64Constant>(&constant)) {
        return std::make_shared<ba_obj>(value->value);
    }
    if (const auto* value = std::get_if<RuntimeObjectConstant>(&constant)) {
        if (value->value == nullptr) {
            return std::nullopt;
        }
        return std::make_shared<ba_obj>(*value->value);
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<bool> constant_to_bool(const Constant& constant) {
    std::optional<ba_obj_ptr> obj = constant_to_ba_obj(constant);
    if (!obj.has_value() || *obj == nullptr) {
        return std::nullopt;
    }

    try {
        return (*obj)->as_bool();
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

[[nodiscard]] const ConstInst* const_def_for_operand(
    const CodeUnit& unit,
    const Operand& operand) {
    const auto* value = std::get_if<ValueId>(&operand);
    if (value == nullptr) {
        return nullptr;
    }

    const ValueInfo* value_info = unit.value_table.find(*value);
    if (value_info == nullptr ||
        value_info->def == nullptr ||
        value_info->def->type() != Instruction::Const) {
        return nullptr;
    }

    return static_cast<const ConstInst*>(value_info->def);
}

[[nodiscard]] BasicBlock* chosen_target_for_branch(
    const CodeUnit& unit,
    const BranchInst& inst) {
    const ConstInst* condition_const = const_def_for_operand(unit, inst.condition);
    if (condition_const == nullptr) {
        return nullptr;
    }

    const std::optional<bool> condition = constant_to_bool(condition_const->value);
    if (!condition.has_value()) {
        return nullptr;
    }

    BasicBlock* target = *condition ? inst.true_target : inst.false_target;
    if (target == nullptr || target->parent != &unit) {
        return nullptr;
    }

    return target;
}

void remove_predecessor(BasicBlock& block, BasicBlock* predecessor) {
    if (predecessor == nullptr) {
        return;
    }

    auto& preds = block.predecessors;
    preds.erase(
        std::remove(preds.begin(), preds.end(), predecessor),
        preds.end());
}

void add_predecessor_if_missing(BasicBlock& block, BasicBlock* predecessor) {
    if (predecessor == nullptr) {
        return;
    }

    if (std::find(block.predecessors.begin(), block.predecessors.end(), predecessor) ==
        block.predecessors.end()) {
        block.predecessors.push_back(predecessor);
    }
}

bool rewrite_branch_to_goto(BasicBlock& block, BranchInst& branch, BasicBlock& target) {
    BasicBlock* old_true_target = branch.true_target;
    BasicBlock* old_false_target = branch.false_target;

    auto go = std::make_unique<GotoInst>();
    go->parent = &block;
    go->source_span = branch.source_span;
    go->attrs = branch.attrs;
    go->target = &target;
    block.instructions.back() = std::move(go);

    block.successors.clear();
    block.successors.push_back(&target);

    if (old_true_target != nullptr && old_true_target != &target) {
        remove_predecessor(*old_true_target, &block);
    }
    if (old_false_target != nullptr &&
        old_false_target != &target &&
        old_false_target != old_true_target) {
        remove_predecessor(*old_false_target, &block);
    }

    add_predecessor_if_missing(target, &block);
    return true;
}

} // namespace

IRPassResult DeadBranchEliminationPass::run(CodeUnit& unit, IRPassContext& context) {
    (void)context;

    IRPassResult result;
    bool changed = false;

    for (const auto& block_ptr : unit.basic_blocks) {
        if (block_ptr == nullptr) {
            continue;
        }

        BasicBlock& block = *block_ptr;
        Instruction* terminator = block.terminator();
        if (terminator == nullptr || terminator->type() != Instruction::Branch) {
            continue;
        }

        auto& branch = static_cast<BranchInst&>(*terminator);
        BasicBlock* target = chosen_target_for_branch(unit, branch);
        if (target == nullptr) {
            continue;
        }

        if (rewrite_branch_to_goto(block, branch, *target)) {
            changed = true;
        }
    }

    result.changed = changed;
    return result;
}

} // namespace baltam
