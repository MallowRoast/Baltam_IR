#include "optimizer/copy_propagation.h"

#include <unordered_map>
#include <unordered_set>

#include "analysis/value_def.h"

namespace baltam {
namespace optimizer {
namespace {

ValueRef resolve_copy_source(ValueRef value, const analysis::ValueDefAnalysis::Result& defs,
                             std::unordered_map<ValueId, ValueRef>& cache,
                             std::unordered_set<ValueId>& visiting) {
    if (!value.valid()) {
        return value;
    }

    auto cache_it = cache.find(value.id);
    if (cache_it != cache.end()) {
        return cache_it->second;
    }

    if (!visiting.insert(value.id).second) {
        cache.emplace(value.id, value);
        return value;
    }

    ValueRef resolved = value;
    const UntypedSSANode* def = defs.definition_of(value.id);
    const auto* copy = def != nullptr ? dynamic_cast<const SSACopyNode*>(def) : nullptr;
    if (copy != nullptr && copy->src().valid()) {
        resolved = resolve_copy_source(copy->src(), defs, cache, visiting);
    }

    visiting.erase(value.id);
    cache.emplace(value.id, resolved);
    return resolved;
}

ValueRef resolve_copy_source(ValueRef value, const analysis::ValueDefAnalysis::Result& defs,
                             std::unordered_map<ValueId, ValueRef>& cache) {
    std::unordered_set<ValueId> visiting;
    return resolve_copy_source(value, defs, cache, visiting);
}

bool has_same_debug_name(const Function& function, ValueId lhs, ValueId rhs) {
    const std::string* lhs_name = function.find_value_debug_name(lhs);
    const std::string* rhs_name = function.find_value_debug_name(rhs);
    return lhs_name != nullptr && rhs_name != nullptr && !lhs_name->empty() && *lhs_name == *rhs_name;
}

}  // namespace

const char* UntypedSSACopyPropagationPass::name() const {
    return "untyped-ssa-copy-propagation";
}

analysis::PreservedAnalyses UntypedSSACopyPropagationPass::run(
    Function& function, analysis::FunctionAnalysisManager& analysis_manager) {
    if (function.stage() != IRNode::UntypedSSA) {
        return analysis::PreservedAnalyses::all();
    }

    const analysis::ValueDefAnalysis::Result& defs =
        analysis_manager.get<analysis::ValueDefAnalysis>(function);
    std::unordered_map<ValueId, ValueRef> resolved_cache;
    bool changed = false;

    for (const auto& block_ptr : function.blocks()) {
        BasicBlock* block = block_ptr.get();
        if (block == nullptr) {
            continue;
        }

        for (IRNode* phi_node : block->phi_nodes()) {
            auto* phi = dynamic_cast<SSAPhiNode*>(phi_node);
            if (phi == nullptr) {
                continue;
            }

            const std::vector<SSAPhiNode::Incoming> incomings = phi->incomings();
            for (const SSAPhiNode::Incoming& incoming : incomings) {
                const ValueRef resolved = resolve_copy_source(incoming.value, defs, resolved_cache);
                if (resolved.valid() && resolved.id != incoming.value.id &&
                    phi->replace_value(incoming.value, resolved)) {
                    changed = true;
                }
            }
        }

        std::vector<IRNode*> copies_to_erase;
        for (IRNode* node : block->instructions()) {
            auto* copy = dynamic_cast<SSACopyNode*>(node);
            if (copy != nullptr) {
                const ValueRef resolved = resolve_copy_source(copy->src(), defs, resolved_cache);
                if (resolved.valid() && resolved.id != copy->src().id) {
                    copy->set_src(resolved);
                    changed = true;
                }
                if (copy->src().valid() && has_same_debug_name(function, copy->result(), copy->src().id)) {
                    copies_to_erase.push_back(copy);
                }
                continue;
            }

            if (auto* store = dynamic_cast<SSAGlobalStoreNode*>(node)) {
                const ValueRef resolved = resolve_copy_source(store->value(), defs, resolved_cache);
                if (resolved.valid() && resolved.id != store->value().id) {
                    store->set_value(resolved);
                    changed = true;
                }
                continue;
            }

            if (auto* unary = dynamic_cast<SSAUnaryOpNode*>(node)) {
                const ValueRef resolved = resolve_copy_source(unary->operand(), defs, resolved_cache);
                if (resolved.valid() && resolved.id != unary->operand().id) {
                    unary->set_operand(resolved);
                    changed = true;
                }
                continue;
            }

            if (auto* binary = dynamic_cast<SSABinOpNode*>(node)) {
                const ValueRef lhs = resolve_copy_source(binary->lhs(), defs, resolved_cache);
                const ValueRef rhs = resolve_copy_source(binary->rhs(), defs, resolved_cache);
                if (lhs.valid() && lhs.id != binary->lhs().id) {
                    binary->set_lhs(lhs);
                    changed = true;
                }
                if (rhs.valid() && rhs.id != binary->rhs().id) {
                    binary->set_rhs(rhs);
                    changed = true;
                }
                continue;
            }

            if (auto* call = dynamic_cast<SSACallNode*>(node)) {
                if (call->callee().type == SSACallNode::Callee::Indirect) {
                    const ValueRef callee =
                        resolve_copy_source(call->callee().indirect_value, defs, resolved_cache);
                    if (callee.valid() && callee.id != call->callee().indirect_value.id) {
                        call->set_callee_indirect_value(callee);
                        changed = true;
                    }
                }
                for (std::size_t i = 0; i < call->inputs().size(); ++i) {
                    const ValueRef resolved =
                        resolve_copy_source(call->inputs()[i], defs, resolved_cache);
                    if (resolved.valid() && resolved.id != call->inputs()[i].id) {
                        call->set_input(i, resolved);
                        changed = true;
                    }
                }
                continue;
            }
        }

        for (IRNode* node : copies_to_erase) {
            auto* copy = static_cast<SSACopyNode*>(node);
            if (!block->erase_instruction(copy)) {
                continue;
            }
            if (!function.erase_value(copy->result())) {
                continue;
            }
            changed = true;
        }

        if (auto* cond_jump = dynamic_cast<SSACondJumpNode*>(block->terminal())) {
            const ValueRef resolved = resolve_copy_source(cond_jump->cond(), defs, resolved_cache);
            if (resolved.valid() && resolved.id != cond_jump->cond().id) {
                cond_jump->set_cond(resolved);
                changed = true;
            }
        } else if (auto* ret = dynamic_cast<SSAReturnNode*>(block->terminal())) {
            for (std::size_t i = 0; i < ret->values().size(); ++i) {
                const ValueRef resolved = resolve_copy_source(ret->values()[i], defs, resolved_cache);
                if (resolved.valid() && resolved.id != ret->values()[i].id) {
                    ret->set_value(i, resolved);
                    changed = true;
                }
            }
        }
    }

    return changed ? analysis::PreservedAnalyses::none() : analysis::PreservedAnalyses::all();
}

}  // namespace optimizer
}  // namespace baltam
