#include "Transform/pass_manager.h"
#include "Transform/mem2reg.h"
#include "Transform/load_store_elim.h"
#include "Transform/simplifycfg.h"

#include <stdexcept>

namespace baltam {

const CFGAnalysis& FunctionAnalysisManager::cfg() {
    const auto current = function_.current_version();
    if (version_ != current)
        invalidate(PreservedAnalyses::none());
    if (!cfg_) {
        version_ = current;
        cfg_ = std::make_unique<CFGAnalysis>(*version_);
    }
    return *cfg_;
}

const DominatorAnalysis& FunctionAnalysisManager::dominators() {
    const auto& graph = cfg();
    if (!dominators_)
        dominators_ = std::make_unique<DominatorAnalysis>(graph);
    return *dominators_;
}

void FunctionAnalysisManager::invalidate(PreservedAnalyses preserved) {
    if (preserved.preserves_cfg && version_ == function_.current_version())
        return;
    dominators_.reset();
    cfg_.reset();
    version_.reset();
}

void FunctionPassManager::run(FunctionUnit& function) const {
    FunctionAnalysisManager analyses(function);
    for (const auto& pass : passes_)
        analyses.invalidate(pass.function(function, analyses));
}

void FunctionPassManager::run(CommandUnit& command) const {
    for (const auto& pass : passes_)
        pass.command(command);
}

void FunctionPassManager::run(IRModule& module) const {
    if (auto* command = module.command_unit())
        run(*command);
    std::vector<FunctionUnit*> pending;
    for (auto it = module.functions().rbegin(); it != module.functions().rend(); ++it)
        pending.push_back(it->get());
    while (!pending.empty()) {
        auto* function = pending.back();
        pending.pop_back();
        run(*function);
        for (auto it = function->nested_functions().rbegin();
             it != function->nested_functions().rend(); ++it)
            pending.push_back(it->get());
    }
}

FunctionPassManager build_pass_pipeline(std::string_view pipeline) {
    const auto trim = [](std::string_view text) {
        const auto first = text.find_first_not_of(" \t\r\n");
        if (first == text.npos)
            return std::string_view{};
        return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
    };
    pipeline = trim(pipeline);
    if (pipeline.substr(0, 9) == "function(") {
        if (pipeline.back() != ')')
            throw std::invalid_argument("函数 pass 管线缺少右括号");
        pipeline = trim(pipeline.substr(9, pipeline.size() - 10));
    }
    FunctionPassManager manager;
    while (!pipeline.empty()) {
        const auto comma = pipeline.find(',');
        const auto name = trim(pipeline.substr(0, comma));
        if (name == "mem2reg")
            manager.add_pass(Mem2RegPass{});
        else if (name == "load-store-elim")
            manager.add_pass(LoadStoreElimPass{});
        else if (name == "simplifycfg")
            manager.add_pass(SimplifyCFGPass{});
        else
            throw std::invalid_argument("未知的优化 pass：" + std::string(name));
        if (comma == pipeline.npos)
            break;
        pipeline = trim(pipeline.substr(comma + 1));
        if (pipeline.empty())
            throw std::invalid_argument("逗号之后缺少优化 pass");
    }
    return manager;
}

} // baltam 命名空间
