#include "optimizer/optimize.h"

#include <memory>

#include "analysis/verifier.h"
#include "optimizer/constant_fold.h"

namespace baltam {
namespace optimizer {

void optimize_function(Function& function, FunctionPassManager& pass_manager,
                       analysis::FunctionAnalysisManager& analysis_manager,
                       const PassManagerOptions& options) {
    pass_manager.run(function, analysis_manager, options);
}

void optimize_module(Module& module, FunctionPassManager& pass_manager,
                     analysis::FunctionAnalysisManager& analysis_manager,
                     const OptimizeOptions& options) {
    if (options.verify_module_before_pipeline) {
        analysis::verify_module_or_throw(module);
    }

    for (const auto& function : module.functions()) {
        if (function == nullptr) {
            continue;
        }
        optimize_function(*function, pass_manager, analysis_manager, options.pass_manager_options);
    }

    if (options.verify_module_after_pipeline) {
        analysis::verify_module_or_throw(module);
    }
}

void optimize_function(Function& function, const PassManagerOptions& options) {
    analysis::FunctionAnalysisManager analysis_manager;
    FunctionPassManager pass_manager;
    pass_manager.add_pass(std::make_unique<UntypedSSAConstantFoldPass>());
    optimize_function(function, pass_manager, analysis_manager, options);
}

void optimize_module(Module& module, const OptimizeOptions& options) {
    analysis::FunctionAnalysisManager analysis_manager;
    FunctionPassManager pass_manager;
    pass_manager.add_pass(std::make_unique<UntypedSSAConstantFoldPass>());
    optimize_module(module, pass_manager, analysis_manager, options);
}

}  // namespace optimizer
}  // namespace baltam
