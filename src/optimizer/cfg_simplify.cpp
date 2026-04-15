#include "optimizer/cfg_simplify.h"

#include <stdexcept>
#include <vector>

namespace baltam {
namespace optimizer {
namespace {

void rewrite_successor_phis(BasicBlock* from, BasicBlock* to,
                            const std::vector<BasicBlock*>& successors) {
    for (BasicBlock* successor : successors) {
        if (successor == nullptr) {
            continue;
        }
        for (IRNode* phi_node : successor->phi_nodes()) {
            auto* phi = dynamic_cast<SSAPhiNode*>(phi_node);
            if (phi == nullptr) {
                throw std::runtime_error("CFG 简化失败：phi 区域中遇到非 SSAPhiNode。");
            }
            if (!phi->replace_predecessor(from, to)) {
                throw std::runtime_error("CFG 简化失败：更新 phi incoming 前驱失败。");
            }
        }
    }
}

// 删除只包含无条件跳转的中间块，并把唯一前驱直接改写为跳到目标块。
bool try_simplify_jump_only_block(Function& function, BasicBlock* block) {
    if (block == nullptr || block == function.entry_block()) {
        return false;
    }
    if (!block->phi_nodes().empty() || !block->instructions().empty()) {
        return false;
    }

    auto* block_jump = dynamic_cast<SSAJumpNode*>(block->terminal());
    if (block_jump == nullptr) {
        return false;
    }

    BasicBlock* target = block_jump->target();
    if (target == nullptr || target == block) {
        return false;
    }

    if (block->predecessors().size() != 1) {
        return false;
    }

    BasicBlock* predecessor = block->predecessors().front();
    if (predecessor == nullptr || predecessor == target) {
        return false;
    }

    auto* predecessor_jump = dynamic_cast<SSAJumpNode*>(predecessor->terminal());
    if (predecessor_jump == nullptr || predecessor_jump->target() != block) {
        return false;
    }

    rewrite_successor_phis(block, predecessor, {target});

    predecessor->remove_successor(block);
    predecessor->add_successor(target);
    predecessor->set_terminal(
        function.create_node<SSAJumpNode>(target, predecessor_jump->source_location()));

    if (!function.erase_block(block)) {
        throw std::runtime_error("CFG 简化失败：删除跳板块失败。");
    }

    return true;
}

// 当一个块只跳到唯一后继，且该后继也只有这一个前驱时，将两者合并成单个线性块。
bool try_merge_linear_successor_block(Function& function, BasicBlock* block) {
    if (block == nullptr) {
        return false;
    }

    auto* predecessor_jump = dynamic_cast<SSAJumpNode*>(block->terminal());
    if (predecessor_jump == nullptr || block->successors().size() != 1) {
        return false;
    }

    BasicBlock* successor = predecessor_jump->target();
    if (successor == nullptr || successor == block) {
        return false;
    }

    if (successor->predecessors().size() != 1 || successor->predecessors().front() != block) {
        return false;
    }
    if (!successor->phi_nodes().empty()) {
        return false;
    }

    std::vector<BasicBlock*> successor_successors = successor->successors();
    rewrite_successor_phis(successor, block, successor_successors);

    block->remove_successor(successor);
    for (BasicBlock* next : successor_successors) {
        block->add_successor(next);
    }

    for (IRNode* instruction : successor->release_instructions()) {
        block->append_instruction(instruction);
    }

    IRNode* successor_terminal = successor->release_terminal();
    if (successor_terminal == nullptr) {
        throw std::runtime_error("CFG 简化失败：待合并块缺少终结节点。");
    }
    block->set_terminal(successor_terminal);

    if (!function.erase_block(successor)) {
        throw std::runtime_error("CFG 简化失败：删除已合并的后继块失败。");
    }

    return true;
}

}  // namespace

const char* UntypedSSACFGSimplifyPass::name() const {
    return "untyped-ssa-cfg-simplify";
}

analysis::PreservedAnalyses UntypedSSACFGSimplifyPass::run(
    Function& function, analysis::FunctionAnalysisManager& analysis_manager) {
    (void)analysis_manager;

    if (function.stage() != IRNode::UntypedSSA) {
        return analysis::PreservedAnalyses::all();
    }

    bool changed = false;
    bool local_changed = false;
    do {
        local_changed = false;

        std::vector<BasicBlock*> blocks;
        blocks.reserve(function.blocks().size());
        for (const auto& block : function.blocks()) {
            if (block != nullptr) {
                blocks.push_back(block.get());
            }
        }

        for (BasicBlock* block : blocks) {
            if (try_simplify_jump_only_block(function, block)) {
                changed = true;
                local_changed = true;
                break;
            }
            if (try_merge_linear_successor_block(function, block)) {
                changed = true;
                local_changed = true;
                break;
            }
        }
    } while (local_changed);

    return changed ? analysis::PreservedAnalyses::none() : analysis::PreservedAnalyses::all();
}

}  // namespace optimizer
}  // namespace baltam
