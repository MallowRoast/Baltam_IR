#include "ir/ir_lowering.h"

#include "bt_ast_interface/bt_ast_interface.h"
#include "bt_ast_interface/ast/flow_control.h"
#include "bt_ast_interface/ast/mfile_func.h"
#include "bt_ast_interface/ast/multi_func_call.h"
#include "bt_ast_interface/ast/numval.h"
#include "bt_ast_interface/ast/symref.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace baltam {
namespace {

/**
 * @brief `bt_ast_interface` 的局部 RAII 会话。
 *
 * 这里故意不做成静态单例：
 * - `initialize()` / `finalize()` 的生命周期需要和一次 parse-lowering 调用显式配对
 * - 避免把全局 parser 状态延长到整个进程生命周期，降低隐藏状态带来的推理成本
 * - 避免首次初始化失败后把失败状态静态缓存住，影响后续调用的重试语义
 */
class ASTInterfaceSession final {
public:
    ASTInterfaceSession() noexcept
        : initialized_(bt_ast_interface::initialize() == 0) {}

    ASTInterfaceSession(const ASTInterfaceSession&) = delete;
    ASTInterfaceSession& operator=(const ASTInterfaceSession&) = delete;

    ~ASTInterfaceSession() {
        if (initialized_) {
            bt_ast_interface::finalize();
        }
    }

    [[nodiscard]] bool ok() const noexcept {
        return initialized_;
    }

private:
    bool initialized_ = false;
};

const char* ast_node_type_name(nodeType type) noexcept {
    const char** names = ast::nodeTypeString();
    if (names == nullptr) {
        return "<unknown>";
    }
    return names[type];
}

BinaryOp lower_binary_op(nodeType type, bool& ok) noexcept {
    ok = true;

    switch (type) {
        case node_add:
            return Add;
        case node_subtract:
            return Sub;
        case node_multiply:
            return Mul;
        case node_right_divide:
            return Rdiv;
        case node_left_divide:
            return Ldiv;
        case node_power:
            return Pow;
        case node_element_mul:
            return ElemMul;
        case node_element_rdiv:
            return ElemRdiv;
        case node_element_ldiv:
            return ElemLdiv;
        case node_element_power:
            return ElemPow;
        case node_logic_and:
            return And;
        case node_logic_or:
            return Or;
        case node_less_than:
            return Lt;
        case node_leq:
            return Le;
        case node_greater_than:
            return Gt;
        case node_geq:
            return Ge;
        case node_eq:
            return Eq;
        case node_noteq:
            return Ne;
        default:
            ok = false;
            return Add;
    }
}

bool try_parse_number_constant(const numval& number_node, Constant& out_constant) {
    std::string text = number_node.str;
    text.erase(
        std::remove_if(
            text.begin(),
            text.end(),
            [](unsigned char ch) { return std::isspace(ch) != 0; }),
        text.end());

    if (text.empty()) {
        return false;
    }

    char* int_end = nullptr;
    errno = 0;
    const long long int_value = std::strtoll(text.c_str(), &int_end, 10);
    if (int_end != nullptr && *int_end == '\0' && errno != ERANGE) {
        out_constant = Int64Constant{int_value};
        return true;
    }

    char* float_end = nullptr;
    errno = 0;
    const double float_value = std::strtod(text.c_str(), &float_end);
    if (float_end != nullptr && *float_end == '\0' && errno != ERANGE) {
        out_constant = Float64Constant{float_value};
        return true;
    }

    return false;
}

} // namespace

void IRLowerer::load_source_text(const NormalizedPath& path) {
    source_text_.clear();
    line_offsets_.clear();

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return;
    }

    source_text_.assign(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());

    line_offsets_.push_back(0);
    for (SourceSpan::offset_type i = 0; i < source_text_.size(); ++i) {
        if (source_text_[i] == '\n') {
            line_offsets_.push_back(i + 1U);
        }
    }
}

SourceSpan IRLowerer::source_span_from(const ast_ptr& node) const noexcept {
    if (node == nullptr ||
        source_text_.empty() ||
        line_offsets_.empty()) {
        return SourceSpan::invalid();
    }

    const auto& loc = node->loc;
    const auto to_offset = [&](const scanp::position& pos, bool is_end) -> SourceSpan::offset_type {
        if (pos.line < 1 || pos.column < 1) {
            return SourceSpan::kInvalidOffset;
        }

        const std::size_t line_index = static_cast<std::size_t>(pos.line - 1);
        if (line_index >= line_offsets_.size()) {
            return SourceSpan::kInvalidOffset;
        }

        const SourceSpan::offset_type line_begin = line_offsets_[line_index];
        const SourceSpan::offset_type line_limit =
            line_index + 1U < line_offsets_.size()
                ? line_offsets_[line_index + 1U]
                : static_cast<SourceSpan::offset_type>(source_text_.size());
        const SourceSpan::offset_type column_offset =
            static_cast<SourceSpan::offset_type>(pos.column - (is_end ? 0 : 1));
        return std::min(line_begin + column_offset, line_limit);
    };

    const SourceSpan::offset_type begin_offset = to_offset(loc.begin, false);
    const SourceSpan::offset_type end_offset = to_offset(loc.end, true);
    if (begin_offset == SourceSpan::kInvalidOffset ||
        end_offset == SourceSpan::kInvalidOffset) {
        return SourceSpan::invalid();
    }

    if (begin_offset < end_offset) {
        return SourceSpan(begin_offset, end_offset);
    }

    const std::size_t line_index = static_cast<std::size_t>(std::max(loc.begin.line, 1) - 1);
    if (line_index >= line_offsets_.size()) {
        return SourceSpan::invalid();
    }

    const SourceSpan::offset_type line_begin = line_offsets_[line_index];
    const SourceSpan::offset_type line_limit =
        line_index + 1U < line_offsets_.size()
            ? line_offsets_[line_index + 1U]
            : static_cast<SourceSpan::offset_type>(source_text_.size());
    return SourceSpan(line_begin, line_limit);
}

IRBuildResult IRLowerer::lower_parsed_units(
    const std::vector<std::shared_ptr<pcdata>>& parsed_units) {
    builder_.reset();
    source_text_.clear();
    line_offsets_.clear();

    if (parsed_units.empty()) {
        builder_.report(
            IRBuildDiagnostic::Error,
            "没有可 lower 的已解析工作区");
        return builder_.finish();
    }

    const std::shared_ptr<pcdata>& first_unit = parsed_units.front();
    if (first_unit == nullptr) {
        builder_.report(
            IRBuildDiagnostic::Error,
            "已解析工作区列表的首项为空");
        return builder_.finish();
    }

    load_source_text(NormalizedPath(first_unit->filename));
    builder_.begin_file(NormalizedPath(first_unit->filename));

    for (const std::shared_ptr<pcdata>& parsed_unit : parsed_units) {
        if (parsed_unit == nullptr) {
            builder_.report(
                IRBuildDiagnostic::Warning,
                "已解析工作区列表中存在空项，已跳过");
            continue;
        }

        if (parsed_unit->is_mscript()) {
            builder_.begin_script_unit(parsed_unit->funname, SourceSpan::invalid());
        } else {
            builder_.begin_function_unit(parsed_unit->funname, SourceSpan::invalid());
            predeclare_function_signature(*parsed_unit);
        }

        CodeUnit* unit = builder_.current_unit();
        if (unit == nullptr) {
            continue;
        }

        if (unit->entry_block == nullptr) {
            BasicBlock* entry_block = unit->create_block("entry", SourceSpan::invalid());
            if (!unit->set_entry_block(entry_block)) {
                continue;
            }
            builder_.set_insert_point(entry_block);
        }

        ast_ptr body = parsed_unit->ast;
        if (body != nullptr && body->nodetype == node_mfile_func) {
            body = std::static_pointer_cast<mFileFunc>(body)->body();
        }

        if (body == nullptr) {
            builder_.report(
                IRBuildDiagnostic::Warning,
                parsed_unit->is_mscript()
                    ? "脚本代码单元没有可 lower 的主体 AST，已跳过主体 lowering"
                    : "函数代码单元没有可 lower 的主体 AST，已跳过主体 lowering");
        } else {
            lower_stmt(body);
        }

        if (!builder_.current_block()->has_terminator()) {
            std::unique_ptr<ReturnInst> inst = std::make_unique<ReturnInst>();
            inst->source_span = SourceSpan::invalid();

            const CodeUnit* unit = builder_.current_unit();
            if (unit != nullptr && unit->is_function()) {
                const auto* function = static_cast<const FunctionUnit*>(unit);
                for (SlotId slot_id : function->return_slots) {
                    inst->values.push_back(slot_id);
                }
            }

            builder_.append_instruction(std::move(inst));
        }
    }

    return builder_.finish();
}

void IRLowerer::lower_stmt(const ast_ptr& node) {
    if (node == nullptr) {
        return;
    }

    switch (node->nodetype) {
        case node_runlist:
        case node_cmdlist:
        case node_list:
            lower_stmt_list(node);
            return;
        case node_asgn:
            lower_assign_stmt(std::static_pointer_cast<symasgn>(node));
            return;
        case node_flow_if:
            lower_if_stmt(std::static_pointer_cast<if_flow>(node));
            return;
        case node_multiple_func:
            lower_call_stmt(std::static_pointer_cast<multipleFuncCall>(node));
            return;
        case node_return: {
            std::unique_ptr<ReturnInst> inst = std::make_unique<ReturnInst>();
            inst->source_span = source_span_from(node);

            const CodeUnit* unit = builder_.current_unit();
            if (unit != nullptr && unit->is_function()) {
                const auto* function = static_cast<const FunctionUnit*>(unit);
                for (SlotId slot_id : function->return_slots) {
                    inst->values.push_back(slot_id);
                }
            }

            builder_.append_instruction(std::move(inst));
            return;
        }
        case node_nop:
        case node_empty:
        case node_comment:
            return;
        default:
            builder_.report(
                IRBuildDiagnostic::Error,
                std::string("当前 lowering 暂不支持语句节点: ") +
                    ast_node_type_name(node->nodetype),
                source_span_from(node));
            return;
    }
}

void IRLowerer::lower_stmt_list(const ast_ptr& node) {
    if (node == nullptr) {
        return;
    }

    for (const ast_ptr& branch : node->branch) {
        lower_stmt(branch);

        const BasicBlock* current = builder_.current_block();
        if (current != nullptr && current->has_terminator()) {
            return;
        }
    }
}

void IRLowerer::lower_assign_stmt(const std::shared_ptr<symasgn>& assign) {
    if (assign == nullptr || assign->s() == nullptr || assign->v() == nullptr) {
        builder_.report(
            IRBuildDiagnostic::Error,
            "赋值语句缺少左值或右值",
            source_span_from(assign));
        return;
    }

    if (assign->s()->nodetype != node_name) {
        builder_.report(
            IRBuildDiagnostic::Error,
            "当前 lowering 只支持名字形式的赋值左值",
            source_span_from(assign->s()));
        return;
    }

    const std::string& name = std::static_pointer_cast<symref>(assign->s())->name();
    const ValueId value = lower_expr(assign->v());
    if (!value.is_valid()) {
        return;
    }

    const SourceSpan source_span = source_span_from(assign);
    if (builder_.current_unit() != nullptr &&
        builder_.current_unit()->is_script()) {
        const SlotId workspace_handle_slot = ensure_workspace_handle_slot(source_span);
        if (!workspace_handle_slot.is_valid()) {
            return;
        }

        std::unique_ptr<StoreWorkspaceInst> inst = std::make_unique<StoreWorkspaceInst>();
        inst->workspace_handle_slot = workspace_handle_slot;
        inst->symbol = InternedString(name);
        inst->value = value;
        inst->source_span = source_span;
        builder_.append_instruction(std::move(inst));
        return;
    }

    const SlotId slot_id = ensure_slot_binding(name, source_span);
    if (!slot_id.is_valid()) {
        return;
    }

    std::unique_ptr<StoreSlotInst> inst = std::make_unique<StoreSlotInst>();
    inst->slot_id = slot_id;
    inst->value = value;
    inst->source_span = source_span;
    builder_.append_instruction(std::move(inst));
}

void IRLowerer::lower_call_stmt(const std::shared_ptr<multipleFuncCall>& call) {
    if (call == nullptr || call->s() == nullptr || call->s()->nodetype != node_name) {
        builder_.report(
            IRBuildDiagnostic::Error,
            "当前 lowering 只支持名字形式的圆括号应用",
            source_span_from(call));
        return;
    }

    const SourceSpan source_span = source_span_from(call);
    std::vector<std::string> result_names;
    if (!collect_call_result_names(call->out_args(), result_names, source_span)) {
        return;
    }

    std::unique_ptr<ApplyInst> inst = std::make_unique<ApplyInst>();
    inst->callee_or_base = InternedString(call->name());
    inst->source_span = source_span;
    for (std::size_t i = 0; i < result_names.size(); ++i) {
        inst->results.push_back(builder_.create_value());
    }

    if (!append_apply_arguments(*inst, call->in_args())) {
        return;
    }

    const std::vector<ValueId> results = inst->results;
    builder_.append_instruction(std::move(inst));

    for (std::size_t i = 0; i < result_names.size(); ++i) {
        if (!store_named_result(result_names[i], results[i], source_span)) {
            return;
        }
    }
}

void IRLowerer::lower_if_stmt(const std::shared_ptr<if_flow>& if_node) {
    if (if_node == nullptr || if_node->cond() == nullptr || if_node->tl() == nullptr) {
        builder_.report(
            IRBuildDiagnostic::Error,
            "if 语句缺少条件或 then 分支",
            source_span_from(if_node));
        return;
    }

    CodeUnit* unit = builder_.current_unit();
    if (unit == nullptr) {
        builder_.report(
            IRBuildDiagnostic::Error,
            "没有活动代码单元，无法 lower if 语句",
            source_span_from(if_node));
        return;
    }

    const ValueId condition = lower_expr(if_node->cond());
    if (!condition.is_valid()) {
        return;
    }

    BasicBlock* then_block = unit->create_block("if.then", source_span_from(if_node->tl()));
    BasicBlock* else_block = nullptr;
    if (if_node->el() != nullptr) {
        else_block = unit->create_block("if.else", source_span_from(if_node->el()));
    }
    BasicBlock* exit_block = unit->create_block("if.exit", source_span_from(if_node));

    if (then_block == nullptr || exit_block == nullptr ||
        (if_node->el() != nullptr && else_block == nullptr)) {
        return;
    }

    std::unique_ptr<BranchInst> branch = std::make_unique<BranchInst>();
    branch->condition = condition;
    branch->true_target = then_block;
    branch->false_target = else_block != nullptr ? else_block : exit_block;
    branch->source_span = source_span_from(if_node->cond());
    builder_.append_instruction(std::move(branch));

    builder_.set_insert_point(then_block);
    lower_stmt(if_node->tl());
    if (builder_.current_block() != nullptr &&
        !builder_.current_block()->has_terminator()) {
        std::unique_ptr<GotoInst> go = std::make_unique<GotoInst>();
        go->target = exit_block;
        go->source_span = source_span_from(if_node->tl());
        builder_.append_instruction(std::move(go));
    }

    if (else_block != nullptr) {
        builder_.set_insert_point(else_block);
        lower_stmt(if_node->el());
        if (builder_.current_block() != nullptr &&
            !builder_.current_block()->has_terminator()) {
            std::unique_ptr<GotoInst> go = std::make_unique<GotoInst>();
            go->target = exit_block;
            go->source_span = source_span_from(if_node->el());
            builder_.append_instruction(std::move(go));
        }
    }

    builder_.set_insert_point(exit_block);
}

bool IRLowerer::append_apply_arguments(ApplyInst& inst, const ast_ptr& in_args) {
    if (in_args == nullptr ||
        in_args->nodetype == node_nop ||
        in_args->nodetype == node_empty) {
        return true;
    }

    if (in_args->nodetype == node_list || in_args->nodetype == node_horz_list) {
        for (const ast_ptr& arg : in_args->branch) {
            const ValueId argument = lower_expr(arg);
            if (!argument.is_valid()) {
                return false;
            }
            inst.arguments.push_back(argument);
        }
        return true;
    }

    const ValueId argument = lower_expr(in_args);
    if (!argument.is_valid()) {
        return false;
    }

    inst.arguments.push_back(argument);
    return true;
}

bool IRLowerer::collect_call_result_names(
    const ast_ptr& out_args,
    std::vector<std::string>& result_names,
    SourceSpan source_span) {
    if (out_args == nullptr ||
        out_args->nodetype == node_nop ||
        out_args->nodetype == node_empty) {
        return true;
    }

    auto append_name = [&](const ast_ptr& result_node) {
        if (result_node == nullptr || result_node->nodetype != node_name) {
            builder_.report(
                IRBuildDiagnostic::Error,
                "当前 lowering 只支持名字形式的调用结果左值",
                source_span);
            return false;
        }

        result_names.push_back(std::static_pointer_cast<symref>(result_node)->name());
        return true;
    };

    if (out_args->nodetype == node_list || out_args->nodetype == node_horz_list) {
        for (const ast_ptr& result_node : out_args->branch) {
            if (!append_name(result_node)) {
                return false;
            }
        }
        return true;
    }

    return append_name(out_args);
}

bool IRLowerer::store_named_result(
    std::string_view name,
    ValueId value,
    SourceSpan source_span) {
    if (builder_.current_unit() != nullptr &&
        builder_.current_unit()->is_script()) {
        const SlotId workspace_handle_slot = ensure_workspace_handle_slot(source_span);
        if (!workspace_handle_slot.is_valid()) {
            return false;
        }

        std::unique_ptr<StoreWorkspaceInst> inst = std::make_unique<StoreWorkspaceInst>();
        inst->workspace_handle_slot = workspace_handle_slot;
        inst->symbol = InternedString(name);
        inst->value = value;
        inst->source_span = source_span;
        builder_.append_instruction(std::move(inst));
        return true;
    }

    const SlotId slot_id = ensure_slot_binding(name, source_span);
    if (!slot_id.is_valid()) {
        return false;
    }

    std::unique_ptr<StoreSlotInst> inst = std::make_unique<StoreSlotInst>();
    inst->slot_id = slot_id;
    inst->value = value;
    inst->source_span = source_span;
    builder_.append_instruction(std::move(inst));
    return true;
}

ValueId IRLowerer::lower_expr(const ast_ptr& node) {
    if (node == nullptr) {
        builder_.report(
            IRBuildDiagnostic::Error,
            "不能 lower 空表达式",
            SourceSpan::invalid());
        return InvalidValueId;
    }

    switch (node->nodetype) {
        case node_name: {
            const std::string& name = std::static_pointer_cast<symref>(node)->name();
            const SourceSpan source_span = source_span_from(node);

            if (builder_.current_unit() != nullptr &&
                builder_.current_unit()->is_script()) {
                const SlotId workspace_handle_slot = ensure_workspace_handle_slot(source_span);
                if (!workspace_handle_slot.is_valid()) {
                    return InvalidValueId;
                }

                std::unique_ptr<LoadWorkspaceInst> inst = std::make_unique<LoadWorkspaceInst>();
                inst->result = builder_.create_value();
                inst->workspace_handle_slot = workspace_handle_slot;
                inst->symbol = InternedString(name);
                inst->source_span = source_span;
                const ValueId result = inst->result;
                builder_.append_instruction(std::move(inst));
                return result;
            }

            const SlotId slot_id = lookup_slot_binding(name, source_span);
            if (!slot_id.is_valid()) {
                return InvalidValueId;
            }

            std::unique_ptr<LoadSlotInst> inst = std::make_unique<LoadSlotInst>();
            inst->result = builder_.create_value();
            inst->slot_id = slot_id;
            inst->source_span = source_span;
            const ValueId result = inst->result;
            builder_.append_instruction(std::move(inst));
            return result;
        }
        case node_number: {
            Constant constant;
            if (!try_parse_number_constant(*std::static_pointer_cast<numval>(node), constant)) {
                builder_.report(
                    IRBuildDiagnostic::Error,
                    "当前 lowering 暂不支持该数值字面量",
                    source_span_from(node));
                return InvalidValueId;
            }

            std::unique_ptr<ConstInst> inst = std::make_unique<ConstInst>();
            inst->result = builder_.create_value();
            inst->value = std::move(constant);
            inst->source_span = source_span_from(node);
            const ValueId result = inst->result;
            builder_.append_instruction(std::move(inst));
            return result;
        }
        case node_add:
        case node_subtract:
        case node_multiply:
        case node_right_divide:
        case node_left_divide:
        case node_power:
        case node_element_mul:
        case node_element_rdiv:
        case node_element_ldiv:
        case node_element_power:
        case node_logic_and:
        case node_logic_or:
        case node_less_than:
        case node_leq:
        case node_greater_than:
        case node_geq:
        case node_eq:
        case node_noteq: {
            bool ok = false;
            const BinaryOp op = lower_binary_op(node->nodetype, ok);
            if (!ok) {
                builder_.report(
                    IRBuildDiagnostic::Error,
                    std::string("当前 lowering 暂不支持二元表达式节点: ") +
                        ast_node_type_name(node->nodetype),
                    source_span_from(node));
                return InvalidValueId;
            }

            const ValueId lhs = lower_expr(node->l());
            const ValueId rhs = lower_expr(node->r());
            if (!lhs.is_valid() || !rhs.is_valid()) {
                return InvalidValueId;
            }

            std::unique_ptr<BinaryInst> inst = std::make_unique<BinaryInst>();
            inst->result = builder_.create_value();
            inst->op = op;
            inst->lhs = lhs;
            inst->rhs = rhs;
            inst->source_span = source_span_from(node);
            const ValueId result = inst->result;
            builder_.append_instruction(std::move(inst));
            return result;
        }
        case node_multiple_func: {
            const auto call = std::static_pointer_cast<multipleFuncCall>(node);
            if (call == nullptr || call->s() == nullptr || call->s()->nodetype != node_name) {
                builder_.report(
                    IRBuildDiagnostic::Error,
                    "当前 lowering 只支持名字形式的圆括号应用",
                    source_span_from(node));
                return InvalidValueId;
            }

            std::vector<std::string> result_names;
            if (!collect_call_result_names(call->out_args(), result_names, source_span_from(node))) {
                return InvalidValueId;
            }
            if (!result_names.empty()) {
                builder_.report(
                    IRBuildDiagnostic::Error,
                    "表达式上下文暂不支持带显式输出左值的圆括号应用",
                    source_span_from(node));
                return InvalidValueId;
            }

            std::unique_ptr<ApplyInst> inst = std::make_unique<ApplyInst>();
            inst->results.push_back(builder_.create_value());
            inst->callee_or_base = InternedString(call->name());
            inst->source_span = source_span_from(node);

            if (!append_apply_arguments(*inst, call->in_args())) {
                return InvalidValueId;
            }

            const ValueId result = inst->results.front();
            builder_.append_instruction(std::move(inst));
            return result;
        }
        default:
            builder_.report(
                IRBuildDiagnostic::Error,
                std::string("当前 lowering 暂不支持表达式节点: ") +
                    ast_node_type_name(node->nodetype),
                source_span_from(node));
            return InvalidValueId;
    }
}

void IRLowerer::predeclare_function_signature(const pcdata& parsed_unit) {
    SlotAttrs attrs;
    attrs.is_user_visible = 1;
    attrs.is_mutable = 1;

    if (parsed_unit.m_in_arg_names != nullptr) {
        for (const std::string& arg_name : *parsed_unit.m_in_arg_names) {
            const SlotId slot_id = builder_.create_slot(
                Slot::Arg,
                arg_name,
                SourceSpan::invalid(),
                attrs);

            builder_.bind_name(arg_name, slot_id);
        }
    }

    if (parsed_unit.m_out_arg_names != nullptr) {
        for (const std::string& ret_name : *parsed_unit.m_out_arg_names) {
            const SlotId slot_id = builder_.create_slot(
                Slot::Ret,
                ret_name,
                SourceSpan::invalid(),
                attrs);

            builder_.bind_name(ret_name, slot_id);
        }
    }
}

SlotId IRLowerer::ensure_slot_binding(std::string_view name, SourceSpan source_span) {
    if (SlotId* slot_id = builder_.find_name(name)) {
        return *slot_id;
    }

    SlotAttrs attrs;
    attrs.is_user_visible = 1;
    attrs.is_mutable = 1;

    const SlotId slot_id = builder_.create_slot(
        Slot::Local,
        name,
        source_span,
        attrs);
    if (!slot_id.is_valid()) {
        return InvalidSlotId;
    }

    builder_.bind_name(name, slot_id);
    return slot_id;
}

SlotId IRLowerer::lookup_slot_binding(std::string_view name, SourceSpan source_span) {
    const SlotId* slot_id = builder_.find_name(name);
    if (slot_id == nullptr) {
        builder_.report(
            IRBuildDiagnostic::Error,
            std::string("读取了尚未绑定到槽位的名字: ") + std::string(name),
            source_span);
        return InvalidSlotId;
    }

    return *slot_id;
}

SlotId IRLowerer::ensure_workspace_handle_slot(SourceSpan source_span) {
    const CodeUnit* unit = builder_.current_unit();
    if (unit == nullptr || !unit->is_script()) {
        builder_.report(
            IRBuildDiagnostic::Error,
            "只有脚本代码单元才能访问工作区句柄槽位",
            source_span);
        return InvalidSlotId;
    }

    if (const Slot* existing = unit->find_hidden_slot(SlotAttrs::WorkspaceHandle)) {
        return existing->slot_id;
    }

    return builder_.create_hidden_slot(
        "__workspace_handle__",
        SlotAttrs::WorkspaceHandle,
        source_span);
}

IRBuildResult parse_and_lower_mfile_to_ir(
    std::string_view filename,
    const ParserOpts& parser_opts) {
    IRBuildResult result;

    if (filename.empty()) {
        result.diagnostics.push_back({
            IRBuildDiagnostic::Error,
            InternedString("文件名不能为空"),
            SourceSpan::invalid(),
        });
        return result;
    }

    ASTInterfaceSession session;
    if (!session.ok()) {
        result.diagnostics.push_back({
            IRBuildDiagnostic::Error,
            InternedString("初始化 AST 解析接口失败"),
            SourceSpan::invalid(),
        });
        return result;
    }

    std::string parser_message;
    std::vector<std::shared_ptr<pcdata>> parsed_units =
        bt_ast_interface::parse_mfile(std::string(filename), parser_opts, parser_message);

    if (!parser_message.empty()) {
        result.diagnostics.push_back({
            parsed_units.empty() ? IRBuildDiagnostic::Error : IRBuildDiagnostic::Warning,
            InternedString(parser_message),
            SourceSpan::invalid(),
        });
    }

    if (parsed_units.empty()) {
        if (parser_message.empty()) {
            result.diagnostics.push_back({
                IRBuildDiagnostic::Error,
                InternedString("解析 M 文件失败，parser 未返回任何工作区"),
                SourceSpan::invalid(),
            });
        }
        return result;
    }

    IRLowerer lowerer;
    IRBuildResult lowered = lowerer.lower_parsed_units(parsed_units);

    result.mfile = std::move(lowered.mfile);
    result.diagnostics.insert(
        result.diagnostics.end(),
        std::make_move_iterator(lowered.diagnostics.begin()),
        std::make_move_iterator(lowered.diagnostics.end()));
    return result;
}

} // namespace baltam
