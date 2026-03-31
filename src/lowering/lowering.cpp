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

// Extracts IR source-location metadata from an AST node when available.
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

// Chooses the lowered function name for one parsed unit.
std::string function_name_from_unit(const pcdata& unit) {
    if (unit.is_mscript()) {
        return "__script_main__";
    }
    if (!unit.funname.empty()) {
        return unit.funname;
    }
    return "__unnamed_function__";
}

// Returns the filename stem used as the default module name.
std::string module_stem_from_path(const std::string& path) {
    if (path.empty()) {
        return {};
    }
    return std::filesystem::path(path).stem().string();
}

// Chooses the lowered module name, preferring the source filename.
std::string module_name_from_unit(const pcdata& unit) {
    const std::string stem = module_stem_from_path(unit.filename);
    if (!stem.empty()) {
        return stem;
    }
    return function_name_from_unit(unit);
}

// Classifies a parsed unit as script, primary function, or local function.
Function::Type function_type_from_unit(const pcdata& unit) {
    if (unit.is_mscript()) {
        return Function::Script;
    }
    return function_name_from_unit(unit) == module_stem_from_path(unit.filename)
               ? Function::PrimaryFunction
               : Function::LocalFunction;
}

// Derives the module kind from the parsed units being lowered.
Module::Type module_type_from_units(const std::vector<std::shared_ptr<pcdata>>& parsed_units) {
    if (!parsed_units.empty() && parsed_units.front() != nullptr && parsed_units.front()->is_mscript()) {
        return Module::M_Script;
    }
    return Module::M_Function;
}

// Collects names from AST nodes that encode identifier lists.
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

// Appends a name once while preserving its discovery order.
void append_unique_name(std::vector<std::string>& names, const std::string& name) {
    if (name.empty()) {
        return;
    }
    if (std::find(names.begin(), names.end(), name) == names.end()) {
        names.push_back(name);
    }
}

// Collects all names assigned within a statement subtree.
void collect_assigned_names(const ast_ptr& node, std::vector<std::string>& names) {
    if (!node) {
        return;
    }

    switch (node->nodetype) {
        case node_runlist:
        case node_cmdlist:
        case node_list:
        case node_horz_list:
            for (const ast_ptr& branch : node->branch) {
                collect_assigned_names(branch, names);
            }
            return;
        case node_asgn:
            append_unique_name(names, std::static_pointer_cast<symasgn>(node)->name());
            return;
        case node_multiple_func: {
            for (const std::string& name :
                 collect_name_list(std::static_pointer_cast<multipleFuncCall>(node)->out_args())) {
                append_unique_name(names, name);
            }
            return;
        }
        case node_flow_if: {
            const auto if_stmt = std::static_pointer_cast<if_flow>(node);
            collect_assigned_names(if_stmt->tl(), names);
            if (if_stmt->el() && if_stmt->el()->nodetype != node_nop) {
                collect_assigned_names(if_stmt->el(), names);
            }
            return;
        }
        case node_flow_switch: {
            const auto switch_stmt = std::static_pointer_cast<switch_flow>(node);
            if (!switch_stmt->cases()) {
                return;
            }
            for (const ast_ptr& case_node : switch_stmt->cases()->branch) {
                if (!case_node) {
                    continue;
                }
                if (case_node->nodetype == node_case && case_node->branch.size() >= 2) {
                    collect_assigned_names(case_node->branch[1], names);
                } else if (case_node->nodetype == node_otherwise && !case_node->branch.empty()) {
                    collect_assigned_names(case_node->branch.back(), names);
                }
            }
            return;
        }
        case node_for: {
            const auto for_stmt = std::static_pointer_cast<flow>(node);
            if (for_stmt->var_ref() && for_stmt->var_ref()->nodetype == node_name) {
                append_unique_name(names, std::static_pointer_cast<symref>(for_stmt->var_ref())->name());
            }
            collect_assigned_names(for_stmt->tl(), names);
            return;
        }
        case node_flow_while:
            collect_assigned_names(std::static_pointer_cast<if_flow>(node)->tl(), names);
            return;
        default:
            return;
    }
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

// Returns the function that owns the block currently being lowered.
Function& current_function(LoweringContext& ctx) {
    if (ctx.current_block == nullptr || ctx.current_block->parent() == nullptr) {
        throw std::runtime_error("IR lower 时找不到当前函数。");
    }
    return *ctx.current_block->parent();
}

// Creates a numbered basic block for the current function.
BasicBlock* create_block(LoweringContext& ctx, const std::string& prefix) {
    std::ostringstream oss;
    oss << prefix << "_" << ctx.next_block_id++;
    return current_function(ctx).create_block(oss.str());
}

Instruction* lower_expr(const ast_ptr& node, LoweringContext& ctx);
void lower_stmt(const ast_ptr& node, LoweringContext& ctx);

// Builds NameInstructions that read the function's declared return bindings.
std::vector<Instruction*> build_explicit_return_values(Function& function,
                                                       std::optional<SourceLocation> location) {
    std::vector<Instruction*> values;
    values.reserve(function.output_names().size());
    for (const std::string& name : function.output_names()) {
        Instruction* value = function.create_instruction<NameInstruction>(name, location);
        function.attach_single_value(*value, name, location);
        values.push_back(value);
    }
    return values;
}

// Allocates a fresh hidden binding name used by lowering temporaries.
std::string create_hidden_name(LoweringContext& ctx, const std::string& prefix) {
    std::ostringstream oss;
    oss << "__" << prefix << "_" << ctx.next_hidden_id++;
    return oss.str();
}

// Converts an instruction result into a value reference, if present.
ValueRef value_ref_from_instruction(Instruction* instruction, std::size_t index = 0) {
    return instruction == nullptr ? ValueRef{} : instruction->value_ref(index);
}

// Collects value references from a list of previously lowered instructions.
std::vector<ValueRef> collect_value_refs(const std::vector<Instruction*>& instructions) {
    std::vector<ValueRef> refs;
    refs.reserve(instructions.size());
    for (Instruction* instruction : instructions) {
        refs.push_back(value_ref_from_instruction(instruction));
    }
    return refs;
}

Instruction* append_valued_instruction(LoweringContext& ctx, Instruction* instruction,
                                       std::string debug_name);
void append_instruction(LoweringContext& ctx, Instruction* instruction);
Instruction* append_name_instruction(LoweringContext& ctx, const std::string& name,
                                     std::optional<SourceLocation> location);
AssignInstruction* create_assign_instruction_from_ref(LoweringContext& ctx, std::string name,
                                                      ValueRef value_ref,
                                                      std::optional<SourceLocation> location);

struct MergeSnapshot {
    BasicBlock* predecessor = nullptr;
    std::vector<ValueRef> value_refs;
};

// Captures the current values for names that must be merged later.
std::optional<MergeSnapshot> capture_merge_snapshot(LoweringContext& ctx,
                                                    const std::vector<std::string>& names,
                                                    std::optional<SourceLocation> location) {
    if (names.empty() || ctx.current_block == nullptr || ctx.current_block->terminal() != nullptr) {
        return std::nullopt;
    }

    MergeSnapshot snapshot;
    snapshot.predecessor = ctx.current_block;
    snapshot.value_refs.reserve(names.size());
    for (const std::string& name : names) {
        Instruction* current_value = append_name_instruction(ctx, name, location);
        snapshot.value_refs.push_back(value_ref_from_instruction(current_value));
    }
    return snapshot;
}

// Inserts merge assignments, creating phi nodes when multiple predecessors reach the merge.
void append_phi_merge_assignments(LoweringContext& ctx, const std::vector<std::string>& names,
                                  const std::vector<MergeSnapshot>& snapshots,
                                  std::optional<SourceLocation> location) {
    if (ctx.current_block == nullptr || names.empty()) {
        return;
    }

    for (std::size_t i = 0; i < names.size(); ++i) {
        std::vector<PhiIncoming> incomings;
        incomings.reserve(snapshots.size());
        for (const MergeSnapshot& snapshot : snapshots) {
            if (snapshot.predecessor == nullptr || i >= snapshot.value_refs.size()) {
                continue;
            }
            incomings.push_back(PhiIncoming{snapshot.predecessor, snapshot.value_refs[i]});
        }

        if (incomings.empty()) {
            continue;
        }

        if (incomings.size() == 1) {
            append_instruction(ctx, create_assign_instruction_from_ref(ctx, names[i],
                                                                       incomings.front().value_ref,
                                                                       location));
            continue;
        }

        Instruction* phi =
            current_function(ctx).create_instruction<PhiInstruction>(std::move(incomings), location);
        append_valued_instruction(ctx, phi, names[i]);
        append_instruction(ctx, create_assign_instruction_from_ref(ctx, names[i], phi->value_ref(),
                                                                   location));
    }
}

// Appends one instruction to the current basic block.
void append_instruction(LoweringContext& ctx, Instruction* instruction) {
    if (ctx.current_block == nullptr) {
        throw std::runtime_error("IR lower 指令时找不到当前基本块。");
    }
    ctx.current_block->append_instruction(instruction);
}

// Attaches one value definition to an instruction and appends it.
Instruction* append_valued_instruction(LoweringContext& ctx, Instruction* instruction,
                                       std::string debug_name = {}) {
    if (instruction == nullptr) {
        return nullptr;
    }
    current_function(ctx).attach_single_value(*instruction, std::move(debug_name),
                                              instruction->source_location());
    append_instruction(ctx, instruction);
    return instruction;
}

// Attaches multiple value definitions to an instruction and appends it.
Instruction* append_multi_valued_instruction(LoweringContext& ctx, Instruction* instruction,
                                             const std::vector<std::string>& debug_names) {
    if (instruction == nullptr) {
        return nullptr;
    }

    std::vector<InstValue> values;
    const std::size_t value_count = debug_names.empty() ? 1 : debug_names.size();
    values.reserve(value_count);
    for (std::size_t i = 0; i < value_count; ++i) {
        values.push_back(current_function(ctx).create_value(
            debug_names.empty() ? std::string{} : debug_names[i], instruction->source_location()));
    }

    current_function(ctx).attach_value_defs(*instruction, std::move(values));
    append_instruction(ctx, instruction);
    return instruction;
}

// Creates a unary operator instruction from one lowered operand.
UnaryOpInstruction* create_unaryop_instruction(LoweringContext& ctx, UnaryOpInstruction::Type op,
                                               Instruction* operand,
                                               std::optional<SourceLocation> location) {
    return current_function(ctx).create_instruction<UnaryOpInstruction>(
        op, value_ref_from_instruction(operand), std::move(location));
}

// Creates a binary operator instruction from two lowered operands.
BinOpInstruction* create_binop_instruction(LoweringContext& ctx, BinOpInstruction::Type op,
                                           Instruction* lhs, Instruction* rhs,
                                           std::optional<SourceLocation> location) {
    return current_function(ctx).create_instruction<BinOpInstruction>(
        op, value_ref_from_instruction(lhs), value_ref_from_instruction(rhs), std::move(location));
}

// Creates an assignment that binds a name to a lowered value instruction.
AssignInstruction* create_assign_instruction(LoweringContext& ctx, std::string name, Instruction* value,
                                             std::optional<SourceLocation> location) {
    return current_function(ctx).create_instruction<AssignInstruction>(
        std::move(name), value_ref_from_instruction(value), std::move(location));
}

// Creates an assignment that binds a name directly to a value reference.
AssignInstruction* create_assign_instruction_from_ref(LoweringContext& ctx, std::string name,
                                                      ValueRef value_ref,
                                                      std::optional<SourceLocation> location) {
    return current_function(ctx).create_instruction<AssignInstruction>(std::move(name), value_ref,
                                                                       std::move(location));
}

// Creates a call instruction with a fixed result arity and lowered inputs.
CallInstruction* create_call_instruction(LoweringContext& ctx, std::string name,
                                         std::size_t output_count,
                                         std::vector<Instruction*> in_args,
                                         std::optional<SourceLocation> location) {
    std::vector<ValueRef> in_arg_refs = collect_value_refs(in_args);
    return current_function(ctx).create_instruction<CallInstruction>(
        std::move(name), output_count, std::move(in_arg_refs), std::move(location));
}

// Creates a conditional branch instruction from a lowered condition value.
CondJumpInstruction* create_cond_jump_instruction(LoweringContext& ctx, Instruction* cond,
                                                  BasicBlock* true_block, BasicBlock* false_block,
                                                  std::optional<SourceLocation> location) {
    return current_function(ctx).create_instruction<CondJumpInstruction>(
        value_ref_from_instruction(cond), true_block, false_block, std::move(location));
}

// Creates a function return instruction from lowered result values.
ReturnInstruction* create_return_instruction(LoweringContext& ctx, std::vector<Instruction*> values,
                                             std::optional<SourceLocation> location) {
    std::vector<ValueRef> value_refs = collect_value_refs(values);
    return current_function(ctx).create_instruction<ReturnInstruction>(std::move(value_refs),
                                                                       std::move(location));
}

// Lowers one variable read and appends it as a value-producing instruction.
Instruction* append_name_instruction(LoweringContext& ctx, const std::string& name,
                                     std::optional<SourceLocation> location) {
    Instruction* instruction =
        current_function(ctx).create_instruction<NameInstruction>(name, std::move(location));
    return append_valued_instruction(ctx, instruction, name);
}

// Lowers a numeric literal, including MATLAB-style imaginary suffixes.
Instruction* lower_number(const std::shared_ptr<numval>& number_node, LoweringContext& ctx) {
    std::string text = number_node->str;
    text.erase(std::remove_if(text.begin(), text.end(),
                              [](unsigned char ch) { return std::isspace(ch) != 0; }),
               text.end());
    if (text.empty()) {
        throw std::runtime_error("IR lower 遇到了空的数字字面量。");
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

        Instruction* instruction = current_function(ctx).create_instruction<NumberInstruction>(
            std::complex<double>{0.0, imag_value}, source_location_from(number_node));
        return append_valued_instruction(ctx, instruction);
    }

    Instruction* instruction = current_function(ctx).create_instruction<NumberInstruction>(
        std::stod(text), source_location_from(number_node));
    return append_valued_instruction(ctx, instruction);
}

// Lowers a general function call and emits explicit assignments for its outputs.
Instruction* lower_call(const std::shared_ptr<multipleFuncCall>& call_node, LoweringContext& ctx) {
    std::vector<std::string> out_names = collect_name_list(call_node->out_args());

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

    Instruction* instruction =
        create_call_instruction(ctx, call_node->name(), out_names.size(), std::move(in_args),
                                source_location_from(call_node));
    Instruction* call = append_multi_valued_instruction(ctx, instruction, out_names);
    for (std::size_t i = 0; i < out_names.size(); ++i) {
        append_instruction(ctx, create_assign_instruction_from_ref(
                                    ctx, out_names[i], call->value_ref(i),
                                    source_location_from(call_node->out_args())));
    }
    return call;
}

// Lowers a builtin call helper and emits explicit assignments for named outputs.
Instruction* lower_builtin_call(const std::string& name, std::vector<std::string> out_names,
                                std::vector<Instruction*> in_args,
                                std::optional<SourceLocation> location, LoweringContext& ctx) {
    const std::optional<SourceLocation> bind_location = location;
    Instruction* instruction = create_call_instruction(ctx, name, out_names.size(),
                                                       std::move(in_args), std::move(location));
    Instruction* call = append_multi_valued_instruction(ctx, instruction, out_names);
    for (std::size_t i = 0; i < out_names.size(); ++i) {
        append_instruction(ctx, create_assign_instruction_from_ref(ctx, out_names[i],
                                                                   call->value_ref(i),
                                                                   bind_location));
    }
    return call;
}

// Flattens nested list-like AST nodes into expression items.
void collect_expr_items(const ast_ptr& node, std::vector<ast_ptr>& items) {
    if (!node) {
        return;
    }

    if (node->nodetype == node_list || node->nodetype == node_horz_list) {
        for (const ast_ptr& branch : node->branch) {
            collect_expr_items(branch, items);
        }
        return;
    }

    items.push_back(node);
}

// Collects cell-array elements into a flat list for builtin lowering.
std::vector<ast_ptr> collect_cell_elements(const ast_ptr& node) {
    std::vector<ast_ptr> items;
    if (!node) {
        return items;
    }

    for (const ast_ptr& branch : node->branch) {
        collect_expr_items(branch, items);
    }
    return items;
}

// Lowers horizontal or vertical concatenation through the corresponding builtin.
Instruction* lower_concat_expr(const ast_ptr& node, const std::string& builtin_name,
                               LoweringContext& ctx) {
    std::vector<Instruction*> in_args;
    in_args.reserve(node->branch.size());
    for (const ast_ptr& branch : node->branch) {
        in_args.push_back(lower_expr(branch, ctx));
    }
    return lower_builtin_call(builtin_name, {}, std::move(in_args), source_location_from(node), ctx);
}

// Builds a hidden local function that implements an anonymous function body.
Function* create_anonymous_function(const ast_ptr& node, LoweringContext& ctx) {
    if (node->branch.size() < 2) {
        throw std::runtime_error("IR lower 暂不支持空体匿名函数。");
    }

    Module* module = current_function(ctx).parent();
    if (module == nullptr) {
        throw std::runtime_error("IR lower 匿名函数时找不到模块。");
    }

    const std::string function_name = create_hidden_name(ctx, "anonymous");
    Function* function = module->create_function(function_name, Function::LocalFunction);
    function->set_input_names(collect_name_list(node->branch[0]));
    function->set_output_names({"__anon_result"});

    BasicBlock* entry = function->create_block("entry");
    function->set_entry_block(entry);

    LoweringContext anon_ctx;
    anon_ctx.current_block = entry;
    anon_ctx.next_hidden_id = ctx.next_hidden_id;

    Instruction* value = lower_expr(node->branch[1], anon_ctx);
    append_instruction(anon_ctx, create_assign_instruction(anon_ctx, "__anon_result", value,
                                                           source_location_from(node)));
    anon_ctx.current_block->set_terminal(
        create_return_instruction(anon_ctx, build_explicit_return_values(*function,
                                                                         source_location_from(node)),
                                  source_location_from(node)));

    ctx.next_hidden_id = anon_ctx.next_hidden_id;
    return function;
}

// Lowers an anonymous function into a function handle construction call.
Instruction* lower_anonymous_function(const ast_ptr& node, LoweringContext& ctx) {
    Function* function = create_anonymous_function(node, ctx);
    Instruction* function_name = current_function(ctx).create_instruction<TextInstruction>(
        function->name(), source_location_from(node));
    append_valued_instruction(ctx, function_name);
    return lower_builtin_call("__ir_make_function_handle__", {}, {function_name},
                              source_location_from(node), ctx);
}

// Lowers a cell expression through the runtime cell-construction builtin.
Instruction* lower_cell_expr(const ast_ptr& node, LoweringContext& ctx) {
    std::vector<Instruction*> in_args;
    for (const ast_ptr& element : collect_cell_elements(node)) {
        in_args.push_back(lower_expr(element, ctx));
    }
    return lower_builtin_call("__ir_make_cell__", {}, std::move(in_args), source_location_from(node),
                              ctx);
}

// Lowers one expression node into value-producing IR.
Instruction* lower_expr(const ast_ptr& node, LoweringContext& ctx) {
    if (!node) {
        throw std::runtime_error("IR lower 不能处理空表达式节点。");
    }

    switch (node->nodetype) {
        case node_name: {
            const auto sym = std::static_pointer_cast<symref>(node);
            Instruction* instruction = current_function(ctx).create_instruction<NameInstruction>(
                sym->name(), source_location_from(node));
            return append_valued_instruction(ctx, instruction, sym->name());
        }
        case node_number:
            return lower_number(std::static_pointer_cast<numval>(node), ctx);
        case node_text:
        case node_char_mat: {
            const auto text = std::static_pointer_cast<textNode>(node);
            Instruction* instruction = current_function(ctx).create_instruction<TextInstruction>(
                text->str, source_location_from(node));
            return append_valued_instruction(ctx, instruction);
        }
        case node_horz_list:
            return lower_concat_expr(node, "horzcat", ctx);
        case node_vert_list:
            return lower_concat_expr(node, "vertcat", ctx);
        case node_cell:
            return lower_cell_expr(node, ctx);
        case node_logic_not: {
            Instruction* operand = lower_expr(node->branch[0], ctx);
            return lower_builtin_call("not", {}, {operand}, source_location_from(node), ctx);
        }
        case node_anonymous_func:
            return lower_anonymous_function(node, ctx);
        case node_negative: {
            Instruction* operand = lower_expr(node->branch[0], ctx);
            Instruction* instruction = create_unaryop_instruction(
                ctx, UnaryOpInstruction::UMinus, operand, source_location_from(node));
            return append_valued_instruction(ctx, instruction);
        }
        case node_add:
        case node_subtract:
        case node_multiply:
        case node_power:
        case node_eq:
        case node_greater_than:
        case node_less_than:
        case node_noteq:
        case node_logic_or: {
            BinOpInstruction::Type op = BinOpInstruction::Add;
            if (node->nodetype == node_subtract) {
                op = BinOpInstruction::Subtract;
            } else if (node->nodetype == node_eq) {
                op = BinOpInstruction::Eq;
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
            Instruction* instruction =
                create_binop_instruction(ctx, op, lhs, rhs, source_location_from(node));
            return append_valued_instruction(ctx, instruction);
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

// Adds a fallthrough jump when the current block does not already terminate.
void ensure_fallthrough_to(LoweringContext& ctx, BasicBlock* target, const ast_ptr& node) {
    if (ctx.current_block != nullptr && ctx.current_block->terminal() == nullptr) {
        ctx.current_block->add_successor(target);
        ctx.current_block->set_terminal(
            current_function(ctx).create_instruction<JumpInstruction>(target,
                                                                      source_location_from(node)));
    }
}

// Lowers each statement in a sequential statement list.
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

// Lowers an if/else CFG and inserts merge assignments for shared outputs.
void lower_if_stmt(const std::shared_ptr<if_flow>& if_node, LoweringContext& ctx) {
    Instruction* cond = lower_expr(if_node->cond(), ctx);
    std::vector<std::string> merge_names;
    if (if_node->el() && if_node->el()->nodetype != node_nop) {
        std::vector<std::string> then_assigned;
        std::vector<std::string> else_assigned;
        collect_assigned_names(if_node->tl(), then_assigned);
        collect_assigned_names(if_node->el(), else_assigned);
        for (const std::string& name : then_assigned) {
            if (std::find(else_assigned.begin(), else_assigned.end(), name) != else_assigned.end()) {
                merge_names.push_back(name);
            }
        }
    }

    BasicBlock* then_block = create_block(ctx, "if_true");
    BasicBlock* else_block = create_block(ctx, "if_false");
    BasicBlock* exit_block = create_block(ctx, "if_exit");

    ctx.current_block->add_successor(then_block);
    ctx.current_block->add_successor(else_block);
    ctx.current_block->set_terminal(
        create_cond_jump_instruction(ctx, cond, then_block, else_block, source_location_from(if_node)));

    ctx.current_block = then_block;
    lower_stmt(if_node->tl(), ctx);
    std::optional<MergeSnapshot> then_snapshot =
        capture_merge_snapshot(ctx, merge_names, source_location_from(if_node->tl()));
    ensure_fallthrough_to(ctx, exit_block, if_node->tl());

    ctx.current_block = else_block;
    if (if_node->el() && if_node->el()->nodetype != node_nop) {
        lower_stmt(if_node->el(), ctx);
    }
    std::optional<MergeSnapshot> else_snapshot =
        capture_merge_snapshot(ctx, merge_names, source_location_from(if_node->el() ? if_node->el()
                                                                                    : if_node));
    ensure_fallthrough_to(ctx, exit_block, if_node->el());

    ctx.current_block = exit_block;
    std::vector<MergeSnapshot> snapshots;
    if (then_snapshot.has_value()) {
        snapshots.push_back(*then_snapshot);
    }
    if (else_snapshot.has_value()) {
        snapshots.push_back(*else_snapshot);
    }
    append_phi_merge_assignments(ctx, merge_names, snapshots, source_location_from(if_node));
}

// Lowers a for-loop through the foreach runtime protocol.
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
    std::vector<std::string> init_out_args;
    // foreach_init 的真实返回值是 [state, max_iter]，第二个结果即使当前
    // 解释器阶段还没有直接使用，也需要在 IR 中显式接住，以匹配运行时 ABI。
    init_out_args.push_back(state_name);
    init_out_args.push_back(max_iter_name);
    (void)lower_builtin_call("foreach_init", std::move(init_out_args), {iterable},
                             source_location_from(for_node), ctx);

    Instruction* init_index =
        current_function(ctx).create_instruction<NumberInstruction>(std::int64_t{1},
                                                                    source_location_from(for_node));
    append_valued_instruction(ctx, init_index);
    append_instruction(ctx, create_assign_instruction(ctx, iter_index_name, init_index,
                                                      source_location_from(for_node)));
    ensure_fallthrough_to(ctx, header_block, for_node);

    ctx.current_block = header_block;
    // foreach_init 返回的第二个结果是最大迭代次数；循环头只负责用隐藏计数器判断是否越界。
    Instruction* max_iter_value =
        append_name_instruction(ctx, max_iter_name, source_location_from(for_node));
    Instruction* iter_index_value =
        append_name_instruction(ctx, iter_index_name, source_location_from(for_node));
    Instruction* done = create_binop_instruction(ctx, BinOpInstruction::Lt, max_iter_value,
                                                 iter_index_value, source_location_from(for_node));
    append_valued_instruction(ctx, done);
    ctx.current_block->add_successor(exit_block);
    ctx.current_block->add_successor(body_block);
    ctx.current_block->set_terminal(
        create_cond_jump_instruction(ctx, done, exit_block, body_block, source_location_from(for_node)));

    ctx.current_block = body_block;
    // foreach_iterate 的输出才是当前轮次的循环变量值，不应直接把 foreach_init 的状态对象赋给用户变量。
    Instruction* state_value =
        append_name_instruction(ctx, state_name, source_location_from(for_node));
    std::vector<std::string> iterate_out_args;
    iterate_out_args.push_back(current_value_name);
    (void)lower_builtin_call("foreach_iterate", std::move(iterate_out_args), {state_value},
                             source_location_from(for_node), ctx);
    Instruction* current_value =
        append_name_instruction(ctx, current_value_name, source_location_from(for_node));
    append_instruction(ctx, create_assign_instruction(ctx, loop_var->name(), current_value,
                                                      source_location_from(for_node)));
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
    append_valued_instruction(ctx, one);
    Instruction* next_index =
        create_binop_instruction(ctx, BinOpInstruction::Add, iter_index_for_add, one,
                                 source_location_from(for_node));
    append_valued_instruction(ctx, next_index);
    append_instruction(ctx, create_assign_instruction(ctx, iter_index_name, next_index,
                                                      source_location_from(for_node)));
    ensure_fallthrough_to(ctx, header_block, for_node);

    ctx.current_block = exit_block;
}

// Lowers a while-loop CFG with explicit header, body, and exit blocks.
void lower_while_stmt(const std::shared_ptr<if_flow>& while_node, LoweringContext& ctx) {
    if (while_node->cond() == nullptr) {
        throw std::runtime_error("IR lower 不能处理空的 while 条件。");
    }

    BasicBlock* header_block = create_block(ctx, "while_header");
    BasicBlock* body_block = create_block(ctx, "while_body");
    BasicBlock* exit_block = create_block(ctx, "while_exit");

    ensure_fallthrough_to(ctx, header_block, while_node);

    ctx.current_block = header_block;
    Instruction* cond = lower_expr(while_node->cond(), ctx);
    ctx.current_block->add_successor(body_block);
    ctx.current_block->add_successor(exit_block);
    ctx.current_block->set_terminal(create_cond_jump_instruction(
        ctx, cond, body_block, exit_block, source_location_from(while_node)));

    ctx.current_block = body_block;
    ctx.loop_stack.push_back({exit_block, header_block});
    lower_stmt(while_node->tl(), ctx);
    ctx.loop_stack.pop_back();
    ensure_fallthrough_to(ctx, header_block, while_node->tl());

    ctx.current_block = exit_block;
}

// Builds the match condition for one switch case, including cell-list cases.
Instruction* build_switch_match_cond(const std::string& switch_value_name, const ast_ptr& match_node,
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

    Instruction* combined_cond = nullptr;
    for (const ast_ptr& item : match_items) {
        Instruction* lhs =
            append_name_instruction(ctx, switch_value_name, source_location_from(match_node));
        Instruction* rhs = lower_expr(item, ctx);
        Instruction* eq = lower_builtin_call("__ir_switch_match__", {}, {lhs, rhs},
                                             source_location_from(item), ctx);

        if (combined_cond == nullptr) {
            combined_cond = eq;
            continue;
        }

        combined_cond = create_binop_instruction(ctx, BinOpInstruction::Or, combined_cond, eq,
                                                 source_location_from(item));
        append_valued_instruction(ctx, combined_cond);
    }

    return combined_cond;
}

// Lowers a switch statement into chained dispatch blocks plus merge handling.
void lower_switch_stmt(const std::shared_ptr<switch_flow>& switch_node, LoweringContext& ctx) {
    if (switch_node->expr() == nullptr || switch_node->cases() == nullptr) {
        throw std::runtime_error("IR lower 不能处理空的 switch 语句。");
    }

    const std::string switch_value_name = create_hidden_name(ctx, "switch_value");
    Instruction* switch_value = lower_expr(switch_node->expr(), ctx);
    append_instruction(ctx, create_assign_instruction(ctx, switch_value_name, switch_value,
                                                      source_location_from(switch_node)));

    BasicBlock* exit_block = create_block(ctx, "switch_exit");
    BasicBlock* dispatch_block = ctx.current_block;
    ast_ptr otherwise_node = nullptr;
    std::vector<std::string> merge_names;
    std::vector<MergeSnapshot> merge_snapshots;
    bool merge_names_initialized = false;

    for (const ast_ptr& case_node : switch_node->cases()->branch) {
        if (!case_node) {
            continue;
        }
        if (case_node->nodetype == node_otherwise) {
            otherwise_node = case_node;
            std::vector<std::string> otherwise_assigned;
            if (!case_node->branch.empty()) {
                collect_assigned_names(case_node->branch.back(), otherwise_assigned);
            }
            if (!merge_names_initialized) {
                merge_names = otherwise_assigned;
                merge_names_initialized = true;
            } else {
                std::vector<std::string> intersection;
                for (const std::string& name : merge_names) {
                    if (std::find(otherwise_assigned.begin(), otherwise_assigned.end(), name) !=
                        otherwise_assigned.end()) {
                        intersection.push_back(name);
                    }
                }
                merge_names = std::move(intersection);
            }
            continue;
        }
        if (case_node->nodetype != node_case || case_node->branch.size() < 2) {
            throw std::runtime_error("IR lower 暂不支持该 switch 分支节点。");
        }

        std::vector<std::string> case_assigned;
        collect_assigned_names(case_node->branch[1], case_assigned);
        if (!merge_names_initialized) {
            merge_names = case_assigned;
            merge_names_initialized = true;
        } else {
            std::vector<std::string> intersection;
            for (const std::string& name : merge_names) {
                if (std::find(case_assigned.begin(), case_assigned.end(), name) !=
                    case_assigned.end()) {
                    intersection.push_back(name);
                }
            }
            merge_names = std::move(intersection);
        }

        ctx.current_block = dispatch_block;
        Instruction* cond = build_switch_match_cond(switch_value_name, case_node->branch[0], ctx);
        BasicBlock* body_block = create_block(ctx, "switch_case");
        BasicBlock* next_block = create_block(ctx, "switch_next");
        ctx.current_block->add_successor(body_block);
        ctx.current_block->add_successor(next_block);
        ctx.current_block->set_terminal(create_cond_jump_instruction(
            ctx, cond, body_block, next_block, source_location_from(case_node)));

        ctx.current_block = body_block;
        lower_stmt(case_node->branch[1], ctx);
        if (std::optional<MergeSnapshot> snapshot = capture_merge_snapshot(
                ctx, merge_names, source_location_from(case_node->branch[1]));
            snapshot.has_value()) {
            merge_snapshots.push_back(*snapshot);
        }
        ensure_fallthrough_to(ctx, exit_block, case_node->branch[1]);

        dispatch_block = next_block;
    }

    if (otherwise_node == nullptr) {
        merge_names.clear();
    }

    ctx.current_block = dispatch_block;
    if (otherwise_node != nullptr && !otherwise_node->branch.empty()) {
        lower_stmt(otherwise_node->branch.back(), ctx);
    }
    if (std::optional<MergeSnapshot> snapshot =
            capture_merge_snapshot(ctx, merge_names,
                                   source_location_from(otherwise_node ? otherwise_node : switch_node));
        snapshot.has_value()) {
        merge_snapshots.push_back(*snapshot);
    }
    ensure_fallthrough_to(ctx, exit_block, otherwise_node ? otherwise_node : switch_node);

    ctx.current_block = exit_block;
    append_phi_merge_assignments(ctx, merge_names, merge_snapshots, source_location_from(switch_node));
}

// Lowers a simple variable assignment statement.
void lower_assignment(const std::shared_ptr<symasgn>& assign_node, LoweringContext& ctx) {
    Instruction* value = lower_expr(assign_node->v(), ctx);
    append_instruction(ctx, create_assign_instruction(ctx, assign_node->name(), value,
                                                      source_location_from(assign_node)));
}

// Dispatches one statement node to the corresponding lowering routine.
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

// Returns the body subtree for script units.
ast_ptr script_body_from_unit(const pcdata& unit) {
    return unit.ast;
}

// Returns the executable body subtree for function units.
ast_ptr function_body_from_unit(const pcdata& unit) {
    if (unit.ast && unit.ast->nodetype == node_mfile_func) {
        return std::static_pointer_cast<mFileFunc>(unit.ast)->body();
    }
    return unit.ast;
}

// Populates function input and output names from pcdata or function AST fallback.
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

// Lowers one parsed unit into a function attached to the target module.
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
            create_return_instruction(ctx, build_explicit_return_values(*function,
                                                                        source_location_from(body)),
                                      source_location_from(body)));
    }
}

}  // namespace

// Lowers parsed units into one IR module with an entry function.
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
