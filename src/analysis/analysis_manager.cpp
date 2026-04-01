#include "analysis/analysis_manager.h"

namespace baltam {
namespace analysis {

PreservedAnalyses PreservedAnalyses::all() {
    PreservedAnalyses preserved;
    preserved.preserve_all_ = true;
    return preserved;
}

PreservedAnalyses PreservedAnalyses::none() {
    return PreservedAnalyses{};
}

bool PreservedAnalyses::preserves_all() const {
    return preserve_all_;
}

void PreservedAnalyses::preserve_impl(std::type_index analysis_id) {
    if (preserve_all_) {
        return;
    }
    preserved_.insert(analysis_id);
}

bool PreservedAnalyses::is_preserved_impl(std::type_index analysis_id) const {
    return preserve_all_ || preserved_.find(analysis_id) != preserved_.end();
}

void FunctionAnalysisManager::invalidate(Function& function, const PreservedAnalyses& preserved) {
    if (preserved.preserves_all()) {
        return;
    }

    auto function_it = cache_.find(&function);
    if (function_it == cache_.end()) {
        return;
    }

    FunctionCache& function_cache = function_it->second;
    for (auto it = function_cache.begin(); it != function_cache.end();) {
        if (!preserved.is_preserved_impl(it->first)) {
            it = function_cache.erase(it);
            continue;
        }
        ++it;
    }

    if (function_cache.empty()) {
        cache_.erase(function_it);
    }
}

void FunctionAnalysisManager::invalidate_all(Function& function) {
    cache_.erase(&function);
}

void FunctionAnalysisManager::clear() {
    cache_.clear();
}

}  // namespace analysis
}  // namespace baltam
