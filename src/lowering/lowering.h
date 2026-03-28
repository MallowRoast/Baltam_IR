#ifndef BALTAM_IR_LOWERING_H
#define BALTAM_IR_LOWERING_H

#include <memory>
#include <vector>

#include "pcdata.h"
#include "ir/ir.h"

namespace baltam {

/**
 * @brief 将 parser 产生的 `.m` 文件工作区列表 lower 成模块级 IR。
 *
 * 当前入口会把脚本主体 lower 成 `__script_main__`，并把同一源文件中的
 * 其它函数工作区 lower 成同一个 Module 中的其它 Function。
 *
 * @param parsed_units `bt_ast_interface::parse_mfile()` 返回的工作区列表。
 * @return 对应的模块级 IR。
 */
Module lower_parsed_units_to_ir(const std::vector<std::shared_ptr<pcdata>>& parsed_units);

}  // namespace baltam

#endif
