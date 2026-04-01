#include "optimizer/pass_manager.h"

#include <iostream>

#include "analysis/verifier.h"

namespace baltam {
namespace optimizer {
namespace {

void print_function_ir_context(const Function& function) {
    (void)function;
    // Legacy value-based IR printing has been removed. Keep this hook as a
    // no-op until the optimizer pipeline is migrated to non-SSA / SSA IR.
}

}  // namespace

void FunctionPassManager::add_pass(std::unique_ptr<FunctionPass> pass) {
    if (pass == nullptr) {
        return;
    }
    passes_.push_back(std::move(pass));
}

std::size_t FunctionPassManager::pass_count() const {
    return passes_.size();
}

bool FunctionPassManager::empty() const {
    return passes_.empty();
}

void FunctionPassManager::run(Function& function, analysis::FunctionAnalysisManager& analysis_manager,
                              const PassManagerOptions& options) const {
    if (options.verify_before_pipeline) {
        analysis::verify_function_or_throw(function);
    }

    for (const std::unique_ptr<FunctionPass>& pass : passes_) {
        if (pass == nullptr) {
            continue;
        }

        if (options.print_before_each_pass) {
            std::cout << "[PassManager] Before `" << pass->name() << "` on function `"
                      << function.name() << "`:\n";
            print_function_ir_context(function);
        }

        analysis::PreservedAnalyses preserved = pass->run(function, analysis_manager);
        // `preserved` 描述的是“pass 执行后仍然有效的 analysis”。
        // 这里的 invalidate(...) 会删除未被保留的缓存项，而不是把
        // `preserved` 里声明仍有效的 analysis 一并失效。
        analysis_manager.invalidate(function, preserved);

        if (options.verify_after_each_pass) {
            analysis::verify_function_or_throw(function);
        }

        if (options.print_after_each_pass) {
            std::cout << "[PassManager] After `" << pass->name() << "` on function `"
                      << function.name() << "`:\n";
            print_function_ir_context(function);
        }
    }
}

}  // namespace optimizer
}  // namespace baltam
