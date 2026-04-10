#ifndef BALTAM_IR_OPTIMIZER_CONSTRUCT_UNTYPED_SSA_H
#define BALTAM_IR_OPTIMIZER_CONSTRUCT_UNTYPED_SSA_H

#include "ir/ir.h"

namespace baltam {
namespace optimizer {

/**
 * @brief 把一个 non-SSA 模块转换成新的 untyped SSA 模块。
 *
 * 当前实现要求输入函数都处于 `NonSSA` 阶段，且 CFG 中不存在不可达块。
 * 输入模块不会被原地修改；返回值是新构造的 SSA 模块。
 */
Module construct_untyped_ssa_module(Module& non_ssa_module);

}  // namespace optimizer
}  // namespace baltam

#endif
