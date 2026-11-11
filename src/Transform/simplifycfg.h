#pragma once

#include "Transform/pass_manager.h"

namespace baltam {

class SimplifyCFGPass final {
public:
    PreservedAnalyses run(FunctionUnit& function, FunctionAnalysisManager& analyses);
    void run(CommandUnit& command);
};

} // baltam 命名空间
