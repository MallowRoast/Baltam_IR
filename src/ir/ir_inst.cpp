#include "ir/ir_unit.h"

#include <stdexcept>
#include <utility>

namespace baltam {

std::string Instruction::to_string() const {
    throw std::logic_error("指令的字符串表示尚未实现");
}

} // baltam 命名空间
