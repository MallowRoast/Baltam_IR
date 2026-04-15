#include "optimizer/copy_propagation.h"

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "analysis/value_def.h"
#include "analysis/value_use.h"

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
    const analysis::ValueUseAnalysis::Result& uses =
        analysis_manager.get<analysis::ValueUseAnalysis>(function);
    std::unordered_map<ValueId, ValueRef> resolved_cache;
    bool changed = false;
    std::vector<SSACopyNode*> copies;

    for (const auto& block_ptr : function.blocks()) {
        BasicBlock* block = block_ptr.get();
        if (block == nullptr) {
            continue;
        }

        for (IRNode* node : block->instructions()) {
            auto* copy = dynamic_cast<SSACopyNode*>(node);
            if (copy != nullptr) {
                copies.push_back(copy);
                continue;
            }
        }
    }

    std::vector<SSACopyNode*> copies_to_erase;
    for (SSACopyNode* copy : copies) {
        const ValueRef resolved = resolve_copy_source(copy->src(), defs, resolved_cache);
        if (resolved.valid() && resolved.id != copy->src().id) {
            copy->set_src(resolved);
            changed = true;
        }

        for (const analysis::ValueUseAnalysis::Use& use : uses.uses_of(copy->result())) {
            if (use.replace_with(ValueRef{copy->result()}, resolved)) {
                changed = true;
            }
        }

        if (copy->src().valid() &&
            has_same_debug_name(function, copy->result(), copy->src().id)) {
            copies_to_erase.push_back(copy);
        }
    }

    for (SSACopyNode* copy : copies_to_erase) {
        BasicBlock* block = copy->parent();
        if (block == nullptr || !block->erase_instruction(copy)) {
            continue;
        }
        if (!function.erase_value(copy->result())) {
            continue;
        }
        changed = true;
    }

    return changed ? analysis::PreservedAnalyses::none() : analysis::PreservedAnalyses::all();
}

}  // namespace optimizer
}  // namespace baltam
