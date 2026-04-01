#ifndef BALTAM_IR_ANALYSIS_VERIFIER_H
#define BALTAM_IR_ANALYSIS_VERIFIER_H

#include <string>
#include <vector>

#include "ir/ir.h"

namespace baltam {
namespace analysis {

struct VerificationDiagnostic {
    std::string message;
};

struct VerificationResult {
    std::vector<VerificationDiagnostic> diagnostics;

    bool ok() const;
    void add_error(std::string message);
    std::string format() const;
};

VerificationResult verify_function(const Function& function);
VerificationResult verify_module(const Module& module);

void verify_function_or_throw(const Function& function);
void verify_module_or_throw(const Module& module);

}  // namespace analysis
}  // namespace baltam

#endif
