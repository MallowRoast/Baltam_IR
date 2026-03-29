#include "lowering/lowering.h"

#include <algorithm>
#include <cctype>
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

Function::Type function_type_from_unit(const pcdata& unit, bool is_first) {
    if (unit.is_mscript()) {
        return Function::Script;
    }
    return is_first ? Function::PrimaryFunction : Function::LocalFunction;
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

void append_instruction(LoweringContext& ctx, Instruction* instruction) {
    if (ctx.current_block == nullptr) {
        throw std::runtime_error("IR lower 指令时找不到当前基本块。");
    }
    ctx.current_block->append_instruction(instruction);
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
        case node_add:
        case node_multiply:
        case node_greater_than: {
            BinOpInstruction::Type op = BinOpInstruction::Add;
            if (node->nodetype == node_multiply) {
                op = BinOpInstruction::Multiply;
            } else if (node->nodetype == node_greater_than) {
                op = BinOpInstruction::Gt;
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
    if (unit.m_in_arg_names != nullptr) {
        function.set_input_names(*unit.m_in_arg_names);
    }
    if (unit.m_out_arg_names != nullptr) {
        function.set_output_names(*unit.m_out_arg_names);
    }
}

void lower_unit_into_function(const pcdata& unit, bool is_first, Module& module) {
    Function* function = module.create_function(function_name_from_unit(unit),
                                                function_type_from_unit(unit, is_first));
    populate_function_signature(*function, unit);
    if (is_first) {
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
    Module module(function_name_from_unit(first_unit), first_unit.filename,
                  module_type_from_units(parsed_units));

    bool first_function = true;
    for (const std::shared_ptr<pcdata>& unit : parsed_units) {
        if (unit == nullptr || !unit->ast) {
            continue;
        }
        lower_unit_into_function(*unit, first_function, module);
        first_function = false;
    }

    if (module.entry_function() == nullptr) {
        throw std::runtime_error("IR lower 失败：没有成功生成任何函数。");
    }
    return module;
}

}  // namespace baltam
