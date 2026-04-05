#ifndef BALTAM_IR_ANALYSIS_VERIFIER_H
#define BALTAM_IR_ANALYSIS_VERIFIER_H

#include <string>
#include <vector>

#include "ir/ir.h"

namespace baltam {
namespace analysis {

/**
 * @brief 单条 verifier 诊断。
 */
struct VerificationDiagnostic {
    std::string message;
};

/**
 * @brief verifier 的聚合结果。
 *
 * 当前 verifier 采用“尽量收集完整错误，再统一返回”的方式，便于一次性
 * 看到结构问题，而不是在第一处失败时立即中断。
 */
struct VerificationResult {
    std::vector<VerificationDiagnostic> diagnostics;

    bool ok() const;
    void add_error(std::string message);
    std::string format() const;
};

/**
 * @brief 验证单个函数 IR 的结构合法性。
 *
 * 当前支持 `NonSSA` 与 `UntypedSSA` 两个阶段。
 */
VerificationResult verify_function(const Function& function);

/**
 * @brief 验证整个模块 IR 的结构合法性。
 */
VerificationResult verify_module(const Module& module);

/**
 * @brief 验证失败时抛出 `std::runtime_error`。
 */
void verify_function_or_throw(const Function& function);

/**
 * @brief 验证失败时抛出 `std::runtime_error`。
 */
void verify_module_or_throw(const Module& module);

}  // namespace analysis
}  // namespace baltam

#endif
