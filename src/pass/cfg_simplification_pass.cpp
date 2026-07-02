#include "pass/cfg_simplification_pass.h"

#include "pass/unreachable_block_elimination_pass.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace baltam {
namespace {

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

bool normalize_cfg_edges(CodeUnit& unit) {
    bool changed = false;
    std::vector<BasicBlock*> blocks;
    std::vector<std::vector<BasicBlock*>> old_successors;
    std::vector<std::vector<BasicBlock*>> old_predecessors;
    blocks.reserve(unit.basic_blocks.size());
    old_successors.reserve(unit.basic_blocks.size());
    old_predecessors.reserve(unit.basic_blocks.size());

    for (const auto& block_ptr : unit.basic_blocks) {
        if (block_ptr == nullptr) {
            continue;
        }

        BasicBlock& block = *block_ptr;
        blocks.push_back(&block);
        old_successors.push_back(block.successors);
        old_predecessors.push_back(block.predecessors);

        std::vector<BasicBlock*> normalized_successors;
        if (const Instruction* terminator = block.terminator()) {
            switch (terminator->type()) {
                case Instruction::Goto: {
                    const auto& go = static_cast<const GotoInst&>(*terminator);
                    if (go.target != nullptr && go.target->parent == &unit) {
                        normalized_successors.push_back(go.target);
                    }
                    break;
                }
                case Instruction::Branch: {
                    const auto& branch = static_cast<const BranchInst&>(*terminator);
                    if (branch.true_target != nullptr && branch.true_target->parent == &unit) {
                        normalized_successors.push_back(branch.true_target);
                    }
                    if (branch.false_target != nullptr &&
                        branch.false_target->parent == &unit &&
                        branch.false_target != branch.true_target) {
                        normalized_successors.push_back(branch.false_target);
                    }
                    break;
                }
                case Instruction::Return:
                    break;
                case Instruction::Const:
                case Instruction::LoadSlot:
                case Instruction::StoreSlot:
                case Instruction::GlobalDecl:
                case Instruction::PersistentDecl:
                case Instruction::CreateNamedFunctionHandle:
                case Instruction::CreateAnonymousFunctionHandle:
                case Instruction::Apply:
                case Instruction::ValueApply:
                case Instruction::MagicEnd:
                case Instruction::Call:
                case Instruction::Copy:
                case Instruction::Unary:
                case Instruction::Binary:
                    break;
            }
        }

        block.successors = std::move(normalized_successors);
    }

    rebuild_predecessors(unit);

    for (std::size_t index = 0; index < blocks.size(); ++index) {
        if (blocks[index]->successors != old_successors[index]) {
            changed = true;
        }
        if (blocks[index]->predecessors != old_predecessors[index]) {
            changed = true;
        }
    }

    return changed;
}

void unique_successors(BasicBlock& block) {
    std::vector<BasicBlock*> unique;
    unique.reserve(block.successors.size());
    for (BasicBlock* successor : block.successors) {
        if (successor == nullptr) {
            continue;
        }
        if (std::find(unique.begin(), unique.end(), successor) == unique.end()) {
            unique.push_back(successor);
        }
    }
    block.successors = std::move(unique);
}

bool is_empty_goto_block(const BasicBlock& block, const CodeUnit& unit) {
    if (&block == unit.entry_block || block.instructions.size() != 1U) {
        return false;
    }

    const Instruction* instruction = block.instructions.front().get();
    if (instruction == nullptr || instruction->type() != Instruction::Goto) {
        return false;
    }

    const auto& go = static_cast<const GotoInst&>(*instruction);
    return go.target != nullptr && go.target != &block && go.target->parent == &unit;
}

void replace_successor(
    BasicBlock& predecessor,
    BasicBlock& old_successor,
    BasicBlock& new_successor) {
    for (BasicBlock*& successor : predecessor.successors) {
        if (successor == &old_successor) {
            successor = &new_successor;
        }
    }
    unique_successors(predecessor);

    Instruction* terminator = predecessor.terminator();
    if (terminator == nullptr) {
        return;
    }

    switch (terminator->type()) {
        case Instruction::Goto: {
            auto& go = static_cast<GotoInst&>(*terminator);
            if (go.target == &old_successor) {
                go.target = &new_successor;
            }
            break;
        }
        case Instruction::Branch: {
            auto& branch = static_cast<BranchInst&>(*terminator);
            if (branch.true_target == &old_successor) {
                branch.true_target = &new_successor;
            }
            if (branch.false_target == &old_successor) {
                branch.false_target = &new_successor;
            }
            break;
        }
        case Instruction::Return:
        case Instruction::Const:
        case Instruction::LoadSlot:
        case Instruction::StoreSlot:
        case Instruction::GlobalDecl:
        case Instruction::PersistentDecl:
        case Instruction::CreateNamedFunctionHandle:
        case Instruction::CreateAnonymousFunctionHandle:
        case Instruction::Apply:
        case Instruction::ValueApply:
        case Instruction::MagicEnd:
        case Instruction::Call:
        case Instruction::Copy:
        case Instruction::Unary:
        case Instruction::Binary:
            break;
    }
}

bool remove_empty_goto_block(CodeUnit& unit, BasicBlock& block) {
    if (!is_empty_goto_block(block, unit)) {
        return false;
    }

    auto& go = static_cast<GotoInst&>(*block.instructions.front());
    BasicBlock* target = go.target;
    if (target == nullptr) {
        return false;
    }

    std::vector<BasicBlock*> predecessors = block.predecessors;
    for (BasicBlock* predecessor : predecessors) {
        if (predecessor == nullptr || predecessor == &block || predecessor->parent != &unit) {
            continue;
        }
        replace_successor(*predecessor, block, *target);
    }

    unit.basic_blocks.erase(
        std::remove_if(
            unit.basic_blocks.begin(),
            unit.basic_blocks.end(),
            [&block](const std::unique_ptr<BasicBlock>& candidate) {
                return candidate.get() == &block;
            }),
        unit.basic_blocks.end());

    rebuild_predecessors(unit);
    return true;
}

bool is_linear_merge_candidate(const BasicBlock& block, const CodeUnit& unit) {
    if (&block == unit.entry_block ||
        block.instructions.size() <= 1U ||
        block.predecessors.size() != 1U ||
        block.successors.size() != 1U) {
        return false;
    }

    BasicBlock* predecessor = block.predecessors.front();
    BasicBlock* successor = block.successors.front();
    if (predecessor == nullptr ||
        successor == nullptr ||
        predecessor == &block ||
        successor == &block ||
        predecessor == successor ||
        predecessor->parent != &unit ||
        successor->parent != &unit ||
        predecessor->instructions.empty()) {
        return false;
    }

    Instruction* predecessor_terminator = predecessor->terminator();
    if (predecessor_terminator == nullptr ||
        predecessor_terminator->type() != Instruction::Goto) {
        return false;
    }

    const auto& predecessor_goto = static_cast<const GotoInst&>(*predecessor_terminator);
    if (predecessor_goto.target != &block) {
        return false;
    }

    const Instruction* block_terminator = block.terminator();
    if (block_terminator == nullptr || block_terminator->type() != Instruction::Goto) {
        return false;
    }

    const auto& block_goto = static_cast<const GotoInst&>(*block_terminator);
    return block_goto.target == successor;
}

bool merge_linear_block(CodeUnit& unit, BasicBlock& block) {
    if (!is_linear_merge_candidate(block, unit)) {
        return false;
    }

    BasicBlock* predecessor = block.predecessors.front();
    if (predecessor == nullptr) {
        return false;
    }

    predecessor->instructions.pop_back();
    for (auto& inst_ptr : block.instructions) {
        if (inst_ptr != nullptr) {
            inst_ptr->parent = predecessor;
        }
        predecessor->instructions.push_back(std::move(inst_ptr));
    }
    block.instructions.clear();
    predecessor->successors = block.successors;
    unique_successors(*predecessor);

    unit.basic_blocks.erase(
        std::remove_if(
            unit.basic_blocks.begin(),
            unit.basic_blocks.end(),
            [&block](const std::unique_ptr<BasicBlock>& candidate) {
                return candidate.get() == &block;
            }),
        unit.basic_blocks.end());

    rebuild_predecessors(unit);
    return true;
}

void append_pass_result(IRPassResult& target, IRPassResult source) {
    target.changed = target.changed || source.changed;
    for (IRPassDiagnostic& diagnostic : source.diagnostics) {
        target.diagnostics.push_back(std::move(diagnostic));
    }
}

} // namespace

IRPassResult CFGSimplificationPass::run(CodeUnit& unit, IRPassContext& context) {
    IRPassResult result;
    result.changed = normalize_cfg_edges(unit);

    UnreachableBlockEliminationPass unreachable_block_elimination;
    append_pass_result(result, unreachable_block_elimination.run(unit, context));
    if (!result.ok()) {
        return result;
    }

    bool changed_this_iteration = true;
    while (changed_this_iteration) {
        changed_this_iteration = false;

        for (const auto& block_ptr : unit.basic_blocks) {
            if (block_ptr == nullptr || !is_empty_goto_block(*block_ptr, unit)) {
                continue;
            }

            BasicBlock* block = block_ptr.get();
            if (remove_empty_goto_block(unit, *block)) {
                result.changed = true;
                changed_this_iteration = true;
            }
            break;
        }

        if (changed_this_iteration) {
            continue;
        }

        for (const auto& block_ptr : unit.basic_blocks) {
            if (block_ptr == nullptr || !is_linear_merge_candidate(*block_ptr, unit)) {
                continue;
            }

            BasicBlock* block = block_ptr.get();
            if (merge_linear_block(unit, *block)) {
                result.changed = true;
                changed_this_iteration = true;
            }
            break;
        }
    }

    return result;
}

} // namespace baltam
