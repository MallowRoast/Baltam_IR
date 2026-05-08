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
struct flow;
struct multipleFuncCall;
struct symasgn;
class pcdata;

/**
 * @brief IR lowering 驱动。
 *
 * 当前版本已支持一组面向 `test0 / test0_1` 的最小 AST 子集：
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
     * - lower `test0 / test0_1` 所需的最小语句 / 表达式子集
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
    void lower_for_stmt(const std::shared_ptr<flow>& for_node);
    void lower_while_stmt(const std::shared_ptr<if_flow>& while_node);
    void lower_break_stmt(const ast_ptr& node);
    void lower_continue_stmt(const ast_ptr& node);
    [[nodiscard]] ValueId lower_expr(const ast_ptr& node);
    /**
     * @brief 统一 lower 调用实参列表。
     *
     * parser 对 `A(...)` 的实参可能给出单节点或 list 形态。把它收成一个 helper，
     * 可以让 `call` 和 `apply` 共用同一套参数顺序与错误处理逻辑，避免两边各自展开。
     */
    [[nodiscard]] bool append_call_arguments(
        std::vector<Operand>& arguments,
        const ast_ptr& in_args);
    /**
     * @brief 提取调用结果左值名字。
     *
     * `out_args` 同样可能是空、单名字或名字列表。单独收在 helper 里，是为了让
     * statement path 复用这套遍历和校验逻辑，而 expression path 也能先据此拒绝
     * “带显式输出左值”的节点。
     */
    [[nodiscard]] bool collect_call_result_names(
        const ast_ptr& out_args,
        std::vector<std::string>& result_names,
        SourceSpan source_span);
    /**
     * @brief 将名字形式的 `A(...)` lower 成 `call` 或 `apply`。
     *
     * 这两类指令在结果分配、参数 lowering 和指令追加上大部分流程相同，只在 callee
     * 形状和分派规则上不同。抽成 helper 可以保证 statement / expression 两条路径
     * 使用完全一致的 lowering 规则。
     */
    [[nodiscard]] bool lower_named_invoke(
        const std::shared_ptr<multipleFuncCall>& call,
        std::size_t result_count,
        SourceSpan source_span,
        std::vector<ValueId>& results);
    [[nodiscard]] const FunctionUnit* lookup_local_function(std::string_view name) const noexcept;
    /**
     * @brief 判断当前函数中的名字调用是否可直接收敛为 `call`。
     *
     * 我们不依赖 AST 给出的符号类型，而是依赖 lowering 过程中已经建立的名字绑定表。
     * 这个 helper 把“函数里未绑定为变量的名字可视为直接调用”这条规则单独封装起来。
     */
    [[nodiscard]] bool should_lower_direct_call(std::string_view name) const noexcept;
    /**
     * @brief 将调用产生的结果值写回到名字。
     *
     * 调用本身先只产出 `ValueId`，真正的写回目标要到这里才按 unit 类型分流：
     * script 写 workspace，function 写 slot。这样多结果调用语句不必重复处理这层差异。
     */
    [[nodiscard]] bool store_named_result(
        std::string_view name,
        ValueId value,
        SourceSpan source_span);

    void predeclare_function_signature(const pcdata& parsed_unit);
    [[nodiscard]] SlotId ensure_slot_binding(std::string_view name, SourceSpan source_span);
    [[nodiscard]] SlotId lookup_slot_binding(std::string_view name, SourceSpan source_span);
    [[nodiscard]] SlotId ensure_workspace_handle_slot(SourceSpan source_span);

    /**
     * @brief 当前正在 lowering 的循环控制流目标。
     *
     * `break` / `continue` 的目标不是语句自身能独立决定的，而是由最近一层循环提供：
     * `break` 跳到循环出口，`continue` 跳到循环 latch。用 vector 按栈保存这些目标，
     * 可以让嵌套循环自然使用最近一层 loop context，并在离开循环体时恢复外层目标。
     */
    struct LoopControlContext {
        BasicBlock* break_target = nullptr;
        BasicBlock* continue_target = nullptr;
    };

    /**
     * @brief 当前循环体 lowering 期间的 loop context 作用域守卫。
     *
     * 构造时把当前循环的 `break / continue` 目标压入 `loop_stack_`，析构时自动弹出。
     * 这样 `lower_break_stmt()` / `lower_continue_stmt()` 总能通过栈顶找到最近一层循环，
     * 同时避免循环体 lowering 中出现提前返回时遗留错误的控制流目标。
     */
    class ScopedLoopContext final {
    public:
        ScopedLoopContext(IRLowerer& lowerer, LoopControlContext context);

        ScopedLoopContext(const ScopedLoopContext&) = delete;
        ScopedLoopContext& operator=(const ScopedLoopContext&) = delete;

        ~ScopedLoopContext();

    private:
        IRLowerer& lowerer_;
    };

    IRBuilder builder_;
    std::string source_text_;
    std::vector<SourceSpan::offset_type> line_offsets_;
    // 这里把 vector 当作小型栈使用；相比 std::stack，调试和必要时遍历诊断更直接。
    std::vector<LoopControlContext> loop_stack_;
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
