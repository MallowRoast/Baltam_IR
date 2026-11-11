#pragma once

#include "Transform/pass_manager.h"

namespace baltam {

// Non-SSA 槽位数据流转发、跨块同值写回及安全覆盖写入消除。
class LoadStoreElimPass final {
public:
    PreservedAnalyses run(FunctionUnit& function, FunctionAnalysisManager& analyses);
    void run(CommandUnit& command);
};

} // baltam 命名空间
