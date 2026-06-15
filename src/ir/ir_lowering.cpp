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
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
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

static const std::unordered_map<nodeType, BinaryOp> kBinaryOpMap = {
    {node_add, Add},
    {node_subtract, Sub},
    {node_multiply, Mul},
    {node_right_divide, Rdiv},
    {node_left_divide, Ldiv},
    {node_power, Pow},
    {node_element_mul, ElemMul},
    {node_element_rdiv, ElemRdiv},
    {node_element_ldiv, ElemLdiv},
    {node_element_power, ElemPow},
    {node_logic_and, And},
    {node_logic_or, Or},
    {node_less_than, Lt},
    {node_leq, Le},
    {node_greater_than, Gt},
    {node_geq, Ge},
    {node_eq, Eq},
    {node_noteq, Ne},
};

static const std::unordered_map<nodeType, UnaryOp> kUnaryOpMap = {
    {node_uplus, Uplus},
    {node_negative, Uminus},
    {node_logic_not, LogicalNot},
    {node_transpose, Transpose},
    {node_ctranspose, Ctranspose},
};

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

    char* float_end = nullptr;
    errno = 0;
    const double float_value = std::strtod(text.c_str(), &float_end);
    if (float_end != nullptr && *float_end == '\0' && errno != ERANGE) {
        out_constant = Float64Constant{float_value};
        return true;
    }

    return false;
}

void collect_cell_elements(const ast_ptr& node, std::vector<ast_ptr>& elements) {
    if (node == nullptr) {
        return;
    }

    if (node->nodetype == node_cell ||
        node->nodetype == node_list ||
        node->nodetype == node_horz_list) {
        for (const ast_ptr& branch : node->branch) {
            collect_cell_elements(branch, elements);
        }
        return;
    }

    elements.push_back(node);
}

ast_ptr single_assignment_lhs(const ast_ptr& lhs) {
    if (lhs != nullptr &&
        (lhs->nodetype == node_list || lhs->nodetype == node_horz_list) &&
        lhs->branch.size() == 1U) {
        return lhs->branch.front();
    }
    return lhs;
}

void collect_name_nodes(const ast_ptr& node, std::vector<std::string>& names) {
    if (node == nullptr) {
        return;
    }

    if (node->nodetype == node_name) {
        names.push_back(std::static_pointer_cast<symref>(node)->name());
        return;
    }

    for (const ast_ptr& branch : node->branch) {
        collect_name_nodes(branch, names);
    }
}

std::vector<std::string> collect_anonymous_param_names(const ast_ptr& param_node) {
    std::vector<std::string> names;
    if (param_node == nullptr ||
        param_node->nodetype == node_nop ||
        param_node->nodetype == node_empty) {
        return names;
    }

    if (param_node->nodetype == node_list || param_node->nodetype == node_horz_list) {
        for (const ast_ptr& branch : param_node->branch) {
            if (branch != nullptr && branch->nodetype == node_name) {
                names.push_back(std::static_pointer_cast<symref>(branch)->name());
            }
        }
        return names;
    }

    if (param_node->nodetype == node_name) {
        names.push_back(std::static_pointer_cast<symref>(param_node)->name());
    }
    return names;
}

std::size_t count_call_arguments(const ast_ptr& in_args) noexcept {
    if (in_args == nullptr ||
        in_args->nodetype == node_nop ||
        in_args->nodetype == node_empty) {
        return 0U;
    }

    if (in_args->nodetype == node_list || in_args->nodetype == node_horz_list) {
        return in_args->branch.size();
    }

    return 1U;
}

std::vector<std::string> collect_anonymous_free_names(
    const ast_ptr& body_node,
    const std::vector<std::string>& param_names) {
    std::unordered_set<std::string> params(param_names.begin(), param_names.end());
    std::set<std::string> free_names;
    std::vector<std::string> used_names;
    collect_name_nodes(body_node, used_names);

    for (const std::string& name : used_names) {
        if (params.find(name) == params.end()) {
            free_names.insert(name);
        }
    }

    return std::vector<std::string>(free_names.begin(), free_names.end());
}

bool is_empty_stmt_node(const ast_ptr& node) {
    if (node == nullptr) {
        return true;
    }

    switch (node->nodetype) {
        case node_nop:
        case node_empty:
        case node_comment:
        case node_andy_end_of_string:
            return true;
        case node_runlist:
        case node_cmdlist:
        case node_list:
            return std::all_of(
                node->branch.begin(),
                node->branch.end(),
                [](const ast_ptr& branch) { return is_empty_stmt_node(branch); });
        default:
            return false;
    }
}

} // namespace

IRLowerer::ScopedLoopContext::ScopedLoopContext(
    IRLowerer& lowerer,
    LoopControlContext context)
    : lowerer_(lowerer) {
    lowerer_.loop_stack_.push_back(std::move(context));
}

IRLowerer::ScopedLoopContext::~ScopedLoopContext() {
    // 确保 lower 当前循环体的任何提前返回路径都不会泄漏 loop context。
    lowerer_.loop_stack_.pop_back();
}

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
    unit_name_bindings_.clear();
    loop_stack_.clear();

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
    MFileUnit& mfile = builder_.begin_file(NormalizedPath(first_unit->filename));
    const std::string file_stem = mfile.file_stem().string();

    std::vector<std::shared_ptr<pcdata>> active_parsed_units;
    active_parsed_units.reserve(parsed_units.size());

    std::vector<CodeUnit*> lowered_units;
    lowered_units.reserve(parsed_units.size());

    mfile.entry_unit = nullptr;
    mfile.local_function_map.clear();

    const bool has_function_main_unit = std::any_of(
        parsed_units.begin(),
        parsed_units.end(),
        [&](const std::shared_ptr<pcdata>& parsed_unit) {
            return parsed_unit != nullptr &&
                !parsed_unit->is_mscript() &&
                parsed_unit->funname == file_stem;
        });

    for (const std::shared_ptr<pcdata>& parsed_unit : parsed_units) {
        if (parsed_unit == nullptr) {
            builder_.report(
                IRBuildDiagnostic::Warning,
                "已解析工作区列表中存在空项，已跳过");
            continue;
        }

        if (has_function_main_unit &&
            parsed_unit->is_mscript() &&
            is_empty_stmt_node(parsed_unit->ast)) {
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
            builder_.report(
                IRBuildDiagnostic::Error,
                std::string("创建代码单元失败: ") + parsed_unit->funname);
            continue;
        }
        unit->source_span = source_span_from(parsed_unit->ast);
        unit_name_bindings_.try_emplace(unit);

        const bool is_main_unit =
            parsed_unit->is_mscript() || parsed_unit->funname == file_stem;

        if (is_main_unit) {
            if (mfile.entry_unit == nullptr) {
                mfile.entry_unit = unit;
            } else {
                builder_.report(
                    IRBuildDiagnostic::Error,
                    "当前 lowering 暂不支持同一个文件中出现多个主代码单元",
                    unit->source_span);
            }
        } else if (unit->is_function()) {
            auto* function = static_cast<FunctionUnit*>(unit);
            const auto [it, inserted] = mfile.local_function_map.emplace(function->name, function);
            if (!inserted) {
                builder_.report(
                    IRBuildDiagnostic::Error,
                    std::string("当前 lowering 暂不支持重名 local 函数: ") + function->name,
                    function->source_span);
                it->second = function;
            }
        }

        active_parsed_units.push_back(parsed_unit);
        lowered_units.push_back(unit);
    }

    for (std::size_t i = 0; i < lowered_units.size(); ++i) {
        const std::shared_ptr<pcdata>& parsed_unit = active_parsed_units[i];
        CodeUnit* unit = lowered_units[i];

        if (unit->entry_block == nullptr) {
            BasicBlock* entry_block = unit->create_block("entry", SourceSpan::invalid());
            if (!unit->set_entry_block(entry_block)) {
                continue;
            }
        }

        builder_.set_current_unit(unit);
        builder_.set_insert_point(unit->entry_block);
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

        if (builder_.current_block() != nullptr &&
            !builder_.current_block()->has_terminator()) {
            std::unique_ptr<ReturnInst> inst = std::make_unique<ReturnInst>();
            inst->source_span = SourceSpan::invalid();

            if (unit->is_function()) {
                inst->values = load_function_return_values(SourceSpan::invalid());
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
        case node_asgn_element:
            lower_assign_stmt(std::static_pointer_cast<symasgn>(node));
            return;
        case node_flow_if:
            lower_if_stmt(std::static_pointer_cast<if_flow>(node));
            return;
        case node_flow_switch:
            lower_switch_stmt(std::static_pointer_cast<switch_flow>(node));
            return;
        case node_for:
            lower_for_stmt(std::static_pointer_cast<flow>(node));
            return;
        case node_flow_while:
            lower_while_stmt(std::static_pointer_cast<if_flow>(node));
            return;
        case node_break:
            lower_break_stmt(node);
            return;
        case node_continue:
            lower_continue_stmt(node);
            return;
        case node_multiple_func:
            lower_call_stmt(std::static_pointer_cast<multipleFuncCall>(node));
            return;
        case node_return: {
            std::unique_ptr<ReturnInst> inst = std::make_unique<ReturnInst>();
            inst->source_span = source_span_from(node);

            const CodeUnit* unit = builder_.current_unit();
            if (unit != nullptr && unit->is_function()) {
                inst->values = load_function_return_values(inst->source_span);
            }

            builder_.append_instruction(std::move(inst));
            return;
        }
        case node_nop:
        case node_empty:
        case node_comment:
        case node_andy_end_of_string:
            return;
        default:
            builder_.report(
                IRBuildDiagnostic::Error,
                std::string("当前 lowering 暂不支持语句节点: ") +
                    ast::nodeTypeString()[node->nodetype],
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

    const ast_ptr lhs = single_assignment_lhs(assign->s());
    if (lhs != assign->s()) {
        assign->s() = lhs;
    }

    if (lhs != nullptr &&
        (lhs->nodetype == node_name_element || lhs->nodetype == node_multiple_func)) {
        (void)lower_indexed_assign_stmt(assign);
        return;
    }

    if (lhs == nullptr || lhs->nodetype != node_name) {
        builder_.report(
            IRBuildDiagnostic::Error,
            "当前 lowering 只支持名字或圆括号索引形式的赋值左值",
            source_span_from(lhs));
        return;
    }

    const std::string& name = std::static_pointer_cast<symref>(lhs)->name();
    const ValueId value = lower_expr(assign->v());
    if (!value.is_valid()) {
        return;
    }

    const SourceSpan source_span = source_span_from(assign);
    const Slot slot = ensure_slot_binding(name, source_span);
    if (!slot.is_valid()) {
        return;
    }

    std::unique_ptr<StoreSlotInst> inst = std::make_unique<StoreSlotInst>();
    inst->slot = slot;
    inst->value = value;
    inst->source_span = source_span;
    builder_.append_instruction(std::move(inst));
}

bool IRLowerer::lower_indexed_assign_stmt(const std::shared_ptr<symasgn>& assign) {
    if (assign == nullptr || assign->s() == nullptr || assign->v() == nullptr) {
        builder_.report(
            IRBuildDiagnostic::Error,
            "索引赋值语句缺少左值或右值",
            source_span_from(assign));
        return false;
    }

    const ast_ptr lhs = single_assignment_lhs(assign->s());
    if (lhs == nullptr ||
        (lhs->nodetype != node_name_element && lhs->nodetype != node_multiple_func)) {
        builder_.report(
            IRBuildDiagnostic::Error,
            "当前 lowering 只支持名字圆括号形式的索引赋值",
            source_span_from(lhs));
        return false;
    }

    std::string name;
    ast_ptr name_node;
    ast_ptr index_args;
    if (lhs->nodetype == node_name_element) {
        const auto element = std::static_pointer_cast<symrefElement>(lhs);
        name = element->name();
        name_node = element->s_ref();
        index_args = element->index();
    } else {
        const auto call = std::static_pointer_cast<multipleFuncCall>(lhs);
        if (call == nullptr || call->s() == nullptr || call->s()->nodetype != node_name) {
            builder_.report(
                IRBuildDiagnostic::Error,
                "当前 lowering 只支持名字圆括号形式的索引赋值",
                source_span_from(lhs));
            return false;
        }
        name = std::static_pointer_cast<symref>(call->s())->name();
        name_node = call->s();
        index_args = call->in_args();
    }

    const SourceSpan source_span = source_span_from(assign);
    const ValueId base = lower_named_value(name, source_span_from(name_node));
    if (!base.is_valid()) {
        return false;
    }

    std::unique_ptr<CallInst> inst = std::make_unique<CallInst>();
    inst->callee_kind = CallInst::Direct;
    inst->dispatch_type = Internal;
    inst->callee = InternedString("paren_assign");
    inst->results.push_back(builder_.create_value());
    inst->arguments.push_back(base);
    inst->source_span = source_span;
    inst->attrs.is_synthetic = 1;

    if (!inst->results.front().is_valid()) {
        return false;
    }

    if (!append_index_arguments(inst->arguments, base, index_args)) {
        return false;
    }

    const ValueId rhs = lower_expr(assign->v());
    if (!rhs.is_valid()) {
        return false;
    }
    inst->arguments.push_back(rhs);

    const ValueId updated_base = inst->results.front();
    builder_.append_instruction(std::move(inst));
    return store_named_result(name, updated_base, source_span);
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

    std::vector<ValueId> results;
    if (!lower_named_invoke(call, result_names, source_span, results)) {
        return;
    }

    for (std::size_t i = 0; i < result_names.size(); ++i) {
        if (result_names[i].empty()) {
            continue;
        }

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
    const bool has_else_body = !is_empty_stmt_node(if_node->el());
    BasicBlock* else_block = nullptr;
    if (has_else_body) {
        else_block = unit->create_block("if.else", source_span_from(if_node->el()));
    }
    BasicBlock* exit_block = unit->create_block("if.exit", source_span_from(if_node));

    if (then_block == nullptr || exit_block == nullptr ||
        (has_else_body && else_block == nullptr)) {
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

void IRLowerer::lower_switch_stmt(const std::shared_ptr<switch_flow>& switch_node) {
    if (switch_node == nullptr ||
        switch_node->expr() == nullptr ||
        switch_node->cases() == nullptr) {
        builder_.report(
            IRBuildDiagnostic::Error,
            "switch 语句缺少表达式或 case 列表",
            source_span_from(switch_node));
        return;
    }

    CodeUnit* unit = builder_.current_unit();
    if (unit == nullptr) {
        builder_.report(
            IRBuildDiagnostic::Error,
            "没有活动代码单元，无法 lower switch 语句",
            source_span_from(switch_node));
        return;
    }

    std::size_t switch_index = 0;
    for (const auto& block_ptr : unit->basic_blocks) {
        if (block_ptr == nullptr) {
            continue;
        }
        const std::string& label = block_ptr->label;
        if (label == "switch.end" ||
            (label.rfind("switch.", 0) == 0 &&
             label.size() > 4U &&
             label.compare(label.size() - 4U, 4U, ".end") == 0)) {
            ++switch_index;
        }
    }
    const std::string switch_prefix = switch_index == 0
        ? std::string("switch")
        : "switch." + std::to_string(switch_index);

    BasicBlock* exit_block =
        unit->create_block(switch_prefix + ".end", source_span_from(switch_node));
    if (exit_block == nullptr) {
        return;
    }

    std::vector<ast_ptr> case_nodes;
    ast_ptr otherwise_node;
    for (const ast_ptr& case_node : switch_node->cases()->branch) {
        if (case_node == nullptr) {
            continue;
        }
        if (case_node->nodetype == node_otherwise) {
            otherwise_node = case_node;
            continue;
        }
        if (case_node->nodetype != node_case || case_node->branch.size() < 2U) {
            builder_.report(
                IRBuildDiagnostic::Error,
                "当前 lowering 暂不支持该 switch 分支节点",
                source_span_from(case_node));
            return;
        }
        case_nodes.push_back(case_node);
    }

    std::vector<BasicBlock*> check_blocks;
    std::vector<BasicBlock*> body_blocks;
    check_blocks.reserve(case_nodes.size());
    body_blocks.reserve(case_nodes.size());
    for (const ast_ptr& case_node : case_nodes) {
        BasicBlock* check_block =
            unit->create_block(switch_prefix + ".case", source_span_from(case_node->branch[0]));
        BasicBlock* body_block =
            unit->create_block(switch_prefix + ".body", source_span_from(case_node->branch[1]));
        if (check_block == nullptr || body_block == nullptr) {
            return;
        }
        check_blocks.push_back(check_block);
        body_blocks.push_back(body_block);
    }

    BasicBlock* otherwise_block = nullptr;
    if (otherwise_node != nullptr && !otherwise_node->branch.empty()) {
        otherwise_block =
            unit->create_block(switch_prefix + ".otherwise", source_span_from(otherwise_node));
        if (otherwise_block == nullptr) {
            return;
        }
    }

    BasicBlock* first_block = nullptr;
    if (!check_blocks.empty()) {
        first_block = check_blocks.front();
    } else if (otherwise_block != nullptr) {
        first_block = otherwise_block;
    } else {
        first_block = exit_block;
    }

    if (builder_.current_block() != nullptr &&
        !builder_.current_block()->has_terminator()) {
        std::unique_ptr<GotoInst> go = std::make_unique<GotoInst>();
        go->target = first_block;
        go->source_span = source_span_from(switch_node);
        go->attrs.is_synthetic = 1;
        builder_.append_instruction(std::move(go));
    }

    builder_.set_insert_point(first_block);
    const ValueId switch_value = lower_expr(switch_node->expr());
    if (!switch_value.is_valid()) {
        return;
    }

    for (std::size_t i = 0; i < case_nodes.size(); ++i) {
        const ast_ptr& case_node = case_nodes[i];
        BasicBlock* check_block = check_blocks[i];
        BasicBlock* body_block = body_blocks[i];
        BasicBlock* next_block = nullptr;
        if (i + 1U < check_blocks.size()) {
            next_block = check_blocks[i + 1U];
        } else if (otherwise_block != nullptr) {
            next_block = otherwise_block;
        } else {
            next_block = exit_block;
        }

        builder_.set_insert_point(check_block);
        const ValueId condition =
            build_switch_match_condition(switch_value, case_node->branch[0]);
        if (!condition.is_valid()) {
            return;
        }

        std::unique_ptr<BranchInst> branch = std::make_unique<BranchInst>();
        branch->condition = condition;
        branch->true_target = body_block;
        branch->false_target = next_block;
        branch->source_span = source_span_from(case_node->branch[0]);
        builder_.append_instruction(std::move(branch));

        builder_.set_insert_point(body_block);
        lower_stmt(case_node->branch[1]);
        if (builder_.current_block() != nullptr &&
            !builder_.current_block()->has_terminator()) {
            std::unique_ptr<GotoInst> go = std::make_unique<GotoInst>();
            go->target = exit_block;
            go->source_span = source_span_from(case_node->branch[1]);
            go->attrs.is_synthetic = 1;
            builder_.append_instruction(std::move(go));
        }
    }

    if (otherwise_block != nullptr) {
        builder_.set_insert_point(otherwise_block);
        lower_stmt(otherwise_node->branch.back());
        if (builder_.current_block() != nullptr &&
            !builder_.current_block()->has_terminator()) {
            std::unique_ptr<GotoInst> otherwise_go = std::make_unique<GotoInst>();
            otherwise_go->target = exit_block;
            otherwise_go->source_span = source_span_from(otherwise_node);
            otherwise_go->attrs.is_synthetic = 1;
            builder_.append_instruction(std::move(otherwise_go));
        }
    }

    builder_.set_insert_point(exit_block);
}

void IRLowerer::lower_for_stmt(const std::shared_ptr<flow>& for_node) {
    if (for_node == nullptr ||
        for_node->var_ref() == nullptr ||
        for_node->cond() == nullptr ||
        for_node->tl() == nullptr) {
        builder_.report(
            IRBuildDiagnostic::Error,
            "for 语句缺少循环变量、迭代表达式或循环体",
            source_span_from(for_node));
        return;
    }

    if (for_node->var_ref()->nodetype != node_name) {
        builder_.report(
            IRBuildDiagnostic::Error,
            "当前 lowering 只支持名字形式的 for 循环变量",
            source_span_from(for_node->var_ref()));
        return;
    }

    CodeUnit* unit = builder_.current_unit();
    if (unit == nullptr) {
        builder_.report(
            IRBuildDiagnostic::Error,
            "没有活动代码单元，无法 lower for 语句",
            source_span_from(for_node));
        return;
    }

    // Matlab for lowering 使用显式 CFG：preheader 只执行一次，header 每轮判断，
    // body 执行用户循环体，latch 维护内部迭代下标，exit 接后续语句。
    BasicBlock* preheader_block =
        unit->create_block("for.preheader", source_span_from(for_node));
    BasicBlock* header_block =
        unit->create_block("for.header", source_span_from(for_node));
    BasicBlock* body_block =
        unit->create_block("for.body", source_span_from(for_node->tl()));
    BasicBlock* latch_block =
        unit->create_block("for.latch", source_span_from(for_node));
    BasicBlock* exit_block =
        unit->create_block("for.end", source_span_from(for_node));

    if (preheader_block == nullptr ||
        header_block == nullptr ||
        body_block == nullptr ||
        latch_block == nullptr ||
        exit_block == nullptr) {
        return;
    }

    // 当前块自然落入 for.preheader，让 for 前的顺序语句和 loop CFG 接起来。
    if (builder_.current_block() != nullptr &&
        !builder_.current_block()->has_terminator()) {
        std::unique_ptr<GotoInst> go = std::make_unique<GotoInst>();
        go->target = preheader_block;
        go->source_span = source_span_from(for_node);
        go->attrs.is_synthetic = 1;
        builder_.append_instruction(std::move(go));
    }

    const std::string& loop_var_name =
        std::static_pointer_cast<symref>(for_node->var_ref())->name();
    const SourceSpan source_span = source_span_from(for_node);

    // preheader 只求值一次迭代表达式；之后循环体内修改 a 或 i 不会改变迭代序列。
    builder_.set_insert_point(preheader_block);
    const ValueId iterable = lower_expr(for_node->cond());
    if (!iterable.is_valid()) {
        return;
    }

    // iter_index 是唯一的循环携带内部状态；类型固定为 int64，不参与用户名字查找。
    const Slot iter_index_slot = builder_.create_slot(
        SlotTag::InternalLocal,
        "__for_idx",
        source_span,
        SlotValueType::Int64Scalar);
    if (!iter_index_slot.is_valid()) {
        return;
    }

    // foreach_init 返回循环不变量 state/max_iter，后续 block 直接复用这两个 ValueId。
    const ValueId state = builder_.create_value();
    const ValueId max_iter = builder_.create_value();
    if (!state.is_valid() || !max_iter.is_valid()) {
        return;
    }

    // internal.foreach_init 捕获当前 iterable，确定最大迭代次数和 runtime 状态。
    std::unique_ptr<CallInst> init = std::make_unique<CallInst>();
    init->callee_kind = CallInst::Direct;
    init->dispatch_type = Internal;
    init->callee = InternedString("foreach_init");
    init->results.push_back(state);
    init->results.push_back(max_iter);
    init->arguments.push_back(iterable);
    init->source_span = source_span;
    init->attrs.is_synthetic = 1;
    builder_.append_instruction(std::move(init));

    // Matlab for 的内部迭代下标从 1 开始，用户循环变量稍后由 foreach_iterate 写入。
    std::unique_ptr<ConstInst> initial_index = std::make_unique<ConstInst>();
    initial_index->result = builder_.create_value();
    initial_index->value = Int64Constant{1};
    initial_index->source_span = source_span;
    initial_index->attrs.is_synthetic = 1;
    const ValueId iter_index = initial_index->result;
    builder_.append_instruction(std::move(initial_index));

    std::unique_ptr<StoreSlotInst> store_initial_index = std::make_unique<StoreSlotInst>();
    store_initial_index->slot = iter_index_slot;
    store_initial_index->value = iter_index;
    store_initial_index->source_span = source_span;
    store_initial_index->attrs.is_synthetic = 1;
    builder_.append_instruction(std::move(store_initial_index));

    std::unique_ptr<GotoInst> preheader_go = std::make_unique<GotoInst>();
    preheader_go->target = header_block;
    preheader_go->source_span = source_span;
    preheader_go->attrs.is_synthetic = 1;
    builder_.append_instruction(std::move(preheader_go));

    // header 只用内部 int64 比较判断是否结束，不触发 Matlab 运算符重载。
    builder_.set_insert_point(header_block);
    std::unique_ptr<LoadSlotInst> load_iter_index = std::make_unique<LoadSlotInst>();
    load_iter_index->result = builder_.create_value();
    load_iter_index->slot = iter_index_slot;
    load_iter_index->source_span = source_span;
    load_iter_index->attrs.is_synthetic = 1;
    const ValueId current_iter_index = load_iter_index->result;
    builder_.append_instruction(std::move(load_iter_index));

    std::unique_ptr<BinaryInst> done = std::make_unique<BinaryInst>();
    done->result = builder_.create_value();
    done->op = Gt;
    done->dispatch_type = Internal;
    done->lhs = current_iter_index;
    done->rhs = max_iter;
    done->source_span = source_span;
    done->attrs.is_synthetic = 1;
    const ValueId done_value = done->result;
    builder_.append_instruction(std::move(done));

    std::unique_ptr<BranchInst> branch = std::make_unique<BranchInst>();
    branch->condition = done_value;
    branch->true_target = exit_block;
    branch->false_target = body_block;
    branch->source_span = source_span;
    branch->attrs.is_synthetic = 1;
    builder_.append_instruction(std::move(branch));

    // body 开始时按当前 iter_index 取出本轮循环变量值，再写入用户可见名字。
    builder_.set_insert_point(body_block);
    std::unique_ptr<LoadSlotInst> load_body_index = std::make_unique<LoadSlotInst>();
    load_body_index->result = builder_.create_value();
    load_body_index->slot = iter_index_slot;
    load_body_index->source_span = source_span;
    load_body_index->attrs.is_synthetic = 1;
    const ValueId body_iter_index = load_body_index->result;
    builder_.append_instruction(std::move(load_body_index));

    const ValueId current_value = builder_.create_value();
    if (!current_value.is_valid()) {
        return;
    }

    std::unique_ptr<CallInst> iterate = std::make_unique<CallInst>();
    iterate->callee_kind = CallInst::Direct;
    iterate->dispatch_type = Internal;
    iterate->callee = InternedString("foreach_iterate");
    iterate->results.push_back(current_value);
    iterate->arguments.push_back(state);
    iterate->arguments.push_back(body_iter_index);
    iterate->source_span = source_span;
    iterate->attrs.is_synthetic = 1;
    builder_.append_instruction(std::move(iterate));

    if (!store_named_result(loop_var_name, current_value, source_span_from(for_node->var_ref()))) {
        return;
    }

    // 用户循环体可能改写循环变量名，但不会影响内部 iter_index/state/max_iter。
    const ScopedLoopContext loop_context(*this, {exit_block, latch_block});
    lower_stmt(for_node->tl());
    if (builder_.current_block() != nullptr &&
        !builder_.current_block()->has_terminator()) {
        std::unique_ptr<GotoInst> body_go = std::make_unique<GotoInst>();
        body_go->target = latch_block;
        body_go->source_span = source_span_from(for_node->tl());
        body_go->attrs.is_synthetic = 1;
        builder_.append_instruction(std::move(body_go));
    }

    // latch 是 normal fallthrough 和 continue 的汇合点，统一推进下一轮 iter_index。
    builder_.set_insert_point(latch_block);
    std::unique_ptr<LoadSlotInst> load_latch_index = std::make_unique<LoadSlotInst>();
    load_latch_index->result = builder_.create_value();
    load_latch_index->slot = iter_index_slot;
    load_latch_index->source_span = source_span;
    load_latch_index->attrs.is_synthetic = 1;
    const ValueId latch_iter_index = load_latch_index->result;
    builder_.append_instruction(std::move(load_latch_index));

    std::unique_ptr<ConstInst> one = std::make_unique<ConstInst>();
    one->result = builder_.create_value();
    one->value = Int64Constant{1};
    one->source_span = source_span;
    one->attrs.is_synthetic = 1;
    const ValueId one_value = one->result;
    builder_.append_instruction(std::move(one));

    // 自增同样是内部 int64 primitive，不走用户级 plus 分派。
    std::unique_ptr<BinaryInst> next_index = std::make_unique<BinaryInst>();
    next_index->result = builder_.create_value();
    next_index->op = Add;
    next_index->dispatch_type = Internal;
    next_index->lhs = latch_iter_index;
    next_index->rhs = one_value;
    next_index->source_span = source_span;
    next_index->attrs.is_synthetic = 1;
    const ValueId next_index_value = next_index->result;
    builder_.append_instruction(std::move(next_index));

    std::unique_ptr<StoreSlotInst> store_next_index = std::make_unique<StoreSlotInst>();
    store_next_index->slot = iter_index_slot;
    store_next_index->value = next_index_value;
    store_next_index->source_span = source_span;
    store_next_index->attrs.is_synthetic = 1;
    builder_.append_instruction(std::move(store_next_index));

    std::unique_ptr<GotoInst> latch_go = std::make_unique<GotoInst>();
    latch_go->target = header_block;
    latch_go->source_span = source_span;
    latch_go->attrs.is_synthetic = 1;
    builder_.append_instruction(std::move(latch_go));

    // 后续语句从 for.end 继续 lower；break 也会跳到这个出口块。
    builder_.set_insert_point(exit_block);
}

void IRLowerer::lower_while_stmt(const std::shared_ptr<if_flow>& while_node) {
    if (while_node == nullptr ||
        while_node->cond() == nullptr ||
        while_node->tl() == nullptr) {
        builder_.report(
            IRBuildDiagnostic::Error,
            "while 语句缺少条件或循环体",
            source_span_from(while_node));
        return;
    }

    CodeUnit* unit = builder_.current_unit();
    if (unit == nullptr) {
        builder_.report(
            IRBuildDiagnostic::Error,
            "没有活动代码单元，无法 lower while 语句",
            source_span_from(while_node));
        return;
    }

    // while 使用三块 CFG：header 每轮重新求值条件，body 执行用户循环体，
    // end 接后续语句。while 没有循环携带更新块，普通路径和 continue 直接回 header。
    BasicBlock* header_block =
        unit->create_block("while.header", source_span_from(while_node));
    BasicBlock* body_block =
        unit->create_block("while.body", source_span_from(while_node->tl()));
    BasicBlock* exit_block =
        unit->create_block("while.end", source_span_from(while_node));

    if (header_block == nullptr ||
        body_block == nullptr ||
        exit_block == nullptr) {
        return;
    }

    if (builder_.current_block() != nullptr &&
        !builder_.current_block()->has_terminator()) {
        std::unique_ptr<GotoInst> go = std::make_unique<GotoInst>();
        go->target = header_block;
        go->source_span = source_span_from(while_node);
        go->attrs.is_synthetic = 1;
        builder_.append_instruction(std::move(go));
    }

    builder_.set_insert_point(header_block);
    const ValueId condition = lower_expr(while_node->cond());
    if (!condition.is_valid()) {
        return;
    }

    std::unique_ptr<BranchInst> branch = std::make_unique<BranchInst>();
    branch->condition = condition;
    branch->true_target = body_block;
    branch->false_target = exit_block;
    branch->source_span = source_span_from(while_node->cond());
    builder_.append_instruction(std::move(branch));

    builder_.set_insert_point(body_block);
    const ScopedLoopContext loop_context(*this, {exit_block, header_block});
    lower_stmt(while_node->tl());
    if (builder_.current_block() != nullptr &&
        !builder_.current_block()->has_terminator()) {
        std::unique_ptr<GotoInst> body_go = std::make_unique<GotoInst>();
        body_go->target = header_block;
        body_go->source_span = source_span_from(while_node->tl());
        body_go->attrs.is_synthetic = 1;
        builder_.append_instruction(std::move(body_go));
    }

    builder_.set_insert_point(exit_block);
}

void IRLowerer::lower_break_stmt(const ast_ptr& node) {
    if (loop_stack_.empty() || loop_stack_.back().break_target == nullptr) {
        builder_.report(
            IRBuildDiagnostic::Error,
            "break 语句必须出现在循环体内",
            source_span_from(node));
        return;
    }

    std::unique_ptr<GotoInst> go = std::make_unique<GotoInst>();
    go->target = loop_stack_.back().break_target;
    go->source_span = source_span_from(node);
    builder_.append_instruction(std::move(go));
}

void IRLowerer::lower_continue_stmt(const ast_ptr& node) {
    if (loop_stack_.empty() || loop_stack_.back().continue_target == nullptr) {
        builder_.report(
            IRBuildDiagnostic::Error,
            "continue 语句必须出现在循环体内",
            source_span_from(node));
        return;
    }

    std::unique_ptr<GotoInst> go = std::make_unique<GotoInst>();
    go->target = loop_stack_.back().continue_target;
    go->source_span = source_span_from(node);
    builder_.append_instruction(std::move(go));
}

bool IRLowerer::append_call_arguments(
    std::vector<Operand>& arguments,
    const ast_ptr& in_args) {
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
            arguments.push_back(argument);
        }
        return true;
    }

    const ValueId argument = lower_expr(in_args);
    if (!argument.is_valid()) {
        return false;
    }

    arguments.push_back(argument);
    return true;
}

bool IRLowerer::append_apply_arguments(
    std::vector<Operand>& arguments,
    const Operand& callee_or_base,
    const ast_ptr& in_args) {
    if (in_args == nullptr ||
        in_args->nodetype == node_nop ||
        in_args->nodetype == node_empty) {
        return true;
    }

    const std::size_t nindices = count_call_arguments(in_args);
    auto append_argument = [&](const ast_ptr& arg, std::size_t dim) {
        MagicEndInst::MagicEndContext context;
        context.callee_or_base = callee_or_base;
        context.dim = static_cast<std::uint32_t>(dim);
        context.nindices = static_cast<std::uint32_t>(nindices);
        magic_end_context_stack_.push_back(context);

        const ValueId argument = lower_expr(arg);
        magic_end_context_stack_.pop_back();
        if (!argument.is_valid()) {
            return false;
        }
        arguments.push_back(argument);
        return true;
    };

    if (in_args->nodetype == node_list || in_args->nodetype == node_horz_list) {
        std::size_t dim = 1U;
        for (const ast_ptr& arg : in_args->branch) {
            if (!append_argument(arg, dim)) {
                return false;
            }
            ++dim;
        }
        return true;
    }

    return append_argument(in_args, 1U);
}

bool IRLowerer::append_index_arguments(
    std::vector<Operand>& arguments,
    ValueId base,
    const ast_ptr& in_args) {
    if (in_args == nullptr ||
        in_args->nodetype == node_nop ||
        in_args->nodetype == node_empty) {
        return true;
    }

    const std::size_t nindices = count_call_arguments(in_args);
    auto append_argument = [&](const ast_ptr& arg, std::size_t dim) {
        MagicEndInst::MagicEndContext context;
        context.callee_or_base = base;
        context.dim = static_cast<std::uint32_t>(dim);
        context.nindices = static_cast<std::uint32_t>(nindices);
        magic_end_context_stack_.push_back(context);

        const ValueId argument = lower_index_argument(arg, base, dim, nindices);
        magic_end_context_stack_.pop_back();
        if (!argument.is_valid()) {
            return false;
        }
        arguments.push_back(argument);
        return true;
    };

    if (in_args->nodetype == node_list || in_args->nodetype == node_horz_list) {
        std::size_t dim = 1U;
        for (const ast_ptr& arg : in_args->branch) {
            if (!append_argument(arg, dim)) {
                return false;
            }
            ++dim;
        }
        return true;
    }

    return append_argument(in_args, 1U);
}

ValueId IRLowerer::lower_deferred_magic_end(const ast_ptr& node) {
    if (magic_end_context_stack_.empty()) {
        builder_.report(
            IRBuildDiagnostic::Error,
            "magic end 缺少有效索引上下文",
            source_span_from(node));
        return InvalidValueId;
    }

    std::unique_ptr<MagicEndInst> inst = std::make_unique<MagicEndInst>();
    inst->result = builder_.create_value();
    inst->source_span = source_span_from(node);
    inst->candidate_contexts.assign(
        magic_end_context_stack_.rbegin(),
        magic_end_context_stack_.rend());

    const ValueId result = inst->result;
    if (!result.is_valid()) {
        return InvalidValueId;
    }

    builder_.append_instruction(std::move(inst));
    return result;
}

ValueId IRLowerer::lower_index_argument(
    const ast_ptr& node,
    ValueId base,
    std::size_t dim,
    std::size_t nindices) {
    if (node == nullptr) {
        builder_.report(
            IRBuildDiagnostic::Error,
            "不能 lower 空索引表达式",
            SourceSpan::invalid());
        return InvalidValueId;
    }

    switch (node->nodetype) {
        case node_magic_end:
            return lower_deferred_magic_end(node);

        case node_uplus:
        case node_negative:
        case node_logic_not:
        case node_transpose:
        case node_ctranspose: {
            const ValueId operand = lower_index_argument(node->l(), base, dim, nindices);
            if (!operand.is_valid()) {
                return InvalidValueId;
            }

            const SourceSpan source_span = source_span_from(node);
            const auto op_it = kUnaryOpMap.find(node->nodetype);
            if (op_it == kUnaryOpMap.end()) {
                builder_.report(
                    IRBuildDiagnostic::Error,
                    std::string("当前 lowering 暂不支持一元索引表达式节点: ") +
                        ast::nodeTypeString()[node->nodetype],
                    source_span);
                return InvalidValueId;
            }

            std::unique_ptr<UnaryInst> inst = std::make_unique<UnaryInst>();
            inst->result = builder_.create_value();
            inst->op = op_it->second;
            inst->operand = operand;
            inst->source_span = source_span;
            const ValueId result = inst->result;
            if (!result.is_valid()) {
                return InvalidValueId;
            }
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
            const SourceSpan source_span = source_span_from(node);
            const auto op_it = kBinaryOpMap.find(node->nodetype);
            if (op_it == kBinaryOpMap.end()) {
                builder_.report(
                    IRBuildDiagnostic::Error,
                    std::string("当前 lowering 暂不支持二元索引表达式节点: ") +
                        ast::nodeTypeString()[node->nodetype],
                    source_span);
                return InvalidValueId;
            }

            const ValueId lhs = lower_index_argument(node->l(), base, dim, nindices);
            const ValueId rhs = lower_index_argument(node->r(), base, dim, nindices);
            if (!lhs.is_valid() || !rhs.is_valid()) {
                return InvalidValueId;
            }

            std::unique_ptr<BinaryInst> inst = std::make_unique<BinaryInst>();
            inst->result = builder_.create_value();
            inst->op = op_it->second;
            inst->lhs = lhs;
            inst->rhs = rhs;
            inst->source_span = source_span;
            const ValueId result = inst->result;
            if (!result.is_valid()) {
                return InvalidValueId;
            }
            builder_.append_instruction(std::move(inst));
            return result;
        }

        case node_colon: {
            if (node->branch.size() != 2U && node->branch.size() != 3U) {
                builder_.report(
                    IRBuildDiagnostic::Error,
                    "当前 lowering 暂不支持该索引冒号表达式",
                    source_span_from(node));
                return InvalidValueId;
            }

            std::unique_ptr<CallInst> inst = std::make_unique<CallInst>();
            inst->callee_kind = CallInst::Direct;
            inst->callee = InternedString("colon");
            inst->source_span = source_span_from(node);

            const ValueId result = builder_.create_value();
            if (!result.is_valid()) {
                return InvalidValueId;
            }
            inst->results.push_back(result);

            for (const ast_ptr& branch : node->branch) {
                const ValueId argument =
                    lower_index_argument(branch, base, dim, nindices);
                if (!argument.is_valid()) {
                    return InvalidValueId;
                }
                inst->arguments.push_back(argument);
            }

            builder_.append_instruction(std::move(inst));
            return result;
        }

        default:
            return lower_expr(node);
    }
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
        if (result_node == nullptr ||
            result_node->nodetype == node_nop ||
            result_node->nodetype == node_empty ||
            result_node->nodetype == node_placeholder) {
            result_names.emplace_back();
            return true;
        }

        if (result_node->nodetype != node_name) {
            builder_.report(
                IRBuildDiagnostic::Error,
                "当前 lowering 只支持名字形式的调用结果左值",
                source_span);
            return false;
        }

        const std::string name = std::static_pointer_cast<symref>(result_node)->name();
        result_names.push_back(name == "~" ? std::string() : name);
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

ValueId IRLowerer::build_switch_match_condition(
    ValueId switch_value,
    const ast_ptr& case_value) {
    if (case_value != nullptr && case_value->nodetype == node_cell) {
        builder_.report(
            IRBuildDiagnostic::Error,
            "当前 lowering 暂不支持 cell 形式的 switch case",
            source_span_from(case_value));
        return InvalidValueId;
    }
    if (case_value == nullptr) {
        builder_.report(
            IRBuildDiagnostic::Error,
            "switch case 缺少匹配值",
            source_span_from(case_value));
        return InvalidValueId;
    }

    const ValueId rhs = lower_expr(case_value);
    if (!rhs.is_valid()) {
        return InvalidValueId;
    }

    std::unique_ptr<CallInst> match = std::make_unique<CallInst>();
    match->callee_kind = CallInst::Direct;
    match->dispatch_type = Internal;
    match->callee = InternedString("switch_match");
    match->results.push_back(builder_.create_value());
    match->arguments.push_back(switch_value);
    match->arguments.push_back(rhs);
    match->source_span = source_span_from(case_value);
    match->attrs.is_synthetic = 1;
    const ValueId match_value = match->results.front();
    if (!match_value.is_valid()) {
        return InvalidValueId;
    }
    builder_.append_instruction(std::move(match));
    return match_value;
}

bool IRLowerer::lower_named_invoke(
    const std::shared_ptr<multipleFuncCall>& call,
    const std::vector<std::string>& result_names,
    SourceSpan source_span,
    std::vector<ValueId>& results) {
    results.clear();
    results.reserve(result_names.size());

    auto append_result_slot = [&](std::vector<ValueId>& inst_results) {
        if (result_names[inst_results.size()].empty()) {
            inst_results.push_back(InvalidValueId);
            results.push_back(InvalidValueId);
            return true;
        }

        const ValueId result = builder_.create_value();
        if (!result.is_valid()) {
            return false;
        }

        inst_results.push_back(result);
        results.push_back(result);
        return true;
    };

    if (should_lower_direct_call(call->name())) {
        std::unique_ptr<CallInst> inst = std::make_unique<CallInst>();
        inst->callee_kind = CallInst::Direct;
        inst->callee = InternedString(call->name());
        inst->source_span = source_span;

        for (std::size_t i = 0; i < result_names.size(); ++i) {
            if (!append_result_slot(inst->results)) {
                return false;
            }
        }

        if (!append_call_arguments(inst->arguments, call->in_args())) {
            return false;
        }

        builder_.append_instruction(std::move(inst));
        return true;
    }

    if (builder_.current_unit() != nullptr &&
        (builder_.current_unit()->is_function() ||
         builder_.current_unit()->is_anonymous_function())) {
        const Slot callee_slot = lookup_slot_binding(call->name(), source_span);
        if (!callee_slot.is_valid()) {
            return false;
        }

        std::unique_ptr<LoadSlotInst> load = std::make_unique<LoadSlotInst>();
        load->result = builder_.create_value();
        if (!load->result.is_valid()) {
            return false;
        }
        load->slot = callee_slot;
        load->source_span = source_span;
        const ValueId base = load->result;
        builder_.append_instruction(std::move(load));

        std::unique_ptr<ValueApplyInst> inst = std::make_unique<ValueApplyInst>();
        inst->base = base;
        inst->source_span = source_span;

        for (std::size_t i = 0; i < result_names.size(); ++i) {
            if (!append_result_slot(inst->results)) {
                return false;
            }
        }

        if (!append_index_arguments(inst->arguments, base, call->in_args())) {
            return false;
        }

        builder_.append_instruction(std::move(inst));
        return true;
    }

    std::unique_ptr<ApplyInst> inst = std::make_unique<ApplyInst>();
    inst->source_span = source_span;
    const Operand callee_or_base = InternedString(call->name());
    inst->callee_or_base = callee_or_base;

    for (std::size_t i = 0; i < result_names.size(); ++i) {
        if (!append_result_slot(inst->results)) {
            return false;
        }
    }

    if (!append_apply_arguments(inst->arguments, callee_or_base, call->in_args())) {
        return false;
    }

    builder_.append_instruction(std::move(inst));
    return true;
}

Slot IRLowerer::lookup_var(std::string_view name) const noexcept {
    if (const Slot* slot = find_name(name)) {
        return *slot;
    }
    return InvalidSlot;
}

bool IRLowerer::should_lower_direct_call(std::string_view name) const noexcept {
    const CodeUnit* unit = builder_.current_unit();
    if (unit == nullptr ||
        (!unit->is_function() && !unit->is_anonymous_function())) {
        return false;
    }

    return !lookup_var(name).is_valid();
}

bool IRLowerer::store_named_result(
    std::string_view name,
    ValueId value,
    SourceSpan source_span) {
    const Slot slot = ensure_slot_binding(name, source_span);
    if (!slot.is_valid()) {
        return false;
    }

    std::unique_ptr<StoreSlotInst> inst = std::make_unique<StoreSlotInst>();
    inst->slot = slot;
    inst->value = value;
    inst->source_span = source_span;
    builder_.append_instruction(std::move(inst));
    return true;
}

ValueId IRLowerer::lower_named_value(std::string_view name, SourceSpan source_span) {
    const Slot slot = lookup_slot_binding(name, source_span);
    if (!slot.is_valid()) {
        return InvalidValueId;
    }

    std::unique_ptr<LoadSlotInst> inst = std::make_unique<LoadSlotInst>();
    inst->result = builder_.create_value();
    inst->slot = slot;
    inst->source_span = source_span;
    const ValueId result = inst->result;
    builder_.append_instruction(std::move(inst));
    return result;
}

ValueId IRLowerer::lower_named_function_handle(const ast_ptr& node) {
    if (node == nullptr || node->branch.empty() ||
        node->branch.front() == nullptr ||
        node->branch.front()->nodetype != node_name) {
        builder_.report(
            IRBuildDiagnostic::Error,
            "当前 lowering 只支持具名函数句柄 @name",
            source_span_from(node));
        return InvalidValueId;
    }

    const std::string& name =
        std::static_pointer_cast<symref>(node->branch.front())->name();

    std::unique_ptr<CreateNamedFunctionHandleInst> inst =
        std::make_unique<CreateNamedFunctionHandleInst>();
    inst->result = builder_.create_value();
    if (!inst->result.is_valid()) {
        return InvalidValueId;
    }
    inst->name = InternedString(name);
    inst->source_span = source_span_from(node);

    const ValueId result = inst->result;
    builder_.append_instruction(std::move(inst));
    return result;
}

ValueId IRLowerer::lower_anonymous_function_handle(const ast_ptr& node) {
    if (node == nullptr ||
        node->nodetype != node_anonymous_func ||
        node->branch.size() < 2U) {
        builder_.report(
            IRBuildDiagnostic::Error,
            "当前 lowering 只支持标准匿名函数句柄 @(args) expr",
            source_span_from(node));
        return InvalidValueId;
    }

    CodeUnit* outer_unit = builder_.current_unit();
    BasicBlock* outer_block = builder_.current_block();
    if (outer_unit == nullptr || outer_block == nullptr) {
        builder_.report(
            IRBuildDiagnostic::Error,
            "创建匿名函数句柄时缺少外层插入点",
            source_span_from(node));
        return InvalidValueId;
    }

    const ast_ptr& param_node = node->branch[0];
    const ast_ptr& body_node = node->branch[1];
    const std::vector<std::string> param_names = collect_anonymous_param_names(param_node);
    const std::vector<std::string> free_names =
        collect_anonymous_free_names(body_node, param_names);

    std::vector<CreateAnonymousFunctionHandleInst::CaptureValue> captures;
    captures.reserve(free_names.size());
    for (const std::string& name : free_names) {
        const Slot source_slot = lookup_slot_binding(name, source_span_from(body_node));
        if (!source_slot.is_valid()) {
            return InvalidValueId;
        }

        std::unique_ptr<LoadSlotInst> load = std::make_unique<LoadSlotInst>();
        load->result = builder_.create_value();
        if (!load->result.is_valid()) {
            return InvalidValueId;
        }
        load->slot = source_slot;
        load->source_span = source_span_from(node);
        const ValueId captured_value = load->result;
        builder_.append_instruction(std::move(load));

        captures.push_back({
            InternedString(name),
            source_slot,
            captured_value,
        });
    }

    AnonymousFunctionUnit& anonymous_unit =
        builder_.begin_anonymous_function_unit(source_span_from(node));
    unit_name_bindings_.try_emplace(&anonymous_unit);
    BasicBlock* entry_block = anonymous_unit.create_block("entry", source_span_from(node));
    if (!anonymous_unit.set_entry_block(entry_block)) {
        return InvalidValueId;
    }

    builder_.set_current_unit(&anonymous_unit);
    builder_.set_insert_point(entry_block);

    for (const std::string& name : param_names) {
        const Slot slot = builder_.create_slot(
            SlotTag::Arg,
            name,
            source_span_from(param_node));
        if (!slot.is_valid()) {
            builder_.set_current_unit(outer_unit);
            builder_.set_insert_point(outer_block);
            return InvalidValueId;
        }
        bind_name(name, slot, source_span_from(param_node));
    }

    for (const auto& capture : captures) {
        const Slot slot = builder_.create_slot(
            SlotTag::Capture,
            capture.name,
            source_span_from(body_node));
        if (!slot.is_valid()) {
            builder_.set_current_unit(outer_unit);
            builder_.set_insert_point(outer_block);
            return InvalidValueId;
        }
        bind_name(capture.name, slot, source_span_from(body_node));
    }

    const ValueId body_value = lower_expr(body_node);
    if (!body_value.is_valid()) {
        builder_.set_current_unit(outer_unit);
        builder_.set_insert_point(outer_block);
        return InvalidValueId;
    }

    std::unique_ptr<ReturnInst> ret = std::make_unique<ReturnInst>();
    ret->values.push_back(body_value);
    ret->source_span = source_span_from(body_node);
    builder_.append_instruction(std::move(ret));

    const AnonymousFunctionId function_id = anonymous_unit.id;
    builder_.set_current_unit(outer_unit);
    builder_.set_insert_point(outer_block);

    std::unique_ptr<CreateAnonymousFunctionHandleInst> inst =
        std::make_unique<CreateAnonymousFunctionHandleInst>();
    inst->result = builder_.create_value();
    if (!inst->result.is_valid()) {
        return InvalidValueId;
    }
    inst->function_id = function_id;
    inst->captures = std::move(captures);
    inst->source_span = source_span_from(node);

    const ValueId result = inst->result;
    builder_.append_instruction(std::move(inst));
    return result;
}

ValueId IRLowerer::lower_concat_expr(const ast_ptr& node) {
    if (node == nullptr ||
        (node->nodetype != node_horz_list && node->nodetype != node_vert_list)) {
        builder_.report(
            IRBuildDiagnostic::Error,
            "当前 lowering 只支持横向或纵向矩阵拼接表达式",
            source_span_from(node));
        return InvalidValueId;
    }

    if (node->nodetype == node_vert_list && node->branch.empty()) {
        std::unique_ptr<ConstInst> inst = std::make_unique<ConstInst>();
        inst->result = builder_.create_value();
        inst->value = EmptyDoubleMatrixConstant{};
        inst->source_span = source_span_from(node);
        const ValueId result = inst->result;
        builder_.append_instruction(std::move(inst));
        return result;
    }

    std::unique_ptr<CallInst> inst = std::make_unique<CallInst>();
    inst->callee_kind = CallInst::Direct;
    inst->callee = InternedString(
        node->nodetype == node_horz_list ? "horzcat" : "vertcat");
    inst->results.push_back(builder_.create_value());
    inst->source_span = source_span_from(node);

    if (!inst->results.front().is_valid()) {
        return InvalidValueId;
    }

    for (const ast_ptr& branch : node->branch) {
        const ValueId argument = lower_expr(branch);
        if (!argument.is_valid()) {
            return InvalidValueId;
        }
        inst->arguments.push_back(argument);
    }

    const ValueId result = inst->results.front();
    builder_.append_instruction(std::move(inst));
    return result;
}

ValueId IRLowerer::lower_index_expr(const ast_ptr& node) {
    if (node == nullptr || node->nodetype != node_name_element) {
        builder_.report(
            IRBuildDiagnostic::Error,
            "当前 lowering 只支持名字圆括号形式的索引表达式",
            source_span_from(node));
        return InvalidValueId;
    }

    const auto element = std::static_pointer_cast<symrefElement>(node);
    const ValueId base = lower_named_value(element->name(), source_span_from(element->s_ref()));
    if (!base.is_valid()) {
        return InvalidValueId;
    }

    std::unique_ptr<ValueApplyInst> inst = std::make_unique<ValueApplyInst>();
    inst->base = base;
    inst->results.push_back(builder_.create_value());
    inst->source_span = source_span_from(node);

    if (!inst->results.front().is_valid()) {
        return InvalidValueId;
    }

    if (!append_index_arguments(inst->arguments, base, element->index())) {
        return InvalidValueId;
    }

    const ValueId result = inst->results.front();
    builder_.append_instruction(std::move(inst));
    return result;
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
            return lower_named_value(name, source_span_from(node));
        }
        case node_handle_func:
            return lower_named_function_handle(node);
        case node_anonymous_func:
            return lower_anonymous_function_handle(node);
        case node_horz_list:
        case node_vert_list:
            return lower_concat_expr(node);
        case node_name_element:
            return lower_index_expr(node);
        case node_uplus:
        case node_negative:
        case node_logic_not:
        case node_transpose:
        case node_ctranspose: {
            const ValueId operand = lower_expr(node->l());
            if (!operand.is_valid()) {
                return InvalidValueId;
            }

            const SourceSpan source_span = source_span_from(node);
            const auto op_it = kUnaryOpMap.find(node->nodetype);
            if (op_it == kUnaryOpMap.end()) {
                builder_.report(
                    IRBuildDiagnostic::Error,
                    std::string("当前 lowering 暂不支持一元表达式节点: ") +
                        ast::nodeTypeString()[node->nodetype],
                    source_span);
                return InvalidValueId;
            }

            std::unique_ptr<UnaryInst> inst = std::make_unique<UnaryInst>();
            inst->result = builder_.create_value();
            inst->op = op_it->second;
            inst->operand = operand;
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
            const SourceSpan source_span = source_span_from(node);
            const auto op_it = kBinaryOpMap.find(node->nodetype);
            if (op_it == kBinaryOpMap.end()) {
                builder_.report(
                    IRBuildDiagnostic::Error,
                    std::string("当前 lowering 暂不支持二元表达式节点: ") +
                        ast::nodeTypeString()[node->nodetype],
                    source_span);
                return InvalidValueId;
            }

            const ValueId lhs = lower_expr(node->l());
            const ValueId rhs = lower_expr(node->r());
            if (!lhs.is_valid() || !rhs.is_valid()) {
                return InvalidValueId;
            }

            std::unique_ptr<BinaryInst> inst = std::make_unique<BinaryInst>();
            inst->result = builder_.create_value();
            inst->op = op_it->second;
            inst->lhs = lhs;
            inst->rhs = rhs;
            inst->source_span = source_span;
            const ValueId result = inst->result;
            builder_.append_instruction(std::move(inst));
            return result;
        }
        case node_logic_and_short:
        case node_logic_or_short:
            return lower_short_circuit_expr(node);
        case node_magic_end:
            return lower_deferred_magic_end(node);
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

            const SourceSpan source_span = source_span_from(node);
            std::vector<ValueId> results;
            const std::vector<std::string> implicit_result_names{"<expr-result>"};
            if (!lower_named_invoke(call, implicit_result_names, source_span, results)) {
                return InvalidValueId;
            }

            return results.front();
        }
        case node_colon: {
            if (node->branch.size() != 2U && node->branch.size() != 3U) {
                builder_.report(
                    IRBuildDiagnostic::Error,
                    "当前 lowering 暂不支持该冒号表达式",
                    source_span_from(node));
                return InvalidValueId;
            }

            std::unique_ptr<CallInst> inst = std::make_unique<CallInst>();
            inst->callee_kind = CallInst::Direct;
            inst->callee = InternedString("colon");
            inst->source_span = source_span_from(node);

            const ValueId result = builder_.create_value();
            if (!result.is_valid()) {
                return InvalidValueId;
            }
            inst->results.push_back(result);

            for (const ast_ptr& branch : node->branch) {
                const ValueId argument = lower_expr(branch);
                if (!argument.is_valid()) {
                    return InvalidValueId;
                }
                inst->arguments.push_back(argument);
            }

            builder_.append_instruction(std::move(inst));
            return result;
        }
        default:
            builder_.report(
                IRBuildDiagnostic::Error,
                std::string("当前 lowering 暂不支持表达式节点: ") +
                    ast::nodeTypeString()[node->nodetype],
                source_span_from(node));
            return InvalidValueId;
    }
}

ValueId IRLowerer::lower_short_circuit_expr(const ast_ptr& node) {
    if (node == nullptr ||
        (node->nodetype != node_logic_and_short &&
         node->nodetype != node_logic_or_short)) {
        builder_.report(
            IRBuildDiagnostic::Error,
            "短路表达式节点非法",
            source_span_from(node));
        return InvalidValueId;
    }

    CodeUnit* unit = builder_.current_unit();
    if (unit == nullptr) {
        builder_.report(
            IRBuildDiagnostic::Error,
            "没有活动代码单元，无法 lower 短路表达式",
            source_span_from(node));
        return InvalidValueId;
    }

    const bool is_and = node->nodetype == node_logic_and_short;
    const SourceSpan source_span = source_span_from(node);
    const SourceSpan lhs_span = source_span_from(node->l());
    const SourceSpan rhs_span = source_span_from(node->r());

    const ValueId lhs = lower_expr(node->l());
    if (!lhs.is_valid()) {
        return InvalidValueId;
    }

    BasicBlock* rhs_block = unit->create_block(
        is_and ? "sc.and.rhs" : "sc.or.rhs",
        rhs_span);
    BasicBlock* short_block = unit->create_block(
        is_and ? "sc.and.false" : "sc.or.true",
        lhs_span);
    BasicBlock* merge_block = unit->create_block(
        is_and ? "sc.and.end" : "sc.or.end",
        source_span);

    if (rhs_block == nullptr || short_block == nullptr || merge_block == nullptr) {
        return InvalidValueId;
    }

    const Slot result_slot = builder_.create_slot(
        SlotTag::InternalLocal,
        is_and ? "__sc_and" : "__sc_or",
        source_span,
        SlotValueType::LogicalScalar);
    if (!result_slot.is_valid()) {
        return InvalidValueId;
    }

    std::unique_ptr<BranchInst> branch = std::make_unique<BranchInst>();
    branch->condition = lhs;
    branch->true_target = is_and ? rhs_block : short_block;
    branch->false_target = is_and ? short_block : rhs_block;
    branch->source_span = source_span;
    builder_.append_instruction(std::move(branch));

    builder_.set_insert_point(short_block);
    std::unique_ptr<ConstInst> short_value = std::make_unique<ConstInst>();
    short_value->result = builder_.create_value();
    short_value->value = LogicalConstant{!is_and};
    short_value->source_span = source_span;
    short_value->attrs.is_synthetic = 1;
    const ValueId short_result = short_value->result;
    if (!short_result.is_valid()) {
        return InvalidValueId;
    }
    builder_.append_instruction(std::move(short_value));

    std::unique_ptr<StoreSlotInst> short_store = std::make_unique<StoreSlotInst>();
    short_store->slot = result_slot;
    short_store->value = short_result;
    short_store->source_span = source_span;
    short_store->attrs.is_synthetic = 1;
    builder_.append_instruction(std::move(short_store));

    std::unique_ptr<GotoInst> short_go = std::make_unique<GotoInst>();
    short_go->target = merge_block;
    short_go->source_span = source_span;
    short_go->attrs.is_synthetic = 1;
    builder_.append_instruction(std::move(short_go));

    builder_.set_insert_point(rhs_block);
    const ValueId rhs = lower_expr(node->r());
    if (!rhs.is_valid()) {
        return InvalidValueId;
    }

    std::unique_ptr<StoreSlotInst> rhs_store = std::make_unique<StoreSlotInst>();
    rhs_store->slot = result_slot;
    rhs_store->value = rhs;
    rhs_store->source_span = rhs_span;
    rhs_store->attrs.is_synthetic = 1;
    builder_.append_instruction(std::move(rhs_store));

    std::unique_ptr<GotoInst> rhs_go = std::make_unique<GotoInst>();
    rhs_go->target = merge_block;
    rhs_go->source_span = source_span;
    rhs_go->attrs.is_synthetic = 1;
    builder_.append_instruction(std::move(rhs_go));

    builder_.set_insert_point(merge_block);
    std::unique_ptr<LoadSlotInst> load = std::make_unique<LoadSlotInst>();
    load->slot = result_slot;
    load->result = builder_.create_value();
    load->source_span = source_span;
    load->attrs.is_synthetic = 1;
    const ValueId result = load->result;
    if (!result.is_valid()) {
        return InvalidValueId;
    }

    builder_.append_instruction(std::move(load));
    return result;
}

void IRLowerer::predeclare_function_signature(const pcdata& parsed_unit) {
    if (parsed_unit.ast != nullptr && parsed_unit.ast->nodetype == node_mfile_func) {
        const auto& function_ast = std::static_pointer_cast<mFileFunc>(parsed_unit.ast);

        if (function_ast->in_args() != nullptr) {
            if (function_ast->in_args()->nodetype == node_list ||
                function_ast->in_args()->nodetype == node_horz_list) {
                for (const ast_ptr& arg_node : function_ast->in_args()->branch) {
                    const std::string& arg_name = std::static_pointer_cast<symref>(arg_node)->name();
                    const Slot slot = builder_.create_slot(
                        SlotTag::Arg,
                        arg_name,
                        SourceSpan::invalid());

                    bind_name(arg_name, slot, SourceSpan::invalid());
                }
            } else if (function_ast->in_args()->nodetype == node_name) {
                const std::string& arg_name =
                    std::static_pointer_cast<symref>(function_ast->in_args())->name();
                const Slot slot = builder_.create_slot(
                    SlotTag::Arg,
                    arg_name,
                    SourceSpan::invalid());

                bind_name(arg_name, slot, SourceSpan::invalid());
            }
        }

        if (function_ast->out_args() != nullptr) {
            if (function_ast->out_args()->nodetype == node_list ||
                function_ast->out_args()->nodetype == node_horz_list) {
                for (const ast_ptr& ret_node : function_ast->out_args()->branch) {
                    const std::string& ret_name = std::static_pointer_cast<symref>(ret_node)->name();
                    const Slot slot = builder_.create_slot(
                        SlotTag::Ret,
                        ret_name,
                        SourceSpan::invalid());

                    bind_name(ret_name, slot, SourceSpan::invalid());
                }
            } else if (function_ast->out_args()->nodetype == node_name) {
                const std::string& ret_name =
                    std::static_pointer_cast<symref>(function_ast->out_args())->name();
                const Slot slot = builder_.create_slot(
                    SlotTag::Ret,
                    ret_name,
                    SourceSpan::invalid());

                bind_name(ret_name, slot, SourceSpan::invalid());
            }
        }
        return;
    }
}

Slot IRLowerer::ensure_slot_binding(std::string_view name, SourceSpan source_span) {
    const Slot slot = lookup_var(name);
    if (slot.is_valid()) {
        return slot;
    }

    const CodeUnit* unit = builder_.current_unit();
    const SlotTag tag = unit != nullptr && unit->is_script()
        ? SlotTag::ScriptVar
        : SlotTag::Local;
    const Slot new_slot = builder_.create_slot(tag, name, source_span);
    if (!new_slot.is_valid()) {
        return InvalidSlot;
    }

    bind_name(name, new_slot, source_span);
    return new_slot;
}

Slot IRLowerer::lookup_slot_binding(std::string_view name, SourceSpan source_span) {
    const Slot slot = lookup_var(name);
    if (!slot.is_valid()) {
        builder_.report(
            IRBuildDiagnostic::Error,
            std::string("读取了尚未绑定到槽位的名字: ") + std::string(name),
            source_span);
        return InvalidSlot;
    }

    return slot;
}

std::vector<ValueId> IRLowerer::load_function_return_values(SourceSpan source_span) {
    std::vector<ValueId> values;

    const CodeUnit* unit = builder_.current_unit();
    if (unit == nullptr || !unit->is_function()) {
        return values;
    }

    const auto* function = static_cast<const FunctionUnit*>(unit);
    values.reserve(function->return_slots.size());

    for (Slot slot : function->return_slots) {
        auto inst = std::make_unique<LoadSlotInst>();
        inst->result = builder_.create_value();
        if (!inst->result.is_valid()) {
            continue;
        }
        inst->slot = slot;
        inst->source_span = source_span;
        const ValueId result = inst->result;
        builder_.append_instruction(std::move(inst));
        values.push_back(result);
    }

    return values;
}

void IRLowerer::bind_name(
    std::string_view name,
    Slot slot,
    SourceSpan source_span) {
    const CodeUnit* unit = builder_.current_unit();
    if (unit == nullptr) {
        builder_.report(
            IRBuildDiagnostic::Error,
            "没有活动代码单元，无法绑定名字",
            source_span);
        return;
    }

    if (!slot.is_valid()) {
        builder_.report(
            IRBuildDiagnostic::Error,
            "当前名字绑定缺少有效槽位",
            source_span);
        return;
    }

    unit_name_bindings_[unit][InternedString(name)] = slot;
}

const Slot* IRLowerer::find_name(std::string_view name) const noexcept {
    const CodeUnit* unit = builder_.current_unit();
    if (unit == nullptr) {
        return nullptr;
    }

    const auto unit_it = unit_name_bindings_.find(unit);
    if (unit_it == unit_name_bindings_.end()) {
        return nullptr;
    }

    const auto binding_it = unit_it->second.find(InternedString(name));
    if (binding_it == unit_it->second.end()) {
        return nullptr;
    }
    return &binding_it->second;
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

    result.module = std::move(lowered.module);
    result.mfile = lowered.mfile;
    lowered.mfile = nullptr;
    result.diagnostics.insert(
        result.diagnostics.end(),
        std::make_move_iterator(lowered.diagnostics.begin()),
        std::make_move_iterator(lowered.diagnostics.end()));
    return result;
}

} // namespace baltam
