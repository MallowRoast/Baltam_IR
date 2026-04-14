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
 * 当前默认 pipeline 会注册 `UntypedSSAConstantFoldPass`、
 * `UntypedSSADeadBranchEliminationPass`、`UntypedSSACopyPropagationPass`、
 * 第二轮 `UntypedSSAConstantFoldPass`、`UntypedSSADeadBranchEliminationPass`、
 * `UntypedSSACopyPropagationPass`、`UntypedSSADCEPass`
 * 和 `UntypedSSACFGSimplifyPass`：
 *
 * - 对 `UntypedSSA` 函数尝试折叠可静态求值的一元常量表达式
 * - 删除常量条件暴露出来的死分支与不可达块
 * - 折叠由 phi 降级和 SSA 赋值留下的 copy 链
 * - 再次折叠经过分支消解和复制传播后新暴露出来的常量表达式
 * - 再次删除因此变成常量条件的分支和 copy 链
 * - 删除常量折叠后暴露出来的无 uses 死节点
 * - 压平 DCE 后遗留的空跳板块与可合并线性块
 * - 对其他 stage 的函数保持 no-op
 */
void optimize_function(Function& function, const PassManagerOptions& options = {});

/**
 * @brief 在整个模块上运行默认构造的优化 pipeline。
 *
 * 当前默认 pipeline 会对模块中的每个函数运行
 * `UntypedSSAConstantFoldPass -> UntypedSSADeadBranchEliminationPass
 * -> UntypedSSACopyPropagationPass -> UntypedSSAConstantFoldPass
 * -> UntypedSSADeadBranchEliminationPass -> UntypedSSACopyPropagationPass
 * -> UntypedSSADCEPass -> UntypedSSACFGSimplifyPass`，
 * 并在模块/函数级按选项执行验证。
 */
void optimize_module(Module& module, const OptimizeOptions& options = {});

}  // namespace optimizer
}  // namespace baltam

#endif
