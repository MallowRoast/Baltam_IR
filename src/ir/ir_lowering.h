#pragma once

#include "ir/ir_builder.h"

#include "bt_ast_interface/ast/ast_base.h"
#include "bt_ast_interface/pcdata.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace baltam {

struct if_flow;
struct multipleFuncCall;
struct symasgn;

/**
 * @brief IR lowering 驱动。
 *
 * 当前版本已支持一组面向 `test0` 的最小 AST 子集：
 * - 简单赋值
 * - 名字与数值字面量表达式
 * - 名字形式的圆括号应用
 * - `if / else`
 * - 隐式 `return`
 */
class IRLowerer final {
public:
    IRLowerer() noexcept = default;

    /**
     * @brief 将 parser 产生的工作区列表 lower 成 IR 文件单元。
     *
     * 当前实现会：
     * - 创建 `MFileUnit`
     * - 为每个 `pcdata` 预声明 `ScriptUnit` 或 `FunctionUnit`
     * - 预声明函数参数 / 返回值 slot
     * - lower `test0` 所需的最小语句 / 表达式子集
     *
     * 当前实现直接复用 `pcdata::filename` 作为文件路径来源，
     * 不再要求调用方单独传入 `path` 参数。
     */
    [[nodiscard]] IRBuildResult lower_parsed_units(
        const std::vector<std::shared_ptr<pcdata>>& parsed_units);

private:
    void load_source_text(const NormalizedPath& path);
    [[nodiscard]] SourceSpan source_span_from(const ast_ptr& node) const noexcept;
    void lower_stmt(const ast_ptr& node);
    void lower_stmt_list(const ast_ptr& node);
    void lower_assign_stmt(const std::shared_ptr<symasgn>& assign);
    void lower_call_stmt(const std::shared_ptr<multipleFuncCall>& call);
    void lower_if_stmt(const std::shared_ptr<if_flow>& if_node);
    [[nodiscard]] ValueId lower_expr(const ast_ptr& node);
    [[nodiscard]] bool append_apply_arguments(ApplyInst& inst, const ast_ptr& in_args);
    [[nodiscard]] bool collect_call_result_names(
        const ast_ptr& out_args,
        std::vector<std::string>& result_names,
        SourceSpan source_span);
    [[nodiscard]] bool store_named_result(
        std::string_view name,
        ValueId value,
        SourceSpan source_span);

    void predeclare_function_signature(const pcdata& parsed_unit);
    [[nodiscard]] SlotId ensure_slot_binding(std::string_view name, SourceSpan source_span);
    [[nodiscard]] SlotId lookup_slot_binding(std::string_view name, SourceSpan source_span);
    [[nodiscard]] SlotId ensure_workspace_handle_slot(SourceSpan source_span);

    IRBuilder builder_;
    std::string source_text_;
    std::vector<SourceSpan::offset_type> line_offsets_;
};

/**
 * @brief 直接从 `.m` 文件 parse 并 lower 成 IR。
 *
 * 该入口会负责：
 * - 初始化 / 释放 `bt_ast_interface`
 * - 调用 `parse_mfile()`
 * - 把 parser 输出继续交给 `IRLowerer`
 */
[[nodiscard]] IRBuildResult parse_and_lower_mfile_to_ir(
    std::string_view filename,
    const ParserOpts& parser_opts = ParserOpts());

} // namespace baltam
