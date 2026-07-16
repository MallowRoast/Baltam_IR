#pragma once

#include "runtime/frame.h"

#include <vector>

namespace baltam {

[[nodiscard]] std::vector<ba_obj_ptr> execute_frame(RuntimeFrame& frame);

} // namespace baltam
