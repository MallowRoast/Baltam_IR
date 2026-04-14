#include "optimizer/dead_branch_elimination.h"

#include <optional>
#include <stdexcept>
#include <unordered_set>
#include <vector>

#include "analysis/value_def.h"

namespace baltam {
namespace optimizer {
namespace {

using NumberValue = SSANumberNode::NumberValue;

std::vector<ValueId> collect_node_defs(const UntypedSSANode& node) {
    switch (node.type()) {
        case UntypedSSANode::SSA_Number:
            return {static_cast<const SSANumberNode&>(node).result()};
        case UntypedSSANode::SSA_Text:
            return {static_cast<const SSATextNode&>(node).result()};
        case UntypedSSANode::SSA_Undef:
            return {static_cast<const SSAUndefNode&>(node).result()};
        case UntypedSSANode::SSA_Phi:
            return {static_cast<const SSAPhiNode&>(node).result()};
        case UntypedSSANode::SSA_Copy:
            return {static_cast<const SSACopyNode&>(node).result()};
        case UntypedSSANode::SSA_GlobalLoad:
            return {static_cast<const SSAGlobalLoadNode&>(node).result()};
        case UntypedSSANode::SSA_UnaryOp:
            return {static_cast<const SSAUnaryOpNode&>(node).result()};
        case UntypedSSANode::SSA_BinOp:
            return {static_cast<const SSABinOpNode&>(node).result()};
        case UntypedSSANode::SSA_Call:
            return static_cast<const SSACallNode&>(node).results();
        case UntypedSSANode::SSA_GlobalStore:
        case UntypedSSANode::SSA_CondJump:
        case UntypedSSANode::SSA_Jump:
        case UntypedSSANode::SSA_Return:
            return {};
    }

    return {};
}

void erase_node_defs(Function& function, const UntypedSSANode& node) {
    for (ValueId value_id : collect_node_defs(node)) {
        if (!function.erase_value(value_id)) {
            throw std::runtime_error("死分支消除失败：删除不可达块定义的 SSA 值失败。");
        }
    }
}

std::optional<NumberValue> try_resolve_number_value(
    ValueRef value, const analysis::ValueDefAnalysis::Result& defs,
    std::unordered_set<ValueId>& visiting) {
    if (!value.valid() || !visiting.insert(value.id).second) {
        return std::nullopt;
    }

    const UntypedSSANode* def = defs.definition_of(value.id);
    if (def == nullptr) {
        visiting.erase(value.id);
        return std::nullopt;
    }

    std::optional<NumberValue> resolved;
    switch (def->type()) {
        case UntypedSSANode::SSA_Number:
            resolved = static_cast<const SSANumberNode*>(def)->value();
            break;
        case UntypedSSANode::SSA_Copy:
            resolved = try_resolve_number_value(static_cast<const SSACopyNode*>(def)->src(), defs,
                                                visiting);
            break;
        default:
            resolved = std::nullopt;
            break;
    }

    visiting.erase(value.id);
    return resolved;
}

std::optional<bool> try_resolve_constant_branch_condition(
    ValueRef condition, const analysis::ValueDefAnalysis::Result& defs) {
    std::unordered_set<ValueId> visiting;
    const std::optional<NumberValue> value = try_resolve_number_value(condition, defs, visiting);
    if (!value.has_value() || !std::holds_alternative<bool>(*value)) {
        return std::nullopt;
    }
    return std::get<bool>(*value);
}

void mark_reachable(BasicBlock* block, std::unordered_set<BasicBlock*>& reachable) {
    if (block == nullptr || !reachable.insert(block).second) {
        return;
    }

    for (BasicBlock* successor : block->successors()) {
        mark_reachable(successor, reachable);
    }
}

IRNode* build_phi_replacement(Function& function, const SSAPhiNode& phi,
                              ValueRef incoming_value) {
    return function.create_node<SSACopyNode>(phi.result(), incoming_value, phi.source_location());
}

}  // namespace

const char* UntypedSSADeadBranchEliminationPass::name() const {
    return "untyped-ssa-dead-branch-elimination";
}

analysis::PreservedAnalyses UntypedSSADeadBranchEliminationPass::run(
    Function& function, analysis::FunctionAnalysisManager& analysis_manager) {
    if (function.stage() != IRNode::UntypedSSA) {
        return analysis::PreservedAnalyses::all();
    }

    const analysis::ValueDefAnalysis::Result& defs =
        analysis_manager.get<analysis::ValueDefAnalysis>(function);
    bool changed = false;

    for (const auto& block_ptr : function.blocks()) {
        BasicBlock* block = block_ptr.get();
        if (block == nullptr) {
            continue;
        }

        auto* cond_jump = dynamic_cast<SSACondJumpNode*>(block->terminal());
        if (cond_jump == nullptr) {
            continue;
        }

        const std::optional<bool> folded_condition =
            try_resolve_constant_branch_condition(cond_jump->cond(), defs);
        if (!folded_condition.has_value()) {
            continue;
        }

        BasicBlock* live_target =
            *folded_condition ? cond_jump->true_block() : cond_jump->false_block();
        BasicBlock* dead_target =
            *folded_condition ? cond_jump->false_block() : cond_jump->true_block();
        if (live_target != dead_target) {
            block->remove_successor(dead_target);
        }
        block->set_terminal(function.create_node<SSAJumpNode>(live_target, cond_jump->source_location()));
        changed = true;
    }

    std::unordered_set<BasicBlock*> reachable_blocks;
    mark_reachable(function.entry_block(), reachable_blocks);

    std::vector<BasicBlock*> unreachable_blocks;
    unreachable_blocks.reserve(function.blocks().size());
    for (const auto& block_ptr : function.blocks()) {
        BasicBlock* block = block_ptr.get();
        if (block != nullptr && reachable_blocks.find(block) == reachable_blocks.end()) {
            unreachable_blocks.push_back(block);
        }
    }

    for (BasicBlock* block : unreachable_blocks) {
        for (IRNode* phi_node : block->phi_nodes()) {
            erase_node_defs(function, static_cast<const UntypedSSANode&>(*phi_node));
        }
        for (IRNode* instruction : block->instructions()) {
            erase_node_defs(function, static_cast<const UntypedSSANode&>(*instruction));
        }
        if (!function.erase_block(block)) {
            throw std::runtime_error("死分支消除失败：删除不可达基本块失败。");
        }
        changed = true;
    }

    for (const auto& block_ptr : function.blocks()) {
        BasicBlock* block = block_ptr.get();
        if (block == nullptr) {
            continue;
        }

        const std::vector<IRNode*> phi_nodes = block->phi_nodes();
        for (IRNode* phi_node : phi_nodes) {
            auto* phi = dynamic_cast<SSAPhiNode*>(phi_node);
            if (phi == nullptr) {
                continue;
            }

            const std::vector<SSAPhiNode::Incoming> incomings = phi->incomings();
            for (const SSAPhiNode::Incoming& incoming : incomings) {
                if (incoming.predecessor == nullptr ||
                    !BasicBlock::contains_block(block->predecessors(), incoming.predecessor)) {
                    if (phi->remove_incoming(incoming.predecessor)) {
                        changed = true;
                    }
                }
            }

            if (phi->incomings().size() == 1 && block->predecessors().size() == 1) {
                IRNode* replacement =
                    build_phi_replacement(function, *phi, phi->incomings().front().value);
                if (!block->erase_phi(phi)) {
                    throw std::runtime_error("死分支消除失败：删除单 incoming phi 失败。");
                }
                block->prepend_instruction(replacement);
                changed = true;
            }
        }
    }

    return changed ? analysis::PreservedAnalyses::none() : analysis::PreservedAnalyses::all();
}

}  // namespace optimizer
}  // namespace baltam
