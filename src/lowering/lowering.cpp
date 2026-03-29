#include "lowering/lowering.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ast/ast_base.h"
#include "ast/flow_control.h"
#include "ast/mfile_func.h"
#include "ast/multi_func_call.h"
#include "ast/numval.h"
#include "ast/symref.h"
#include "ast/text_node.h"
#include "ba_obj/ba_obj.h"
#include "ba_obj/matrix.h"

namespace baltam {
namespace {

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

Module::Type module_type_from_units(const std::vector<std::shared_ptr<pcdata>>& parsed_units) {
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

    const std::string name = get_ast_sym_name(node, 0);
    if (name.empty()) {
        throw std::runtime_error("IR lower 暂不支持该名称列表节点。");
    }
    return {name};
}

struct LoweringContext {
    BasicBlock* current_block = nullptr;
    int next_block_id = 0;
    int next_hidden_id = 0;
    struct LoopContext {
        BasicBlock* break_target = nullptr;
        BasicBlock* continue_target = nullptr;
    };
    std::vector<LoopContext> loop_stack;
};

Function& current_function(LoweringContext& ctx) {
    if (ctx.current_block == nullptr || ctx.current_block->parent() == nullptr) {
        throw std::runtime_error("IR lower 时找不到当前函数。");
    }
    return *ctx.current_block->parent();
}

BasicBlock* create_block(LoweringContext& ctx, const std::string& prefix) {
    std::ostringstream oss;
    oss << prefix << "_" << ctx.next_block_id++;
    return current_function(ctx).create_block(oss.str());
}

Instruction* lower_expr(const ast_ptr& node, LoweringContext& ctx);
void lower_stmt(const ast_ptr& node, LoweringContext& ctx);

std::string create_hidden_name(LoweringContext& ctx, const std::string& prefix) {
    std::ostringstream oss;
    oss << "__" << prefix << "_" << ctx.next_hidden_id++;
    return oss.str();
}

void append_instruction(LoweringContext& ctx, Instruction* instruction) {
    if (ctx.current_block == nullptr) {
        throw std::runtime_error("IR lower 指令时找不到当前基本块。");
    }
    ctx.current_block->append_instruction(instruction);
}

Instruction* append_name_instruction(LoweringContext& ctx, const std::string& name,
                                     std::optional<SourceLocation> location) {
    Instruction* instruction =
        current_function(ctx).create_instruction<NameInstruction>(name, std::move(location));
    append_instruction(ctx, instruction);
    return instruction;
}

Instruction* lower_number(const std::shared_ptr<numval>& number_node, LoweringContext& ctx) {
    std::string text = number_node->str;
    text.erase(std::remove_if(text.begin(), text.end(),
                              [](unsigned char ch) { return std::isspace(ch) != 0; }),
               text.end());
    if (text.empty()) {
        throw std::runtime_error("IR lower 遇到了空的数字字面量。");
    }

    if (text.find('i') != std::string::npos || text.find('j') != std::string::npos ||
        text.find('I') != std::string::npos || text.find('J') != std::string::npos) {
        throw std::runtime_error("IR lower 暂不支持复数字面量。");
    }

    Instruction* instruction = current_function(ctx).create_instruction<NumberInstruction>(
        std::stod(text), source_location_from(number_node));
    append_instruction(ctx, instruction);
    return instruction;
}

Instruction* lower_call(const std::shared_ptr<multipleFuncCall>& call_node, LoweringContext& ctx) {
    std::vector<Instruction*> out_args;
    for (const std::string& name : collect_name_list(call_node->out_args())) {
        Instruction* out_arg =
            current_function(ctx).create_instruction<NameInstruction>(
                name, source_location_from(call_node->out_args()));
        append_instruction(ctx, out_arg);
        out_args.push_back(out_arg);
    }

    std::vector<Instruction*> in_args;
    if (call_node->in_args()) {
        if (call_node->in_args()->nodetype == node_list || call_node->in_args()->nodetype == node_horz_list) {
            for (const ast_ptr& branch : call_node->in_args()->branch) {
                in_args.push_back(lower_expr(branch, ctx));
            }
        } else {
            in_args.push_back(lower_expr(call_node->in_args(), ctx));
        }
    }

    Instruction* instruction = current_function(ctx).create_instruction<CallInstruction>(
        call_node->name(), std::move(out_args), std::move(in_args),
        source_location_from(call_node));
    append_instruction(ctx, instruction);
    return instruction;
}

Instruction* lower_builtin_call(const std::string& name, std::vector<Instruction*> out_args,
                                std::vector<Instruction*> in_args,
                                std::optional<SourceLocation> location, LoweringContext& ctx) {
    Instruction* instruction = current_function(ctx).create_instruction<CallInstruction>(
        name, std::move(out_args), std::move(in_args), std::move(location));
    append_instruction(ctx, instruction);
    return instruction;
}

Instruction* lower_expr(const ast_ptr& node, LoweringContext& ctx) {
    if (!node) {
        throw std::runtime_error("IR lower 不能处理空表达式节点。");
    }

    switch (node->nodetype) {
        case node_name: {
            const auto sym = std::static_pointer_cast<symref>(node);
            Instruction* instruction = current_function(ctx).create_instruction<NameInstruction>(
                sym->name(), source_location_from(node));
            append_instruction(ctx, instruction);
            return instruction;
        }
        case node_number:
            return lower_number(std::static_pointer_cast<numval>(node), ctx);
        case node_text:
        case node_char_mat: {
            const auto text = std::static_pointer_cast<textNode>(node);
            Instruction* instruction = current_function(ctx).create_instruction<TextInstruction>(
                text->str, source_location_from(node));
            append_instruction(ctx, instruction);
            return instruction;
        }
        case node_negative: {
            Instruction* operand = lower_expr(node->branch[0], ctx);
            Instruction* instruction = current_function(ctx).create_instruction<UnaryOpInstruction>(
                UnaryOpInstruction::UMinus, operand, source_location_from(node));
            append_instruction(ctx, instruction);
            return instruction;
        }
        case node_add:
        case node_subtract:
        case node_multiply:
        case node_power:
        case node_greater_than:
        case node_less_than:
        case node_noteq:
        case node_logic_or: {
            BinOpInstruction::Type op = BinOpInstruction::Add;
            if (node->nodetype == node_subtract) {
                op = BinOpInstruction::Subtract;
            } else if (node->nodetype == node_multiply) {
                op = BinOpInstruction::Multiply;
            } else if (node->nodetype == node_power) {
                op = BinOpInstruction::MPower;
            } else if (node->nodetype == node_greater_than) {
                op = BinOpInstruction::Gt;
            } else if (node->nodetype == node_less_than) {
                op = BinOpInstruction::Lt;
            } else if (node->nodetype == node_noteq) {
                op = BinOpInstruction::Ne;
            } else if (node->nodetype == node_logic_or) {
                op = BinOpInstruction::Or;
            }

            Instruction* lhs = lower_expr(node->branch[0], ctx);
            Instruction* rhs = lower_expr(node->branch[1], ctx);
            Instruction* instruction = current_function(ctx).create_instruction<BinOpInstruction>(
                op, lhs, rhs, source_location_from(node));
            append_instruction(ctx, instruction);
            return instruction;
        }
        case node_multiple_func:
            return lower_call(std::static_pointer_cast<multipleFuncCall>(node), ctx);
        case node_colon: {
            if (node->branch.size() != 2 && node->branch.size() != 3) {
                throw std::runtime_error("IR lower 暂不支持该冒号表达式。");
            }
            std::vector<Instruction*> in_args;
            in_args.reserve(node->branch.size());
            for (const ast_ptr& branch : node->branch) {
                in_args.push_back(lower_expr(branch, ctx));
            }
            return lower_builtin_call("colon", {}, std::move(in_args), source_location_from(node), ctx);
        }
        default:
            break;
    }

    throw std::runtime_error("IR lower 暂不支持该表达式节点。");
}

void ensure_fallthrough_to(LoweringContext& ctx, BasicBlock* target, const ast_ptr& node) {
    if (ctx.current_block != nullptr && ctx.current_block->terminal() == nullptr) {
        ctx.current_block->add_successor(target);
        ctx.current_block->set_terminal(
            current_function(ctx).create_instruction<JumpInstruction>(target,
                                                                      source_location_from(node)));
    }
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
    Instruction* cond = lower_expr(if_node->cond(), ctx);

    BasicBlock* then_block = create_block(ctx, "if_true");
    BasicBlock* else_block = create_block(ctx, "if_false");
    BasicBlock* exit_block = create_block(ctx, "if_exit");

    ctx.current_block->add_successor(then_block);
    ctx.current_block->add_successor(else_block);
    ctx.current_block->set_terminal(current_function(ctx).create_instruction<CondJumpInstruction>(
        cond, then_block, else_block, source_location_from(if_node)));

    ctx.current_block = then_block;
    lower_stmt(if_node->tl(), ctx);
    ensure_fallthrough_to(ctx, exit_block, if_node->tl());

    ctx.current_block = else_block;
    if (if_node->el() && if_node->el()->nodetype != node_nop) {
        lower_stmt(if_node->el(), ctx);
    }
    ensure_fallthrough_to(ctx, exit_block, if_node->el());

    ctx.current_block = exit_block;
}

void lower_for_stmt(const std::shared_ptr<flow>& for_node, LoweringContext& ctx) {
    if (for_node->var_ref() == nullptr || for_node->var_ref()->nodetype != node_name) {
        throw std::runtime_error("IR lower 目前只支持名字形式的 for 循环变量。");
    }
    if (for_node->cond() == nullptr) {
        throw std::runtime_error("IR lower 不能处理空的 for 迭代表达式。");
    }
    const auto loop_var = std::static_pointer_cast<symref>(for_node->var_ref());
    BasicBlock* preheader_block = create_block(ctx, "for_preheader");
    BasicBlock* header_block = create_block(ctx, "for_header");
    BasicBlock* body_block = create_block(ctx, "for_body");
    BasicBlock* latch_block = create_block(ctx, "for_latch");
    BasicBlock* exit_block = create_block(ctx, "for_exit");

    ensure_fallthrough_to(ctx, preheader_block, for_node);

    const std::string state_name = create_hidden_name(ctx, "foreach_state");
    const std::string max_iter_name = create_hidden_name(ctx, "foreach_max_iter");
    const std::string iter_index_name = create_hidden_name(ctx, "foreach_iter_index");
    const std::string current_value_name = create_hidden_name(ctx, "foreach_value");

    ctx.current_block = preheader_block;
    Instruction* iterable = lower_expr(for_node->cond(), ctx);
    std::vector<Instruction*> init_out_args;
    // foreach_init 的真实返回值是 [state, max_iter]，第二个结果即使当前
    // 解释器阶段还没有直接使用，也需要在 IR 中显式接住，以匹配运行时 ABI。
    init_out_args.push_back(append_name_instruction(ctx, state_name, source_location_from(for_node)));
    init_out_args.push_back(
        append_name_instruction(ctx, max_iter_name, source_location_from(for_node)));
    (void)lower_builtin_call("foreach_init", std::move(init_out_args), {iterable},
                             source_location_from(for_node), ctx);

    Instruction* init_index =
        current_function(ctx).create_instruction<NumberInstruction>(std::int64_t{1},
                                                                    source_location_from(for_node));
    append_instruction(ctx, init_index);
    append_instruction(ctx, current_function(ctx).create_instruction<AssignInstruction>(
                                iter_index_name, init_index, source_location_from(for_node)));
    ensure_fallthrough_to(ctx, header_block, for_node);

    ctx.current_block = header_block;
    // foreach_init 返回的第二个结果是最大迭代次数；循环头只负责用隐藏计数器判断是否越界。
    Instruction* max_iter_value =
        append_name_instruction(ctx, max_iter_name, source_location_from(for_node));
    Instruction* iter_index_value =
        append_name_instruction(ctx, iter_index_name, source_location_from(for_node));
    Instruction* done =
        current_function(ctx).create_instruction<BinOpInstruction>(
            BinOpInstruction::Lt, max_iter_value, iter_index_value,
            source_location_from(for_node));
    append_instruction(ctx, done);
    ctx.current_block->add_successor(exit_block);
    ctx.current_block->add_successor(body_block);
    ctx.current_block->set_terminal(current_function(ctx).create_instruction<CondJumpInstruction>(
        done, exit_block, body_block, source_location_from(for_node)));

    ctx.current_block = body_block;
    // foreach_iterate 的输出才是当前轮次的循环变量值，不应直接把 foreach_init 的状态对象赋给用户变量。
    Instruction* state_value =
        append_name_instruction(ctx, state_name, source_location_from(for_node));
    std::vector<Instruction*> iterate_out_args;
    iterate_out_args.push_back(
        append_name_instruction(ctx, current_value_name, source_location_from(for_node)));
    (void)lower_builtin_call("foreach_iterate", std::move(iterate_out_args), {state_value},
                             source_location_from(for_node), ctx);
    Instruction* current_value =
        append_name_instruction(ctx, current_value_name, source_location_from(for_node));
    append_instruction(ctx, current_function(ctx).create_instruction<AssignInstruction>(
                                loop_var->name(), current_value, source_location_from(for_node)));
    ctx.loop_stack.push_back({exit_block, latch_block});
    lower_stmt(for_node->tl(), ctx);
    ctx.loop_stack.pop_back();
    ensure_fallthrough_to(ctx, latch_block, for_node->tl());

    ctx.current_block = latch_block;
    Instruction* iter_index_for_add =
        append_name_instruction(ctx, iter_index_name, source_location_from(for_node));
    Instruction* one =
        current_function(ctx).create_instruction<NumberInstruction>(std::int64_t{1},
                                                                    source_location_from(for_node));
    append_instruction(ctx, one);
    Instruction* next_index =
        current_function(ctx).create_instruction<BinOpInstruction>(
            BinOpInstruction::Add, iter_index_for_add, one, source_location_from(for_node));
    append_instruction(ctx, next_index);
    append_instruction(ctx, current_function(ctx).create_instruction<AssignInstruction>(
                                iter_index_name, next_index, source_location_from(for_node)));
    ensure_fallthrough_to(ctx, header_block, for_node);

    ctx.current_block = exit_block;
}

void lower_assignment(const std::shared_ptr<symasgn>& assign_node, LoweringContext& ctx) {
    Instruction* value = lower_expr(assign_node->v(), ctx);
    append_instruction(ctx, current_function(ctx).create_instruction<AssignInstruction>(
                                assign_node->name(), value, source_location_from(assign_node)));
}

void lower_stmt(const ast_ptr& node, LoweringContext& ctx) {
    if (!node) {
        return;
    }

    switch (node->nodetype) {
        case node_runlist:
        case node_cmdlist:
            lower_stmt_list(node, ctx);
            return;
        case node_asgn:
            lower_assignment(std::static_pointer_cast<symasgn>(node), ctx);
            return;
        case node_multiple_func:
            (void)lower_call(std::static_pointer_cast<multipleFuncCall>(node), ctx);
            return;
        case node_flow_if:
            lower_if_stmt(std::static_pointer_cast<if_flow>(node), ctx);
            return;
        case node_for:
            lower_for_stmt(std::static_pointer_cast<flow>(node), ctx);
            return;
        case node_break:
            if (ctx.loop_stack.empty()) {
                throw std::runtime_error("break 只能出现在循环内部。");
            }
            ctx.current_block->add_successor(ctx.loop_stack.back().break_target);
            ctx.current_block->set_terminal(current_function(ctx).create_instruction<JumpInstruction>(
                ctx.loop_stack.back().break_target, source_location_from(node)));
            ctx.current_block = nullptr;
            return;
        case node_continue:
            if (ctx.loop_stack.empty()) {
                throw std::runtime_error("continue 只能出现在循环内部。");
            }
            ctx.current_block->add_successor(ctx.loop_stack.back().continue_target);
            ctx.current_block->set_terminal(current_function(ctx).create_instruction<JumpInstruction>(
                ctx.loop_stack.back().continue_target, source_location_from(node)));
            ctx.current_block = nullptr;
            return;
        case node_nop:
        case node_andy_end_of_string:
            return;
        default:
            break;
    }

    throw std::runtime_error("IR lower 暂不支持该语句节点。");
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

void populate_function_signature(Function& function, const pcdata& unit) {
    if (unit.m_in_arg_names != nullptr && !unit.m_in_arg_names->empty()) {
        function.set_input_names(*unit.m_in_arg_names);
    } else if (unit.ast && unit.ast->nodetype == node_mfile_func) {
        // 某些 parsed unit 上 pcdata 的参数名列表没有补全，此时直接回退到
        // mFileFunc AST 本身提取函数签名，避免局部函数丢失入参与出参信息。
        const auto func_ast = std::static_pointer_cast<mFileFunc>(unit.ast);
        function.set_input_names(collect_name_list(func_ast->in_args()));
    }

    if (unit.m_out_arg_names != nullptr && !unit.m_out_arg_names->empty()) {
        function.set_output_names(*unit.m_out_arg_names);
    } else if (unit.ast && unit.ast->nodetype == node_mfile_func) {
        const auto func_ast = std::static_pointer_cast<mFileFunc>(unit.ast);
        function.set_output_names(collect_name_list(func_ast->out_args()));
    }
}

void lower_unit_into_function(const pcdata& unit, Module& module) {
    Function* function = module.create_function(function_name_from_unit(unit),
                                                function_type_from_unit(unit));
    populate_function_signature(*function, unit);
    if (function->type() == Function::Script || function->type() == Function::PrimaryFunction) {
        module.set_entry_function(function);
    }

    BasicBlock* entry = function->create_block("entry");
    function->set_entry_block(entry);

    LoweringContext ctx;
    ctx.current_block = entry;
    ctx.next_block_id = 0;

    ast_ptr body = unit.is_mscript() ? script_body_from_unit(unit) : function_body_from_unit(unit);
    lower_stmt(body, ctx);
    if (ctx.current_block != nullptr && ctx.current_block->terminal() == nullptr) {
        ctx.current_block->set_terminal(
            current_function(ctx).create_instruction<ReturnInstruction>(source_location_from(body)));
    }
}

}  // namespace

Module lower_parsed_units_to_ir(const std::vector<std::shared_ptr<pcdata>>& parsed_units) {
    if (parsed_units.empty() || parsed_units.front() == nullptr) {
        throw std::runtime_error("IR lower 没有可用的解析单元。");
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
        throw std::runtime_error("IR lower 失败：没有成功生成任何函数。");
    }
    return module;
}

}  // namespace baltam
