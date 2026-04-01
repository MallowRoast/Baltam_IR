#ifndef BALTAM_IR_OPTIMIZER_PASS_MANAGER_H
#define BALTAM_IR_OPTIMIZER_PASS_MANAGER_H

#include <cstddef>
#include <memory>
#include <vector>

#include "optimizer/pass.h"

namespace baltam {
namespace optimizer {

/**
 * @brief 控制 `FunctionPassManager` 运行行为的选项。
 *
 * 第一版选项只覆盖最核心的调试与防护行为：
 *
 * - pipeline 前是否先跑 verifier
 * - 每个 pass 后是否跑 verifier
 * - pass 前后是否尝试打印 IR
 */
struct PassManagerOptions {
    /**
     * @brief 是否在整个 pipeline 开始前先验证输入函数 IR。
     */
    bool verify_before_pipeline = true;

    /**
     * @brief 是否在每个 pass 执行后重新验证函数 IR。
     */
    bool verify_after_each_pass = true;

    /**
     * @brief 是否在每个 pass 执行前打印当前函数所在模块的 IR。
     *
     * 旧 value-based IR printer 已移除，这个开关当前不会产生输出。
     */
    bool print_before_each_pass = false;

    /**
     * @brief 是否在每个 pass 执行后打印当前函数所在模块的 IR。
     *
     * 旧 value-based IR printer 已移除，这个开关当前不会产生输出。
     */
    bool print_after_each_pass = false;
};

/**
 * @brief 负责按顺序执行一组函数级 pass 的管理器。
 *
 * 该类本身不实现任何优化逻辑，只负责：
 *
 * - 保存 pass 顺序
 * - 驱动 pass 依次运行
 * - 在 pass 前后执行 verifier / 打印等调试动作
 * - 根据 `PreservedAnalyses` 通知 `FunctionAnalysisManager` 失效缓存
 */
class FunctionPassManager {
public:
    /**
     * @brief 向 pipeline 末尾追加一个函数级 pass。
     *
     * 空指针会被静默忽略。
     */
    void add_pass(std::unique_ptr<FunctionPass> pass);

    /**
     * @brief 返回当前已注册的 pass 个数。
     */
    std::size_t pass_count() const;

    /**
     * @brief 判断当前 pipeline 是否为空。
     */
    bool empty() const;

    /**
     * @brief 在指定函数上运行整个 pass pipeline。
     *
     * @param function 待优化的函数。
     * @param analysis_manager 当前 pipeline 共享的函数级 analysis 管理器。
     * @param options 控制 verifier 和打印行为的运行选项。
     */
    void run(Function& function, analysis::FunctionAnalysisManager& analysis_manager,
             const PassManagerOptions& options = {}) const;

private:
    /**
     * @brief 按注册顺序保存的函数级 pass 列表。
     */
    std::vector<std::unique_ptr<FunctionPass>> passes_;
};

}  // namespace optimizer
}  // namespace baltam

#endif
