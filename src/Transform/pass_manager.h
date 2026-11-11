#pragma once

#include "Analysis/ir_analysis.h"

#include <functional>
#include <memory>
#include <string_view>
#include <utility>
#include <type_traits>
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

template <class Pass, class = void> struct SupportsCommandPass : std::false_type {};
template <class Pass>
struct SupportsCommandPass<
    Pass, std::void_t<decltype(std::declval<Pass&>().run(std::declval<CommandUnit&>()))>>
    : std::true_type {};

class FunctionPassManager {
public:
    template <class Pass> void add_pass(Pass pass) {
        auto implementation = std::make_shared<Pass>(std::move(pass));
        passes_.push_back(
            {[implementation](FunctionUnit& function, FunctionAnalysisManager& analyses) {
                 return implementation->run(function, analyses);
             },
             [implementation](CommandUnit& command) {
                 if constexpr (SupportsCommandPass<Pass>::value)
                     implementation->run(command);
             }});
    }

    void run(FunctionUnit& function) const;
    void run(CommandUnit& command) const;
    void run(IRModule& module) const;

private:
    struct PassEntry {
        std::function<PreservedAnalyses(FunctionUnit&, FunctionAnalysisManager&)> function;
        std::function<void(CommandUnit&)> command;
    };
    std::vector<PassEntry> passes_;
};

// 当前支持逗号分隔的函数管线，以及 function(...) 适配语法。
FunctionPassManager build_pass_pipeline(std::string_view pipeline);

} // baltam 命名空间
