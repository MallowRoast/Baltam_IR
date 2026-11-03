#pragma once

#include "Transform/pass_manager.h"

namespace baltam {

class Mem2RegPass {
public:
    PreservedAnalyses run(FunctionUnit& function, FunctionAnalysisManager& analyses);
};

} // baltam 命名空间
