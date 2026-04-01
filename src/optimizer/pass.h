#ifndef BALTAM_IR_OPTIMIZER_PASS_H
#define BALTAM_IR_OPTIMIZER_PASS_H

#include "analysis/analysis_manager.h"

namespace baltam {
namespace optimizer {

/**
 * @brief 所有函数级优化 pass 的抽象基类。
 *
 * 当前优化框架参考 LLVM 新 PassManager 的思路，把 pass 建模为：
 *
 * - 输入一个 `Function`
 * - 通过 `FunctionAnalysisManager` 获取所需 analysis
 * - 返回一份 `PreservedAnalyses`，声明哪些 analysis 结果仍然有效
 *
 * 具体 pass 只负责自己的 IR 变换逻辑，不直接管理 analysis 缓存失效。
 */
class FunctionPass {
public:
    /**
     * @brief 虚析构，允许通过基类指针安全释放具体 pass。
     */
    virtual ~FunctionPass() = default;

    /**
     * @brief 返回 pass 的稳定名称，用于日志、调试输出和诊断。
     */
    virtual const char* name() const = 0;

    /**
     * @brief 在一个函数上执行该 pass。
     *
     * @param function 待处理的函数 IR。
     * @param analysis_manager 用于按需获取 analysis 结果的管理器。
     * @return 一份 preserved 集合，描述 pass 执行后仍然有效的 analysis。
     */
    virtual analysis::PreservedAnalyses run(Function& function,
                                            analysis::FunctionAnalysisManager& analysis_manager) = 0;
};

}  // namespace optimizer
}  // namespace baltam

#endif
