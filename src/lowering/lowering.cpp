#include "lowering/lowering.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ast/flow_control.h"
#include "ast/mfile_func.h"
#include "ast/multi_func_call.h"
#include "ast/numval.h"
#include "ast/symref.h"
#include "ast/text_node.h"

namespace baltam {
namespace {

const char* ast_node_type_name(nodeType type) {
    const char** names = ast::nodeTypeString();
    if (names == nullptr) {
        return "<unknown>";
    }
    return names[type];
}

std::optional<SourceLocation> source_location_from(const ast_ptr& node) {
    if (!node) {
        return std::nullopt;
    }

    return SourceLocation{
        node->loc.begin.filename,
        node->loc.begin.line,
        node->loc.begin.column,
        node->loc.end.line,
        node->loc.end.column,
    };
}

std::string function_name_from_unit(const pcdata& unit) {
    if (unit.is_mscript()) {
        return "__script_main__";
    }
    if (!unit.funname.empty()) {
        return unit.funname;
    }
    return "__unnamed_function__";
}

std::string module_stem_from_path(const std::string& path) {
    if (path.empty()) {
        return {};
    }
    return std::filesystem::path(path).stem().string();
}

std::string module_name_from_unit(const pcdata& unit) {
    const std::string stem = module_stem_from_path(unit.filename);
    if (!stem.empty()) {
        return stem;
    }
    return function_name_from_unit(unit);
}

Function::Type function_type_from_unit(const pcdata& unit) {
    if (unit.is_mscript()) {
        return Function::Script;
    }
    return function_name_from_unit(unit) == module_stem_from_path(unit.filename)
               ? Function::PrimaryFunction
               : Function::LocalFunction;
}

Module::Type module_type_from_units(
    const std::vector<std::shared_ptr<pcdata>>& parsed_units) {
    if (!parsed_units.empty() && parsed_units.front() != nullptr && parsed_units.front()->is_mscript()) {
        return Module::M_Script;
    }
    return Module::M_Function;
}

std::vector<std::string> collect_name_list(const ast_ptr& node) {
    if (!node) {
        return {};
    }

    switch (node->nodetype) {
        case node_nop:
            return {};
        case node_name: {
            const auto sym = std::static_pointer_cast<symref>(node);
            return {sym->name()};
        }
        case node_list:
        case node_horz_list: {
            std::vector<std::string> names;
            for (const ast_ptr& branch : node->branch) {
                std::vector<std::string> branch_names = collect_name_list(branch);
                names.insert(names.end(), branch_names.begin(), branch_names.end());
            }
            return names;
        }
        default:
            break;
    }

    throw std::runtime_error("non-SSA lower 遇到了无法收集名字的 AST 节点。");
}

std::vector<std::string> collect_assignment_target_name_list(const ast_ptr& node) {
    if (!node) {
        return {};
    }

    switch (node->nodetype) {
        case node_nop:
            return {};
        case node_name: {
            const auto sym = std::static_pointer_cast<symref>(node);
            return {sym->name()};
        }
        case node_list:
        case node_horz_list: {
            std::vector<std::string> names;
            for (const ast_ptr& branch : node->branch) {
                std::vector<std::string> branch_names = collect_assignment_target_name_list(branch);
                names.insert(names.end(), branch_names.begin(), branch_names.end());
            }
            return names;
        }
        case node_struct_get:
        case node_cell_get:
            return collect_assignment_target_name_list(node->branch[0]);
        case node_multiple_func: {
            const auto call = std::static_pointer_cast<multipleFuncCall>(node);
            // 多返回值左值里允许出现 `X(...)` 这类切片写入目标；
            // predeclare 阶段这里只需要基名 `X`，不需要把索引结构继续展开。
            if (call->s() != nullptr && call->s()->nodetype == node_name) {
                return {call->name()};
            }
            break;
        }
        default:
            break;
    }

    throw std::runtime_error("non-SSA lower 遇到了无法收集赋值左值名字的 AST 节点。");
}

struct CallOutputTargetSpec {
    std::string name;
    ast_ptr lhs;
    bool requires_store_back = false;
};

std::vector<CallOutputTargetSpec> collect_call_output_target_specs(const ast_ptr& node) {
    if (!node) {
        return {};
    }

    switch (node->nodetype) {
        case node_nop:
            return {};
        case node_name: {
            const auto sym = std::static_pointer_cast<symref>(node);
            return {{sym->name(), node, false}};
        }
        case node_list:
        case node_horz_list: {
            std::vector<CallOutputTargetSpec> specs;
            for (const ast_ptr& branch : node->branch) {
                std::vector<CallOutputTargetSpec> branch_specs =
                    collect_call_output_target_specs(branch);
                specs.insert(specs.end(),
                             std::make_move_iterator(branch_specs.begin()),
                             std::make_move_iterator(branch_specs.end()));
            }
            return specs;
        }
        case node_multiple_func: {
            const auto call = std::static_pointer_cast<multipleFuncCall>(node);
            // `[a, X(...)] = f(...)` 这种左值不能把 call 输出直接绑定到 `X`：
            // 真正的语义是“先拿到该返回值，再把它写回 `X(...)` 指向的切片”。
            if (call->s() != nullptr && call->s()->nodetype == node_name) {
                return {{call->name(), node, true}};
            }
            break;
        }
        default:
            break;
    }

    throw std::runtime_error("non-SSA lower 遇到了暂不支持的多返回值左值。");
}

ast_ptr script_body_from_unit(const pcdata& unit) {
    return unit.ast;
}

ast_ptr function_body_from_unit(const pcdata& unit) {
    if (unit.ast && unit.ast->nodetype == node_mfile_func) {
        return std::static_pointer_cast<mFileFunc>(unit.ast)->body();
    }
    return unit.ast;
}

void collect_predeclared_user_names(const ast_ptr& node,
                                    std::unordered_set<std::string>& names,
                                    std::unordered_set<std::string>& global_names) {
    if (!node) {
        return;
    }

    switch (node->nodetype) {
        case node_runlist:
        case node_cmdlist:
        case node_list:
            for (const ast_ptr& branch : node->branch) {
                collect_predeclared_user_names(branch, names, global_names);
            }
            return;
        case node_asgn: {
            const auto assign = std::static_pointer_cast<symasgn>(node);
            if (assign->s() != nullptr && assign->s()->nodetype == node_name &&
                global_names.find(assign->name()) == global_names.end()) {
                names.insert(assign->name());
            }
            return;
        }
        case node_global:
            for (const ast_ptr& branch : node->branch) {
                if (branch == nullptr || branch->nodetype != node_name) {
                    throw std::runtime_error("non-SSA lower 目前只支持名字形式的 global 声明。");
                }
                const std::string& name = std::static_pointer_cast<symref>(branch)->name();
                global_names.insert(name);
                names.erase(name);
            }
            return;
        case node_struct_set:
            for (const std::string& name :
                 collect_assignment_target_name_list(node->branch[0])) {
                if (global_names.find(name) == global_names.end()) {
                    names.insert(name);
                }
            }
            return;
        case node_flow_if: {
            const auto if_node = std::static_pointer_cast<if_flow>(node);
            collect_predeclared_user_names(if_node->tl(), names, global_names);
            collect_predeclared_user_names(if_node->el(), names, global_names);
            return;
        }
        case node_flow_switch: {
            const auto switch_node = std::static_pointer_cast<switch_flow>(node);
            if (switch_node->cases() == nullptr) {
                return;
            }
            for (const ast_ptr& case_node : switch_node->cases()->branch) {
                if (!case_node) {
                    continue;
                }
                if (case_node->nodetype == node_case && case_node->branch.size() >= 2) {
                    collect_predeclared_user_names(case_node->branch[1], names, global_names);
                } else if (case_node->nodetype == node_otherwise &&
                           !case_node->branch.empty()) {
                    collect_predeclared_user_names(case_node->branch.back(), names, global_names);
                }
            }
            return;
        }
        case node_for: {
            const auto for_node = std::static_pointer_cast<flow>(node);
            if (for_node->var_ref() != nullptr && for_node->var_ref()->nodetype == node_name) {
                const std::string& name =
                    std::static_pointer_cast<symref>(for_node->var_ref())->name();
                if (global_names.find(name) == global_names.end()) {
                    names.insert(name);
                }
            }
            collect_predeclared_user_names(for_node->tl(), names, global_names);
            return;
        }
        case node_flow_while: {
            const auto while_node = std::static_pointer_cast<if_flow>(node);
            collect_predeclared_user_names(while_node->tl(), names, global_names);
            return;
        }
        case node_multiple_func: {
            const auto call = std::static_pointer_cast<multipleFuncCall>(node);
            for (const std::string& name : collect_assignment_target_name_list(call->out_args())) {
                if (global_names.find(name) == global_names.end()) {
                    names.insert(name);
                }
            }
            return;
        }
        default:
            return;
    }
}

struct CollectedFunctionNames {
    std::unordered_set<std::string> user_names;
    std::unordered_set<std::string> global_names;
};

CollectedFunctionNames collect_function_predeclared_names(const pcdata& unit) {
    CollectedFunctionNames result;
    collect_predeclared_user_names(function_body_from_unit(unit), result.user_names,
                                   result.global_names);
    return result;
}

std::vector<std::string> to_sorted_name_list(const std::unordered_set<std::string>& names) {
    std::vector<std::string> result(names.begin(), names.end());
    std::sort(result.begin(), result.end());
    return result;
}

void populate_function_signature(Function& function, const pcdata& unit) {
    const auto has_trailing_name = [](const std::vector<NamedValue>& values,
                                      const char* expected_name) {
        return !values.empty() && values.back().name == expected_name;
    };

    if (unit.m_in_arg_names != nullptr && !unit.m_in_arg_names->empty()) {
        function.set_input_names(*unit.m_in_arg_names);
    } else if (unit.ast && unit.ast->nodetype == node_mfile_func) {
        const auto func_ast = std::static_pointer_cast<mFileFunc>(unit.ast);
        function.set_input_names(collect_name_list(func_ast->in_args()));
    }

    if (unit.m_out_arg_names != nullptr && !unit.m_out_arg_names->empty()) {
        function.set_output_names(*unit.m_out_arg_names);
    } else if (unit.ast && unit.ast->nodetype == node_mfile_func) {
        const auto func_ast = std::static_pointer_cast<mFileFunc>(unit.ast);
        function.set_output_names(collect_name_list(func_ast->out_args()));
    }

    function.set_has_varargin(has_trailing_name(function.inputs(), "varargin"));
    function.set_has_varargout(has_trailing_name(function.outputs(), "varargout"));
}

struct LoweringContext {
    struct LoopContext {
        BasicBlock* break_target = nullptr;
        BasicBlock* continue_target = nullptr;
    };

    Function* function = nullptr;
    BasicBlock* current_block = nullptr;
    std::size_t next_temp_id = 0;
    std::size_t next_block_id = 0;
    std::vector<LoopContext> loop_stack;
    std::unordered_set<std::string> defined_user_names;
    std::unordered_set<std::string> active_global_names;

    NamedValue classify_name(const std::string& name) const {
        return NamedValue{name, NamedValue::UserVariable};
    }

    bool is_known_user_name(const std::string& name) const {
        return defined_user_names.find(name) != defined_user_names.end();
    }

    bool is_active_global_name(const std::string& name) const {
        return active_global_names.find(name) != active_global_names.end();
    }

    void activate_global_name(const std::string& name) {
        active_global_names.insert(name);
    }

    void mark_defined(const NamedValue& value) {
        if (value.type == NamedValue::UserVariable) {
            defined_user_names.insert(value.name);
        }
    }

    void mark_defined(const std::vector<NamedValue>& values) {
        for (const NamedValue& value : values) {
            mark_defined(value);
        }
    }

    NamedValue create_temp(std::string prefix = "__t") {
        return NamedValue{prefix + std::to_string(next_temp_id++), NamedValue::Temporary};
    }

    NamedValue create_hidden_name(const std::string& prefix) {
        return create_temp("__" + prefix + ".");
    }

    std::string create_hidden_symbol(const std::string& prefix) {
        return "__" + prefix + "." + std::to_string(next_temp_id++);
    }

    BasicBlock* create_block(const std::string& base_name) {
        if (function == nullptr) {
            throw std::runtime_error("non-SSA lower 当前没有激活函数。");
        }

        std::string name = base_name;
        if (base_name != "entry") {
            name += "." + std::to_string(next_block_id++);
        }
        return function->create_block(std::move(name));
    }

    template <typename T, typename... Args>
    T* append_node(Args&&... args) {
        if (function == nullptr || current_block == nullptr) {
            throw std::runtime_error("non-SSA lower 当前没有激活基本块。");
        }

        T* node = function->create_node<T>(std::forward<Args>(args)...);
        current_block->append_instruction(node);
        return node;
    }

    template <typename T, typename... Args>
    T* set_terminal(Args&&... args) {
        if (function == nullptr || current_block == nullptr) {
            throw std::runtime_error("non-SSA lower 当前没有激活基本块。");
        }

        T* node = function->create_node<T>(std::forward<Args>(args)...);
        current_block->set_terminal(node);
        return node;
    }
};

BinOpNode::Op lower_binop_type(nodeType type) {
    switch (type) {
        case node_add:
            return BinOpNode::Add;
        case node_subtract:
            return BinOpNode::Subtract;
        case node_eq:
            return BinOpNode::Eq;
        case node_geq:
            return BinOpNode::Ge;
        case node_greater_than:
            return BinOpNode::Gt;
        case node_leq:
            return BinOpNode::Le;
        case node_less_than:
            return BinOpNode::Lt;
        case node_noteq:
            return BinOpNode::Ne;
        case node_logic_and:
            return BinOpNode::And;
        case node_logic_or:
            return BinOpNode::Or;
        case node_element_ldiv:
            return BinOpNode::LDivide;
        case node_element_power:
            return BinOpNode::Power;
        case node_left_divide:
            return BinOpNode::MLeftDivide;
        case node_power:
            return BinOpNode::MPower;
        case node_right_divide:
            return BinOpNode::MRightDivide;
        case node_element_mul:
            return BinOpNode::Times;
        case node_multiply:
            return BinOpNode::Multiply;
        case node_element_rdiv:
            return BinOpNode::RDivide;
        default:
            break;
    }

    throw std::runtime_error("non-SSA lower 遇到了暂不支持的二元运算。");
}

UnaryOpNode::Op lower_unaryop_type(nodeType type) {
    switch (type) {
        case node_logic_not:
            return UnaryOpNode::Logic_Not;
        case node_uplus:
            return UnaryOpNode::UPlus;
        case node_negative:
            return UnaryOpNode::UMinus;
        case node_transpose:
            return UnaryOpNode::Transpose;
        case node_ctranspose:
            return UnaryOpNode::CTranspose;
        default:
            break;
    }

    throw std::runtime_error("non-SSA lower 遇到了暂不支持的一元运算。");
}

NumberNode::NumberValue parse_number_value(const std::shared_ptr<numval>& number_node) {
    std::string text = number_node->str;
    text.erase(std::remove_if(text.begin(), text.end(),
                              [](unsigned char ch) { return std::isspace(ch) != 0; }),
               text.end());
    if (text.empty()) {
        throw std::runtime_error("non-SSA lower 遇到了空数字字面量。");
    }

    const char suffix = text.back();
    if (suffix == 'i' || suffix == 'j' || suffix == 'I' || suffix == 'J') {
        const std::string imag_text = text.substr(0, text.size() - 1);
        double imag_value = 0.0;
        if (imag_text.empty() || imag_text == "+") {
            imag_value = 1.0;
        } else if (imag_text == "-") {
            imag_value = -1.0;
        } else {
            imag_value = std::stod(imag_text);
        }
        return std::complex<double>{0.0, imag_value};
    }

    return std::stod(text);
}

NamedValue lower_expr_to_operand(const ast_ptr& node, LoweringContext& ctx);
void lower_expr_into(const ast_ptr& node, const NamedValue& target, LoweringContext& ctx);
void lower_stmt(const ast_ptr& node, LoweringContext& ctx);
bool should_treat_symref_as_user_value(const std::shared_ptr<symref>& sym,
                                       const LoweringContext& ctx);
std::vector<ast_ptr> collect_cell_elements(const ast_ptr& node);
std::vector<ast_ptr> collect_cell_index_nodes(const ast_ptr& node, bool has_assignment_value);
void ensure_fallthrough_to(LoweringContext& ctx, BasicBlock* target, const ast_ptr& node);

struct MagicEndIndexInfo {
    NamedValue base;
    std::size_t index_position = 0;
    std::size_t total_index_count = 0;
};

MagicEndIndexInfo resolve_magic_end_index_info(const ast* end_node, LoweringContext& ctx) {
    const ast* index_root = nullptr;
    for (const ast* cursor = end_node != nullptr ? end_node->parent : nullptr; cursor != nullptr;
         cursor = cursor->parent) {
        if (cursor->nodetype == node_cell_get || cursor->nodetype == node_cell_set) {
            index_root = cursor;
            break;
        }
        if (cursor->nodetype == node_multiple_func) {
            const ast_ptr& callee_node = cursor->branch.size() > 1 ? cursor->branch[1] : ast_ptr{};
            if (callee_node != nullptr && callee_node->nodetype == node_name &&
                should_treat_symref_as_user_value(std::static_pointer_cast<symref>(callee_node),
                                                  ctx)) {
                index_root = cursor;
                break;
            }
        }
    }

    if (index_root == nullptr) {
        throw std::runtime_error("non-SSA lower `end` 只能出现在索引表达式中。");
    }

    MagicEndIndexInfo info;
    std::vector<const ast*> index_nodes;
    const auto append_index_nodes = [&index_nodes](const ast* node) {
        if (node == nullptr) {
            return;
        }

        if (node->nodetype == node_list || node->nodetype == node_horz_list) {
            for (const ast_ptr& branch : node->branch) {
                if (branch != nullptr) {
                    index_nodes.push_back(branch.get());
                }
            }
            return;
        }

        index_nodes.push_back(node);
    };

    switch (index_root->nodetype) {
        case node_cell_get:
        case node_cell_set: {
            const ast_ptr& base_node = index_root->branch[0];
            if (base_node != nullptr && base_node->nodetype == node_name &&
                should_treat_symref_as_user_value(std::static_pointer_cast<symref>(base_node), ctx)) {
                info.base = ctx.classify_name(std::static_pointer_cast<symref>(base_node)->name());
            } else {
                info.base = lower_expr_to_operand(base_node, ctx);
            }
            append_index_nodes(index_root->branch[1].get());
            break;
        }
        case node_multiple_func: {
            const ast_ptr& callee_node = index_root->branch[1];
            if (callee_node->nodetype != node_name) {
                break;
            }
            const auto callee_sym = std::static_pointer_cast<symref>(callee_node);
            if (!should_treat_symref_as_user_value(callee_sym, ctx)) {
                break;
            }
            info.base = ctx.classify_name(callee_sym->name());
            append_index_nodes(index_root->branch[2].get());
            break;
        }
        default:
            break;
    }

    if (index_nodes.empty()) {
        throw std::runtime_error("non-SSA lower 无法为 `end` 找到索引表达式列表。");
    }

    for (std::size_t i = 0; i < index_nodes.size(); ++i) {
        for (const ast* cursor = end_node; cursor != nullptr; cursor = cursor->parent) {
            if (cursor == index_nodes[i]) {
                info.index_position = i;
                info.total_index_count = index_nodes.size();
                return info;
            }
            if (cursor == index_root) {
                break;
            }
        }
    }

    throw std::runtime_error("non-SSA lower 无法从原 AST 中定位 `end` 的索引位置。");
}

void lower_return_stmt(const ast_ptr& node, LoweringContext& ctx) {
    if (ctx.function == nullptr || ctx.current_block == nullptr) {
        throw std::runtime_error("non-SSA lower 当前没有可返回的基本块。");
    }

    ctx.set_terminal<ReturnNode>(ctx.function->outputs(), source_location_from(node));
    ctx.current_block = nullptr;
}

void append_bool_assignment(const NamedValue& target, bool value, LoweringContext& ctx,
                            const ast_ptr& node) {
    ctx.append_node<NumberNode>(target, value, source_location_from(node));
}

bool should_treat_symref_as_user_value(const std::shared_ptr<symref>& sym,
                                       const LoweringContext& ctx) {
    return sym != nullptr &&
           (sym->get_symbol_type() == symbol_variable || ctx.is_known_user_name(sym->name()));
}

std::vector<NamedValue> lower_call_inputs(const ast_ptr& input_args, LoweringContext& ctx) {
    std::vector<NamedValue> inputs;
    if (!input_args) {
        return inputs;
    }

    if (input_args->nodetype == node_list || input_args->nodetype == node_horz_list) {
        inputs.reserve(input_args->branch.size());
        for (const ast_ptr& branch : input_args->branch) {
            inputs.push_back(lower_expr_to_operand(branch, ctx));
        }
        return inputs;
    }

    inputs.push_back(lower_expr_to_operand(input_args, ctx));
    return inputs;
}

CallNode::CalleeType lower_multiple_func_callee_type(const std::shared_ptr<multipleFuncCall>& call,
                                                     const LoweringContext& ctx) {
    if (!call || call->s() == nullptr || call->s()->nodetype != node_name) {
        return CallNode::Direct;
    }

    const auto callee_sym = std::static_pointer_cast<symref>(call->s());
    return should_treat_symref_as_user_value(callee_sym, ctx) ? CallNode::Indirect
                                                              : CallNode::Direct;
}

void lower_short_circuit_expr_into(const ast_ptr& node, const NamedValue& target,
                                   LoweringContext& ctx) {
    if (!node || node->branch.size() < 2) {
        throw std::runtime_error("non-SSA lower 遇到了不完整的短路逻辑表达式。");
    }

    if (ctx.current_block == nullptr) {
        throw std::runtime_error("non-SSA lower 当前没有激活基本块。");
    }

    const bool is_short_or = node->nodetype == node_logic_or_short;
    const NamedValue lhs = lower_expr_to_operand(node->branch[0], ctx);
    if (ctx.current_block == nullptr) {
        throw std::runtime_error("non-SSA lower 短路逻辑左操作数没有落到有效基本块。");
    }

    BasicBlock* short_block =
        ctx.create_block(is_short_or ? "logic.or.short.short" : "logic.and.short.short");
    BasicBlock* rhs_block =
        ctx.create_block(is_short_or ? "logic.or.short.rhs" : "logic.and.short.rhs");
    BasicBlock* rhs_true_block =
        ctx.create_block(is_short_or ? "logic.or.short.rhs.true" : "logic.and.short.rhs.true");
    BasicBlock* rhs_false_block =
        ctx.create_block(is_short_or ? "logic.or.short.rhs.false" : "logic.and.short.rhs.false");
    BasicBlock* merge_block =
        ctx.create_block(is_short_or ? "logic.or.short.end" : "logic.and.short.end");

    BasicBlock* lhs_true_block = is_short_or ? short_block : rhs_block;
    BasicBlock* lhs_false_block = is_short_or ? rhs_block : short_block;
    ctx.current_block->add_successor(lhs_true_block);
    ctx.current_block->add_successor(lhs_false_block);
    ctx.set_terminal<CondJumpNode>(lhs, lhs_true_block, lhs_false_block, source_location_from(node));

    ctx.current_block = short_block;
    append_bool_assignment(target, is_short_or, ctx, node);
    ensure_fallthrough_to(ctx, merge_block, node);

    ctx.current_block = rhs_block;
    const NamedValue rhs = lower_expr_to_operand(node->branch[1], ctx);
    if (ctx.current_block == nullptr) {
        throw std::runtime_error("non-SSA lower 短路逻辑右操作数没有落到有效基本块。");
    }
    ctx.current_block->add_successor(rhs_true_block);
    ctx.current_block->add_successor(rhs_false_block);
    ctx.set_terminal<CondJumpNode>(rhs, rhs_true_block, rhs_false_block, source_location_from(node));

    ctx.current_block = rhs_true_block;
    append_bool_assignment(target, true, ctx, node);
    ensure_fallthrough_to(ctx, merge_block, node);

    ctx.current_block = rhs_false_block;
    append_bool_assignment(target, false, ctx, node);
    ensure_fallthrough_to(ctx, merge_block, node);

    ctx.current_block = merge_block;
    ctx.mark_defined(target);
}

Function* create_anonymous_function(const ast_ptr& node, LoweringContext& ctx) {
    if (!node || node->branch.size() < 2) {
        throw std::runtime_error("non-SSA lower 暂不支持空体匿名函数。");
    }
    if (ctx.function == nullptr || ctx.function->parent() == nullptr) {
        throw std::runtime_error("non-SSA lower 匿名函数时找不到模块。");
    }

    Module* module = ctx.function->parent();
    Function* function =
        module->create_function(ctx.create_hidden_symbol("anonymous"), Function::LocalFunction);
    function->set_input_names(collect_name_list(node->branch[0]));
    function->set_output_names({"__anon_result"});

    BasicBlock* entry = function->create_block("entry");
    function->set_entry_block(entry);

    LoweringContext anon_ctx;
    anon_ctx.function = function;
    anon_ctx.current_block = entry;
    anon_ctx.next_temp_id = ctx.next_temp_id;
    anon_ctx.mark_defined(function->inputs());
    anon_ctx.mark_defined(function->outputs());

    lower_expr_into(node->branch[1], anon_ctx.classify_name("__anon_result"), anon_ctx);
    anon_ctx.set_terminal<ReturnNode>(function->outputs(), source_location_from(node));

    ctx.next_temp_id = anon_ctx.next_temp_id;
    return function;
}

void lower_name_expr_into(const std::shared_ptr<symref>& sym, const NamedValue& target,
                          LoweringContext& ctx, const ast_ptr& node) {
    if (sym != nullptr && ctx.is_active_global_name(sym->name())) {
        ctx.append_node<GlobalLoadNode>(target, sym->name(), source_location_from(node));
        ctx.mark_defined(target);
        return;
    }

    if (should_treat_symref_as_user_value(sym, ctx)) {
        const NamedValue source = ctx.classify_name(sym->name());
        if (source.name != target.name || source.type != target.type) {
            ctx.append_node<AssignNode>(target, source, source_location_from(node));
        }
        ctx.mark_defined(target);
        return;
    }

    ctx.append_node<CallNode>(CallNode::Direct, sym->name(), std::vector<NamedValue>{target},
                              std::vector<NamedValue>{}, source_location_from(node));
    ctx.mark_defined(target);
}

NamedValue lower_struct_field_selector_to_operand(const ast_ptr& node, LoweringContext& ctx) {
    if (!node) {
        throw std::runtime_error("non-SSA lower struct 字段名不能为空。");
    }

    if (node->nodetype == node_name) {
        const auto sym = std::static_pointer_cast<symref>(node);
        if (!node->paren && !should_treat_symref_as_user_value(sym, ctx)) {
            const NamedValue field_name = ctx.create_hidden_name("struct.field");
            ctx.append_node<TextNode>(field_name, sym->name(), source_location_from(node));
            ctx.mark_defined(field_name);
            return field_name;
        }
    }

    return lower_expr_to_operand(node, ctx);
}

NamedValue lower_struct_set_result(const ast_ptr& base_node, const ast_ptr& field_node,
                                   const NamedValue& value, LoweringContext& ctx,
                                   const ast_ptr& location_node) {
    if (!base_node || !field_node) {
        throw std::runtime_error("non-SSA lower 遇到了不完整的 struct 写回节点。");
    }

    const NamedValue base = lower_expr_to_operand(base_node, ctx);
    const NamedValue field = lower_struct_field_selector_to_operand(field_node, ctx);
    const NamedValue updated_base = ctx.create_temp("__struct.set");

    std::vector<NamedValue> inputs;
    inputs.push_back(base);
    inputs.push_back(field);
    inputs.push_back(value);

    ctx.append_node<CallNode>(CallNode::Direct, "setfield",
                              std::vector<NamedValue>{updated_base}, std::move(inputs),
                              source_location_from(location_node));
    ctx.mark_defined(updated_base);
    return updated_base;
}

void lower_store_back_to_lvalue(const ast_ptr& lhs, const NamedValue& value, LoweringContext& ctx) {
    if (!lhs) {
        throw std::runtime_error("non-SSA lower 不能把值写回到空左值。");
    }

    switch (lhs->nodetype) {
        case node_name: {
            const auto sym = std::static_pointer_cast<symref>(lhs);
            if (ctx.is_active_global_name(sym->name())) {
                ctx.append_node<GlobalStoreNode>(sym->name(), value, source_location_from(lhs));
                return;
            }

            const NamedValue target = ctx.classify_name(sym->name());
            if (target.name != value.name || target.type != value.type) {
                ctx.append_node<AssignNode>(target, value, source_location_from(lhs));
            }
            ctx.mark_defined(target);
            return;
        }
        case node_struct_get: {
            const NamedValue updated_base =
                lower_struct_set_result(lhs->branch[0], lhs->branch[1], value, ctx, lhs);
            lower_store_back_to_lvalue(lhs->branch[0], updated_base, ctx);
            return;
        }
        default:
            break;
    }

    throw std::runtime_error("non-SSA lower phase2 暂不支持该左值写回形式。");
}

void lower_call_stmt(const std::shared_ptr<multipleFuncCall>& call, LoweringContext& ctx) {
    const std::vector<CallOutputTargetSpec> output_specs =
        collect_call_output_target_specs(call->out_args());

    std::vector<NamedValue> outputs;
    outputs.reserve(output_specs.size());
    for (const CallOutputTargetSpec& spec : output_specs) {
        const bool requires_store_back =
            spec.requires_store_back ||
            (spec.lhs != nullptr && spec.lhs->nodetype == node_name &&
             ctx.is_active_global_name(spec.name));
        // 纯名字左值可以直接接 call 输出；
        // 带索引左值则必须先接到临时，后面再显式 lower 成 `__ir_paren_set__`。
        outputs.push_back(requires_store_back ? ctx.create_temp("__call.out")
                                              : ctx.classify_name(spec.name));
    }

    std::vector<NamedValue> inputs = lower_call_inputs(call->in_args(), ctx);
    const CallNode::CalleeType callee_type = lower_multiple_func_callee_type(call, ctx);
    const std::vector<NamedValue> call_results = outputs;
    ctx.append_node<CallNode>(callee_type, call->name(), std::move(outputs), std::move(inputs),
                              source_location_from(call));
    ctx.mark_defined(call_results);

    for (std::size_t i = 0; i < output_specs.size(); ++i) {
        const CallOutputTargetSpec& spec = output_specs[i];
        if (!spec.requires_store_back &&
            !(spec.lhs != nullptr && spec.lhs->nodetype == node_name &&
              ctx.is_active_global_name(spec.name))) {
            continue;
        }

        if (spec.lhs != nullptr && spec.lhs->nodetype == node_name && !spec.requires_store_back) {
            lower_store_back_to_lvalue(spec.lhs, call_results[i], ctx);
            continue;
        }

        const auto lhs_call = std::static_pointer_cast<multipleFuncCall>(spec.lhs);
        const NamedValue target = ctx.classify_name(spec.name);
        if (ctx.is_active_global_name(target.name)) {
            throw std::runtime_error("non-SSA lower phase1 暂不支持对 global 变量做索引写回。");
        }

        std::vector<NamedValue> set_inputs;
        set_inputs.push_back(target);

        std::vector<NamedValue> block_indices = lower_call_inputs(lhs_call->in_args(), ctx);
        set_inputs.insert(set_inputs.end(), std::make_move_iterator(block_indices.begin()),
                          std::make_move_iterator(block_indices.end()));
        // 返回值必须以“call 先全部完成，再按左值顺序回写”的方式展开：
        // 这样既保留多返回值求值顺序，也复用已有 `__ir_paren_set__` 桥接语义。
        set_inputs.push_back(call_results[i]);

        ctx.append_node<CallNode>(CallNode::Direct, "__ir_paren_set__",
                                  std::vector<NamedValue>{target}, std::move(set_inputs),
                                  source_location_from(spec.lhs));
        ctx.mark_defined(target);
    }
}

bool is_block_assignment_lhs(const ast_ptr& node) {
    if (node == nullptr || node->nodetype != node_multiple_func) {
        return false;
    }

    const auto call = std::static_pointer_cast<multipleFuncCall>(node);
    // `node_asgn` 左值是否表示 `A(...) = rhs`，只看：
    //   1) 左值本身是 `node_multiple_func`
    //   2) `node_multiple_func` 的 `branch[1]` / `s()` 是 `node_name`
    // 不依赖 parser 是否把 `mfc_type` 标成 `mfc_name_element`。
    return call->s() != nullptr && call->s()->nodetype == node_name;
}

void lower_block_assignment_stmt(const std::shared_ptr<symasgn>& assign, LoweringContext& ctx) {
    const auto lhs_call = std::static_pointer_cast<multipleFuncCall>(assign->s());
    const NamedValue target = ctx.classify_name(lhs_call->name());
    if (ctx.is_active_global_name(target.name)) {
        throw std::runtime_error("non-SSA lower phase1 暂不支持对 global 变量做圆括号写回。");
    }

    // `A(...) = rhs` 在 AST 中会被编码成 `node_asgn(node_multiple_func, rhs)`；
    // 这里可以在 lowering 阶段就唯一化成 `__ir_paren_set__`：
    // 赋值语境已经保证它不是普通函数调用，只可能是圆括号下标写入。
    // 后续由解释器桥接到 runtime `block set`，并返回新的 SSA 版本值。
    std::vector<NamedValue> inputs;
    inputs.push_back(target);

    std::vector<NamedValue> block_indices = lower_call_inputs(lhs_call->in_args(), ctx);
    inputs.insert(inputs.end(), std::make_move_iterator(block_indices.begin()),
                  std::make_move_iterator(block_indices.end()));
    inputs.push_back(lower_expr_to_operand(assign->v(), ctx));

    ctx.append_node<CallNode>(CallNode::Direct, "__ir_paren_set__",
                              std::vector<NamedValue>{target}, std::move(inputs),
                              source_location_from(assign));
    ctx.mark_defined(target);
}

void lower_expr_into(const ast_ptr& node, const NamedValue& target, LoweringContext& ctx) {
    if (!node) {
        throw std::runtime_error("non-SSA lower 不能处理空表达式。");
    }

    switch (node->nodetype) {
        case node_name: {
            const auto sym = std::static_pointer_cast<symref>(node);
            lower_name_expr_into(sym, target, ctx, node);
            return;
        }
        case node_number:
            ctx.append_node<NumberNode>(target, parse_number_value(std::static_pointer_cast<numval>(node)),
                                        source_location_from(node));
            ctx.mark_defined(target);
            return;
        case node_horz_list:
        case node_vert_list: {
            // TODO(opt): 在正式的 ConstantFold pass 中识别“所有元素都是常量”的
            // `horzcat/vertcat`，例如 `[1 2 3]`、`[1; 2; 3]`，直接折叠成常量矩阵，
            // 避免继续保留运行时 `horzcat/vertcat` 调用。这里先保持 lowering 只做语义展开。
            std::vector<NamedValue> inputs;
            inputs.reserve(node->branch.size());
            for (const ast_ptr& branch : node->branch) {
                inputs.push_back(lower_expr_to_operand(branch, ctx));
            }

            ctx.append_node<CallNode>(CallNode::Direct,
                                      node->nodetype == node_horz_list ? "horzcat" : "vertcat",
                                      std::vector<NamedValue>{target}, std::move(inputs),
                                      source_location_from(node));
            ctx.mark_defined(target);
            return;
        }
        case node_cell: {
            std::vector<NamedValue> inputs;
            for (const ast_ptr& item : collect_cell_elements(node)) {
                inputs.push_back(lower_expr_to_operand(item, ctx));
            }

            ctx.append_node<CallNode>(CallNode::Direct, "__ir_make_cell__",
                                      std::vector<NamedValue>{target}, std::move(inputs),
                                      source_location_from(node));
            ctx.mark_defined(target);
            return;
        }
        case node_cell_get: {
            std::vector<NamedValue> inputs;
            inputs.push_back(lower_expr_to_operand(node->branch[0], ctx));
            for (const ast_ptr& index_node : collect_cell_index_nodes(node, false)) {
                inputs.push_back(lower_expr_to_operand(index_node, ctx));
            }

            ctx.append_node<CallNode>(CallNode::Direct, "__ir_cell_get__",
                                      std::vector<NamedValue>{target}, std::move(inputs),
                                      source_location_from(node));
            ctx.mark_defined(target);
            return;
        }
        case node_struct_get: {
            std::vector<NamedValue> inputs;
            // `a.b.c` 会解析成嵌套的 `node_struct_get`：
            // 外层 `branch[0]` 仍然是一个 `node_struct_get(a, b)`，
            // 这里通过递归 lower base，把整条 dot 访问链逐层展开成 `getfield`。
            inputs.push_back(lower_expr_to_operand(node->branch[0], ctx));
            inputs.push_back(lower_struct_field_selector_to_operand(node->branch[1], ctx));

            ctx.append_node<CallNode>(CallNode::Direct, "getfield",
                                      std::vector<NamedValue>{target}, std::move(inputs),
                                      source_location_from(node));
            ctx.mark_defined(target);
            return;
        }
        case node_text:
        case node_char_mat: {
            const auto text = std::static_pointer_cast<textNode>(node);
            ctx.append_node<TextNode>(target, text->str, source_location_from(node));
            ctx.mark_defined(target);
            return;
        }
        case node_uplus:
        case node_negative:
        case node_logic_not:
        case node_transpose:
        case node_ctranspose: {
            const NamedValue operand = lower_expr_to_operand(node->branch[0], ctx);
            const UnaryOpNode::Op op = lower_unaryop_type(node->nodetype);
            ctx.append_node<UnaryOpNode>(op, target, operand, source_location_from(node));
            ctx.mark_defined(target);
            return;
        }
        case node_add:
        case node_subtract:
        case node_element_ldiv:
        case node_element_mul:
        case node_element_rdiv:
        case node_multiply:
        case node_left_divide:
        case node_right_divide:
        case node_element_power:
        case node_power:
        case node_eq:
        case node_geq:
        case node_greater_than:
        case node_leq:
        case node_less_than:
        case node_noteq:
        case node_logic_and:
        case node_logic_or: {
            const NamedValue lhs = lower_expr_to_operand(node->branch[0], ctx);
            const NamedValue rhs = lower_expr_to_operand(node->branch[1], ctx);
            ctx.append_node<BinOpNode>(lower_binop_type(node->nodetype), target, lhs, rhs,
                                       source_location_from(node));
            ctx.mark_defined(target);
            return;
        }
        case node_logic_or_short:
        case node_logic_and_short:
            lower_short_circuit_expr_into(node, target, ctx);
            return;
        case node_multiple_func: {
            const auto call = std::static_pointer_cast<multipleFuncCall>(node);
            std::vector<NamedValue> inputs = lower_call_inputs(call->in_args(), ctx);
            // `A(...)` 在表达式位置不能提前 lower 成固定的 `block get`：
            // 它既可能是函数/函数句柄调用，也可能是矩阵/元胞等对象的圆括号取值。
            // 已知是“变量值”的裸名字会先 lower 成 indirect call；
            // 运行时再根据该值是否是 `function_handle` 分派到函数调用或 `block get`。
            const CallNode::CalleeType callee_type = lower_multiple_func_callee_type(call, ctx);
            ctx.append_node<CallNode>(callee_type, call->name(), std::vector<NamedValue>{target},
                                      std::move(inputs), source_location_from(node));
            ctx.mark_defined(target);
            return;
        }
        case node_magic_end: {
            const MagicEndIndexInfo index_info = resolve_magic_end_index_info(node.get(), ctx);
            const NamedValue index_position_literal =
                ctx.create_hidden_name("magic_end.index.literal");
            ctx.append_node<NumberNode>(
                index_position_literal, static_cast<std::int64_t>(index_info.index_position),
                source_location_from(node));
            ctx.mark_defined(index_position_literal);

            const NamedValue total_index_count_literal =
                ctx.create_hidden_name("magic_end.total.literal");
            ctx.append_node<NumberNode>(
                total_index_count_literal, static_cast<std::int64_t>(index_info.total_index_count),
                source_location_from(node));
            ctx.mark_defined(total_index_count_literal);

            ctx.append_node<CallNode>(
                CallNode::Direct, "magic_end", std::vector<NamedValue>{target},
                std::vector<NamedValue>{index_info.base, index_position_literal,
                                        total_index_count_literal},
                source_location_from(node));
            ctx.mark_defined(target);
            return;
        }
        case node_anonymous_func: {
            Function* function = create_anonymous_function(node, ctx);
            const NamedValue function_name = ctx.create_temp("__anon.name");
            ctx.append_node<TextNode>(function_name, function->name(), source_location_from(node));
            ctx.mark_defined(function_name);
            ctx.append_node<CallNode>(CallNode::Direct, "__ir_make_function_handle__",
                                      std::vector<NamedValue>{target},
                                      std::vector<NamedValue>{function_name},
                                      source_location_from(node));
            ctx.mark_defined(target);
            return;
        }
        case node_magic_colon: {
            // `A(:, ...)` 里的裸 `:` 在 AST 中是独立的 `node_magic_colon`；
            // runtime `block` 约定直接接收字符矩阵 `":"` 作为整维切片哨兵。
            ctx.append_node<TextNode>(target, ":", source_location_from(node));
            ctx.mark_defined(target);
            return;
        }
        case node_colon: {
            if (node->branch.size() != 2 && node->branch.size() != 3) {
                throw std::runtime_error("non-SSA lower 暂不支持该冒号表达式。");
            }

            std::vector<NamedValue> inputs;
            inputs.reserve(node->branch.size());
            for (const ast_ptr& branch : node->branch) {
                inputs.push_back(lower_expr_to_operand(branch, ctx));
            }

            ctx.append_node<CallNode>(CallNode::Direct, "colon", std::vector<NamedValue>{target},
                                      std::move(inputs), source_location_from(node));
            ctx.mark_defined(target);
            return;
        }
        default:
            break;
    }

    throw std::runtime_error(std::string("non-SSA lower 暂不支持该表达式节点: ") +
                             ast_node_type_name(node->nodetype));
}

NamedValue lower_expr_to_operand(const ast_ptr& node, LoweringContext& ctx) {
    if (!node) {
        throw std::runtime_error("non-SSA lower 不能处理空操作数表达式。");
    }

    if (node->nodetype == node_name) {
        const auto sym = std::static_pointer_cast<symref>(node);
        if (ctx.is_active_global_name(sym->name())) {
            const NamedValue temp = ctx.create_temp("__global.load");
            lower_name_expr_into(sym, temp, ctx, node);
            return temp;
        }
        if (should_treat_symref_as_user_value(sym, ctx)) {
            return ctx.classify_name(sym->name());
        }

        const NamedValue temp = ctx.create_temp();
        lower_name_expr_into(sym, temp, ctx, node);
        return temp;
    }

    const NamedValue temp = ctx.create_temp();
    lower_expr_into(node, temp, ctx);
    return temp;
}

void ensure_fallthrough_to(LoweringContext& ctx, BasicBlock* target, const ast_ptr& node) {
    if (ctx.current_block != nullptr && ctx.current_block->terminal() == nullptr) {
        ctx.current_block->add_successor(target);
        ctx.set_terminal<JumpNode>(target, std::nullopt);
    }
}

std::vector<ast_ptr> collect_cell_elements(const ast_ptr& node) {
    std::vector<ast_ptr> items;
    if (!node) {
        return items;
    }

    if (node->nodetype == node_cell || node->nodetype == node_list || node->nodetype == node_horz_list) {
        for (const ast_ptr& branch : node->branch) {
            std::vector<ast_ptr> nested = collect_cell_elements(branch);
            items.insert(items.end(), nested.begin(), nested.end());
        }
        return items;
    }

    items.push_back(node);
    return items;
}

std::vector<ast_ptr> collect_cell_index_nodes(const ast_ptr& node, bool has_assignment_value) {
    std::vector<ast_ptr> indices;
    if (!node || node->branch.size() <= 1) {
        return indices;
    }

    const std::size_t end =
        has_assignment_value ? node->branch.size() - 1 : node->branch.size();
    for (std::size_t i = 1; i < end; ++i) {
        const ast_ptr& branch = node->branch[i];
        if (branch != nullptr &&
            (branch->nodetype == node_list || branch->nodetype == node_horz_list)) {
            indices.insert(indices.end(), branch->branch.begin(), branch->branch.end());
            continue;
        }
        indices.push_back(branch);
    }

    return indices;
}

NamedValue build_switch_match_cond(const NamedValue& switch_value, const ast_ptr& match_node,
                                   LoweringContext& ctx) {
    std::vector<ast_ptr> match_items;
    if (match_node != nullptr && match_node->nodetype == node_cell) {
        match_items = collect_cell_elements(match_node);
    } else if (match_node != nullptr) {
        match_items.push_back(match_node);
    }

    if (match_items.empty()) {
        throw std::runtime_error("switch case 缺少匹配值。");
    }

    NamedValue combined_cond;
    bool has_combined = false;
    for (const ast_ptr& item : match_items) {
        const NamedValue rhs = lower_expr_to_operand(item, ctx);
        const NamedValue eq = ctx.create_temp("__switch.match");
        ctx.append_node<CallNode>(CallNode::Direct, "switch_case_match", std::vector<NamedValue>{eq},
                                  std::vector<NamedValue>{switch_value, rhs}, source_location_from(item));
        ctx.mark_defined(eq);

        if (!has_combined) {
            combined_cond = eq;
            has_combined = true;
            continue;
        }

        const NamedValue merged = ctx.create_temp("__switch.or");
        ctx.append_node<BinOpNode>(BinOpNode::Or, merged, combined_cond, eq, source_location_from(item));
        ctx.mark_defined(merged);
        combined_cond = merged;
    }

    return combined_cond;
}

void lower_switch_stmt(const std::shared_ptr<switch_flow>& switch_node, LoweringContext& ctx) {
    if (switch_node->expr() == nullptr || switch_node->cases() == nullptr) {
        throw std::runtime_error("non-SSA lower 不能处理空的 switch 语句。");
    }

    const NamedValue switch_value = lower_expr_to_operand(switch_node->expr(), ctx);
    BasicBlock* exit_block = ctx.create_block("switch.end");
    BasicBlock* dispatch_block = ctx.current_block;
    ast_ptr otherwise_node = nullptr;

    for (const ast_ptr& case_node : switch_node->cases()->branch) {
        if (!case_node) {
            continue;
        }
        if (case_node->nodetype == node_otherwise) {
            otherwise_node = case_node;
            continue;
        }
        if (case_node->nodetype != node_case || case_node->branch.size() < 2) {
            throw std::runtime_error("non-SSA lower 暂不支持该 switch 分支节点。");
        }

        ctx.current_block = dispatch_block;
        const NamedValue cond = build_switch_match_cond(switch_value, case_node->branch[0], ctx);
        BasicBlock* body_block = ctx.create_block("switch.case");
        BasicBlock* next_block = ctx.create_block("switch.next");
        ctx.current_block->add_successor(body_block);
        ctx.current_block->add_successor(next_block);
        ctx.set_terminal<CondJumpNode>(cond, body_block, next_block, std::nullopt);

        ctx.current_block = body_block;
        lower_stmt(case_node->branch[1], ctx);
        ensure_fallthrough_to(ctx, exit_block, case_node->branch[1]);

        dispatch_block = next_block;
    }

    ctx.current_block = dispatch_block;
    if (otherwise_node != nullptr && !otherwise_node->branch.empty()) {
        lower_stmt(otherwise_node->branch.back(), ctx);
    }
    ensure_fallthrough_to(ctx, exit_block, otherwise_node ? otherwise_node : switch_node);

    ctx.current_block = exit_block;
}

void lower_stmt_list(const ast_ptr& node, LoweringContext& ctx) {
    if (!node) {
        return;
    }

    for (const ast_ptr& branch : node->branch) {
        if (ctx.current_block == nullptr) {
            return;
        }
        lower_stmt(branch, ctx);
    }
}

void lower_if_stmt(const std::shared_ptr<if_flow>& if_node, LoweringContext& ctx) {
    const NamedValue cond = lower_expr_to_operand(if_node->cond(), ctx);
    BasicBlock* then_block = ctx.create_block("if.then");
    BasicBlock* else_block = ctx.create_block("if.else");
    BasicBlock* merge_block = ctx.create_block("if.end");

    ctx.current_block->add_successor(then_block);
    ctx.current_block->add_successor(else_block);
    ctx.set_terminal<CondJumpNode>(cond, then_block, else_block, std::nullopt);

    ctx.current_block = then_block;
    lower_stmt(if_node->tl(), ctx);
    ensure_fallthrough_to(ctx, merge_block, if_node->tl());

    ctx.current_block = else_block;
    if (if_node->el() && if_node->el()->nodetype != node_nop) {
        lower_stmt(if_node->el(), ctx);
        ensure_fallthrough_to(ctx, merge_block, if_node->el());
    } else {
        ensure_fallthrough_to(ctx, merge_block, if_node);
    }

    ctx.current_block = merge_block;
}

void lower_for_stmt(const std::shared_ptr<flow>& for_node, LoweringContext& ctx) {
    if (for_node->var_ref() == nullptr || for_node->var_ref()->nodetype != node_name) {
        throw std::runtime_error("non-SSA lower 目前只支持名字形式的 for 循环变量。");
    }
    if (!for_node->cond()) {
        throw std::runtime_error("non-SSA lower 不能处理空的 for 迭代表达式。");
    }

    const NamedValue loop_var =
        ctx.classify_name(std::static_pointer_cast<symref>(for_node->var_ref())->name());

    BasicBlock* preheader_block = ctx.create_block("for.preheader");
    BasicBlock* header_block = ctx.create_block("for.header");
    BasicBlock* body_block = ctx.create_block("for.body");
    BasicBlock* latch_block = ctx.create_block("for.latch");
    BasicBlock* exit_block = ctx.create_block("for.end");

    ensure_fallthrough_to(ctx, preheader_block, for_node);

    const NamedValue state_name = ctx.create_hidden_name("foreach_state");
    const NamedValue max_iter_name = ctx.create_hidden_name("foreach_max_iter");
    const NamedValue iter_index_name = ctx.create_hidden_name("foreach_iter_index");
    const NamedValue current_value_name = ctx.create_hidden_name("foreach_value");

    ctx.current_block = preheader_block;
    const NamedValue iterable = lower_expr_to_operand(for_node->cond(), ctx);
    ctx.append_node<CallNode>(CallNode::Direct, "foreach_init",
                              std::vector<NamedValue>{state_name, max_iter_name},
                              std::vector<NamedValue>{iterable}, source_location_from(for_node));
    ctx.append_node<NumberNode>(iter_index_name, std::int64_t{1}, source_location_from(for_node));
    ensure_fallthrough_to(ctx, header_block, for_node);

    ctx.current_block = header_block;
    const NamedValue done_name = ctx.create_hidden_name("foreach_done");
    ctx.append_node<BinOpNode>(BinOpNode::Lt, done_name, max_iter_name, iter_index_name,
                               source_location_from(for_node));
    header_block->add_successor(exit_block);
    header_block->add_successor(body_block);
    ctx.set_terminal<CondJumpNode>(done_name, exit_block, body_block, std::nullopt);

    ctx.current_block = body_block;
    ctx.append_node<CallNode>(CallNode::Direct, "foreach_iterate",
                              std::vector<NamedValue>{current_value_name},
                              std::vector<NamedValue>{state_name}, source_location_from(for_node));
    ctx.append_node<AssignNode>(loop_var, current_value_name, source_location_from(for_node->var_ref()));
    ctx.loop_stack.push_back({exit_block, latch_block});
    lower_stmt(for_node->tl(), ctx);
    ctx.loop_stack.pop_back();
    ensure_fallthrough_to(ctx, latch_block, for_node->tl());

    ctx.current_block = latch_block;
    const NamedValue one_name = ctx.create_hidden_name("foreach_one");
    ctx.append_node<NumberNode>(one_name, std::int64_t{1}, source_location_from(for_node));
    ctx.append_node<BinOpNode>(BinOpNode::Add, iter_index_name, iter_index_name, one_name,
                               source_location_from(for_node));
    ensure_fallthrough_to(ctx, header_block, for_node);

    ctx.current_block = exit_block;
}

void lower_while_stmt(const std::shared_ptr<if_flow>& while_node, LoweringContext& ctx) {
    if (while_node->cond() == nullptr) {
        throw std::runtime_error("non-SSA lower 不能处理空的 while 条件。");
    }

    BasicBlock* header_block = ctx.create_block("while.header");
    BasicBlock* body_block = ctx.create_block("while.body");
    BasicBlock* exit_block = ctx.create_block("while.end");

    ensure_fallthrough_to(ctx, header_block, while_node);

    ctx.current_block = header_block;
    const NamedValue cond = lower_expr_to_operand(while_node->cond(), ctx);
    header_block->add_successor(body_block);
    header_block->add_successor(exit_block);
    ctx.set_terminal<CondJumpNode>(cond, body_block, exit_block, std::nullopt);

    ctx.current_block = body_block;
    ctx.loop_stack.push_back({exit_block, header_block});
    lower_stmt(while_node->tl(), ctx);
    ctx.loop_stack.pop_back();
    ensure_fallthrough_to(ctx, header_block, while_node->tl());

    ctx.current_block = exit_block;
}

void lower_stmt(const ast_ptr& node, LoweringContext& ctx) {
    if (!node) {
        return;
    }

    switch (node->nodetype) {
        case node_runlist:
        case node_cmdlist:
        case node_list:
            lower_stmt_list(node, ctx);
            return;
        case node_asgn: {
            const auto assign = std::static_pointer_cast<symasgn>(node);
            if (is_block_assignment_lhs(assign->s())) {
                lower_block_assignment_stmt(assign, ctx);
                return;
            }
            if (assign->s() == nullptr || assign->s()->nodetype != node_name) {
                throw std::runtime_error("non-SSA lower 目前只支持名字左值赋值。");
            }
            if (ctx.is_active_global_name(assign->name())) {
                const NamedValue value = lower_expr_to_operand(assign->v(), ctx);
                ctx.append_node<GlobalStoreNode>(assign->name(), value, source_location_from(assign));
                return;
            }
            lower_expr_into(assign->v(), ctx.classify_name(assign->name()), ctx);
            return;
        }
        case node_struct_set: {
            const NamedValue value = lower_expr_to_operand(node->branch[2], ctx);
            // 这里保留 lhs base 的 AST，而不是先 lower 成单个值：
            // `setfield` 需要读取当前 base 值，而后续 store-back 还要沿着
            // `a` / `a.b` / `global a` 这条左值链逐层写回。
            const NamedValue updated_base =
                lower_struct_set_result(node->branch[0], node->branch[1], value, ctx, node);
            lower_store_back_to_lvalue(node->branch[0], updated_base, ctx);
            return;
        }
        case node_global:
            for (const ast_ptr& branch : node->branch) {
                if (branch == nullptr || branch->nodetype != node_name) {
                    throw std::runtime_error("non-SSA lower 目前只支持名字形式的 global 声明。");
                }
                ctx.activate_global_name(std::static_pointer_cast<symref>(branch)->name());
            }
            return;
        case node_cell_set: {
            if (node->branch.empty() || node->branch[0] == nullptr ||
                node->branch[0]->nodetype != node_name) {
                throw std::runtime_error("non-SSA lower 目前只支持名字基对象的元胞写入。");
            }

            const auto base_sym = std::static_pointer_cast<symref>(node->branch[0]);
            const NamedValue target = ctx.classify_name(base_sym->name());
            if (ctx.is_active_global_name(target.name)) {
                throw std::runtime_error("non-SSA lower phase1 暂不支持对 global 变量做元胞写回。");
            }

            std::vector<NamedValue> inputs;
            inputs.push_back(target);
            for (const ast_ptr& index_node : collect_cell_index_nodes(node, true)) {
                inputs.push_back(lower_expr_to_operand(index_node, ctx));
            }
            inputs.push_back(lower_expr_to_operand(node->branch.back(), ctx));

            ctx.append_node<CallNode>(CallNode::Direct, "__ir_cell_set__",
                                      std::vector<NamedValue>{target}, std::move(inputs),
                                      source_location_from(node));
            ctx.mark_defined(target);
            return;
        }
        case node_flow_if:
            lower_if_stmt(std::static_pointer_cast<if_flow>(node), ctx);
            return;
        case node_flow_switch:
            lower_switch_stmt(std::static_pointer_cast<switch_flow>(node), ctx);
            return;
        case node_for:
            lower_for_stmt(std::static_pointer_cast<flow>(node), ctx);
            return;
        case node_flow_while:
            lower_while_stmt(std::static_pointer_cast<if_flow>(node), ctx);
            return;
        case node_break:
            if (ctx.loop_stack.empty()) {
                throw std::runtime_error("break 只能出现在循环内部。");
            }
            ctx.current_block->add_successor(ctx.loop_stack.back().break_target);
            ctx.set_terminal<JumpNode>(ctx.loop_stack.back().break_target, std::nullopt);
            ctx.current_block = nullptr;
            return;
        case node_continue:
            if (ctx.loop_stack.empty()) {
                throw std::runtime_error("continue 只能出现在循环内部。");
            }
            ctx.current_block->add_successor(ctx.loop_stack.back().continue_target);
            ctx.set_terminal<JumpNode>(ctx.loop_stack.back().continue_target, std::nullopt);
            ctx.current_block = nullptr;
            return;
        case node_return:
            lower_return_stmt(node, ctx);
            return;
        case node_multiple_func:
            lower_call_stmt(std::static_pointer_cast<multipleFuncCall>(node), ctx);
            return;
        case node_nop:
        case node_empty:
        case node_comment:
        case node_andy_end_of_string:
            return;
        default:
            break;
    }

    throw std::runtime_error(std::string("non-SSA lower 暂不支持该语句节点: ") +
                             ast_node_type_name(node->nodetype));
}

void lower_unit_into_function(const pcdata& unit, Module& module) {
    Function* function =
        module.create_function(function_name_from_unit(unit), function_type_from_unit(unit));
    const CollectedFunctionNames collected_names =
        collect_function_predeclared_names(unit);
    populate_function_signature(*function, unit);
    if (function->type() == Function::Script && function->outputs().empty()) {
        function->set_output_names(to_sorted_name_list(collected_names.user_names));
    }
    if (function->type() == Function::Script ||
        function->type() == Function::PrimaryFunction) {
        module.set_entry_function(function);
    }

    BasicBlock* entry = function->create_block("entry");
    function->set_entry_block(entry);

    LoweringContext ctx;
    ctx.function = function;
    ctx.current_block = entry;
    ctx.defined_user_names = collected_names.user_names;
    ctx.active_global_names = collected_names.global_names;
    ctx.mark_defined(function->inputs());
    ctx.mark_defined(function->outputs());

    const ast_ptr body = unit.is_mscript() ? script_body_from_unit(unit) : function_body_from_unit(unit);
    lower_stmt(body, ctx);
    if (ctx.current_block != nullptr && ctx.current_block->terminal() == nullptr) {
        ctx.set_terminal<ReturnNode>(function->outputs(), std::nullopt);
    }
}

}  // namespace

Module lower_parsed_units_to_ir(
    const std::vector<std::shared_ptr<pcdata>>& parsed_units) {
    if (parsed_units.empty() || parsed_units.front() == nullptr) {
        throw std::runtime_error("non-SSA lower 没有可用的解析单元。");
    }

    const pcdata& first_unit = *parsed_units.front();
    Module module(module_name_from_unit(first_unit), first_unit.filename,
                        module_type_from_units(parsed_units));

    for (const std::shared_ptr<pcdata>& unit : parsed_units) {
        if (unit == nullptr || !unit->ast) {
            continue;
        }
        lower_unit_into_function(*unit, module);
    }

    if (module.entry_function() == nullptr) {
        throw std::runtime_error("non-SSA lower 失败：没有成功生成任何函数。");
    }

    return module;
}

}  // namespace baltam
