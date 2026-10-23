#include "ir/ir_unit.h"

#include <stdexcept>
#include <utility>

namespace baltam {

std::string Instruction::to_string() const {
    throw std::logic_error("指令的字符串表示尚未实现");
}

CreateAnonymousFunctionHandleInst::CreateAnonymousFunctionHandleInst(
    std::shared_ptr<AnonymousFunctionUnit> target,
    std::vector<CaptureValue> captures, ValueId result)
    : Instruction(Type::CreateAnonymousFunctionHandle),
      target(std::move(target)), captures(std::move(captures)), result(result) {
    if (!this->target) {
        throw std::invalid_argument("匿名函数句柄的函数体不能为空");
    }
    if (this->captures.size() != this->target->capture_slots().size()) {
        throw std::invalid_argument("匿名函数句柄的捕获值数量与函数体不一致");
    }
    for (const CaptureValue& capture : this->captures) {
        if (capture.name.empty()) {
            throw std::invalid_argument("匿名函数句柄的捕获信息无效");
        }
    }
}

} // baltam 命名空间
