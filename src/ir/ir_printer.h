#ifndef BALTAM_IR_IR_PRINTER_H
#define BALTAM_IR_IR_PRINTER_H

#include <iosfwd>

#include "ir/ir.h"

namespace baltam {

/**
 * @brief 以接近 LLVM IR 的文本风格打印当前 IR。
 *
 * 当前支持 `NonSSA` 与 `UntypedSSA` 两个阶段。
 */
void print_ir(std::ostream& os, const Module& module);

}  // namespace baltam

#endif
