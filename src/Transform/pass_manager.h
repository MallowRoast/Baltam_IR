#pragma once

#include "Analysis/ir_analysis.h"

#include <functional>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace baltam {

// 当前已实现的分析均只依赖 CFG；保留状态与变换是否发生相互独立。
struct PreservedAnalyses {
    bool preserves_cfg = false;

    static PreservedAnalyses all() { return {true}; }
    static PreservedAnalyses none() { return {false}; }
};

class FunctionAnalysisManager {
public:
    explicit FunctionAnalysisManager(FunctionUnit& function) : function_(function) {}

    const CFGAnalysis& cfg();
    const DominatorAnalysis& dominators();
    void invalidate(PreservedAnalyses preserved);

private:
    FunctionUnit& function_;
    std::shared_ptr<const IRStageVersion> version_;
    std::unique_ptr<CFGAnalysis> cfg_;
    std::unique_ptr<DominatorAnalysis> dominators_;
};

class FunctionPassManager {
public:
    template <class Pass> void add_pass(Pass pass) {
        passes_.emplace_back([pass = std::move(pass)](FunctionUnit& function,
                                                      FunctionAnalysisManager& analyses) mutable {
            return pass.run(function, analyses);
        });
    }

    void run(FunctionUnit& function) const;
    void run(IRModule& module) const;

private:
    std::vector<std::function<PreservedAnalyses(FunctionUnit&, FunctionAnalysisManager&)>> passes_;
};

// 当前支持逗号分隔的函数管线，以及 function(...) 适配语法。
FunctionPassManager build_pass_pipeline(std::string_view pipeline);

} // baltam 命名空间
