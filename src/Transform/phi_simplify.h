#pragma once

#include "ir/ir_unit.h"

namespace baltam {

// 消除有唯一具体来源的 PHI，保留 Undefined 和没有外部来源的循环。
bool simplify_trivial_phis(IRStageVersion& version);

} // baltam 命名空间
