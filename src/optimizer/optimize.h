#ifndef BALTAM_IR_OPTIMIZER_OPTIMIZE_H
#define BALTAM_IR_OPTIMIZER_OPTIMIZE_H

#include "analysis/analysis_manager.h"
#include "optimizer/pass_manager.h"

namespace baltam {
namespace optimizer {

/**
 * @brief 控制模块级优化入口行为的选项。
 *
 * 该结构在 `PassManagerOptions` 之上，再补充模块级前后验证开关。
 */
struct OptimizeOptions {
    /**
     * @brief 透传给函数级 pass manager 的运行选项。
     */
    PassManagerOptions pass_manager_options;

    /**
     * @brief 是否在模块级 pipeline 开始前先验证整个模块 IR。
     */
    bool verify_module_before_pipeline = true;

    /**
     * @brief 是否在模块级 pipeline 结束后再次验证整个模块 IR。
     */
    bool verify_module_after_pipeline = true;
};

/**
 * @brief 在单个函数上运行给定的 pass manager。
 *
 * 该重载用于显式传入外部构建好的 `FunctionPassManager` 和
 * `FunctionAnalysisManager`，便于调用方复用 pipeline 与 analysis 缓存。
 */
void optimize_function(Function& function, FunctionPassManager& pass_manager,
                       analysis::FunctionAnalysisManager& analysis_manager,
                       const PassManagerOptions& options = {});

/**
 * @brief 在模块中的所有函数上运行给定的 pass manager。
 *
 * 该重载会遍历模块内所有函数，并在它们之间共享同一个
 * `FunctionAnalysisManager` 实例。
 */
void optimize_module(Module& module, FunctionPassManager& pass_manager,
                     analysis::FunctionAnalysisManager& analysis_manager,
                     const OptimizeOptions& options = {});

/**
 * @brief 在单个函数上运行默认构造的优化 pipeline。
 *
 * 当前默认 pipeline 只注册 `UntypedSSAConstantFoldPass`：
 *
 * - 对 `UntypedSSA` 函数尝试折叠可静态求值的一元常量表达式
 * - 对其他 stage 的函数保持 no-op
 */
void optimize_function(Function& function, const PassManagerOptions& options = {});

/**
 * @brief 在整个模块上运行默认构造的优化 pipeline。
 *
 * 当前默认 pipeline 会对模块中的每个函数运行
 * `UntypedSSAConstantFoldPass`，并在模块/函数级按选项执行验证。
 */
void optimize_module(Module& module, const OptimizeOptions& options = {});

}  // namespace optimizer
}  // namespace baltam

#endif
