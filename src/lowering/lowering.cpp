#include "lowering/lowering.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
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

ValueRef value_ref_from_instruction(Instruction* instruction, std::size_t index = 0);
std::vector<ValueRef> collect_value_refs(const std::vector<Instruction*>& instructions);

}  // namespace

namespace {

// 从 AST 节点提取 IR 源码位置信息；若缺失则返回空。
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

// 为单个 parsed unit 选择 lowering 后的函数名。
std::string function_name_from_unit(const pcdata& unit) {
    if (unit.is_mscript()) {
        return "__script_main__";
    }
    if (!unit.funname.empty()) {
        return unit.funname;
    }
    return "__unnamed_function__";
}

// 返回文件名 stem，作为默认模块名。
std::string module_stem_from_path(const std::string& path) {
    if (path.empty()) {
        return {};
    }
    return std::filesystem::path(path).stem().string();
}

// 选择 lowering 后的模块名，优先使用源文件名。
std::string module_name_from_unit(const pcdata& unit) {
    const std::string stem = module_stem_from_path(unit.filename);
    if (!stem.empty()) {
        return stem;
    }
    return function_name_from_unit(unit);
}

// 将一个 parsed unit 分类为脚本、主函数或局部函数。
Function::Type function_type_from_unit(const pcdata& unit) {
    if (unit.is_mscript()) {
        return Function::Script;
    }
    return function_name_from_unit(unit) == module_stem_from_path(unit.filename)
               ? Function::PrimaryFunction
               : Function::LocalFunction;
}

// 根据待 lower 的 parsed units 推导模块类型。
Module::Type module_type_from_units(const std::vector<std::shared_ptr<pcdata>>& parsed_units) {
    if (!parsed_units.empty() && parsed_units.front() != nullptr && parsed_units.front()->is_mscript()) {
        return Module::M_Script;
    }
    return Module::M_Function;
}

// 从表示标识符列表的 AST 节点中收集名字。
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

// 按发现顺序追加名字，并避免重复。
void append_unique_name(std::vector<std::string>& names, const std::string& name) {
    if (name.empty()) {
        return;
    }
    if (std::find(names.begin(), names.end(), name) == names.end()) {
        names.push_back(name);
    }
}

bool contains_name(const std::vector<std::string>& names, const std::string& name) {
    return std::find(names.begin(), names.end(), name) != names.end();
}

// 收集一个语句子树中所有被赋值的名字。
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

// 收集一个表达式子树中被读取的名字。
void collect_read_names(const ast_ptr& node, std::vector<std::string>& names) {
    if (!node) {
        return;
    }

    switch (node->nodetype) {
        case node_name:
            append_unique_name(names, std::static_pointer_cast<symref>(node)->name());
            return;
        case node_number:
        case node_text:
        case node_char_mat:
        case node_nop:
        case node_andy_end_of_string:
            return;
        case node_asgn:
            collect_read_names(std::static_pointer_cast<symasgn>(node)->v(), names);
            return;
        case node_multiple_func: {
            const auto call = std::static_pointer_cast<multipleFuncCall>(node);
            if (call->s() && call->s()->nodetype == node_name && call->type() == symbol_variable) {
                collect_read_names(call->s(), names);
            }
            collect_read_names(call->in_args(), names);
            return;
        }
        case node_anonymous_func:
            // 匿名函数体在创建时不会立刻执行，这里不把其自由变量计为当前语句读集合。
            return;
        default:
            break;
    }

    for (const ast_ptr& branch : node->branch) {
        collect_read_names(branch, names);
    }
}

// 将尚未在当前路径上被定义的读名字加入 live-in 集合。
void append_live_in_reads(const std::vector<std::string>& read_names,
                          const std::unordered_set<std::string>& definitely_assigned,
                          std::vector<std::string>& live_in_names) {
    for (const std::string& name : read_names) {
        if (definitely_assigned.find(name) == definitely_assigned.end()) {
            append_unique_name(live_in_names, name);
        }
    }
}

// 以顺序语义近似收集“本轮开始时就需要的名字”，用于循环 carried-name 判断。
void collect_live_in_names(const ast_ptr& node, std::vector<std::string>& live_in_names,
                           std::unordered_set<std::string>& definitely_assigned) {
    if (!node) {
        return;
    }

    switch (node->nodetype) {
        case node_runlist:
        case node_cmdlist:
        case node_list:
        case node_horz_list:
            for (const ast_ptr& branch : node->branch) {
                collect_live_in_names(branch, live_in_names, definitely_assigned);
            }
            return;
        case node_asgn: {
            const auto assign = std::static_pointer_cast<symasgn>(node);
            std::vector<std::string> read_names;
            collect_read_names(assign->v(), read_names);
            append_live_in_reads(read_names, definitely_assigned, live_in_names);
            definitely_assigned.insert(assign->name());
            return;
        }
        case node_multiple_func: {
            const auto call = std::static_pointer_cast<multipleFuncCall>(node);
            std::vector<std::string> read_names;
            collect_read_names(call->in_args(), read_names);
            append_live_in_reads(read_names, definitely_assigned, live_in_names);
            for (const std::string& name : collect_name_list(call->out_args())) {
                definitely_assigned.insert(name);
            }
            return;
        }
        case node_flow_if: {
            const auto if_stmt = std::static_pointer_cast<if_flow>(node);
            std::vector<std::string> cond_read_names;
            collect_read_names(if_stmt->cond(), cond_read_names);
            append_live_in_reads(cond_read_names, definitely_assigned, live_in_names);

            std::unordered_set<std::string> then_assigned = definitely_assigned;
            collect_live_in_names(if_stmt->tl(), live_in_names, then_assigned);

            if (if_stmt->el() && if_stmt->el()->nodetype != node_nop) {
                std::unordered_set<std::string> else_assigned = definitely_assigned;
                collect_live_in_names(if_stmt->el(), live_in_names, else_assigned);

                std::unordered_set<std::string> merged_assigned;
                for (const std::string& name : then_assigned) {
                    if (else_assigned.find(name) != else_assigned.end()) {
                        merged_assigned.insert(name);
                    }
                }
                definitely_assigned = std::move(merged_assigned);
            }
            return;
        }
        case node_flow_switch: {
            const auto switch_stmt = std::static_pointer_cast<switch_flow>(node);
            std::vector<std::string> expr_read_names;
            collect_read_names(switch_stmt->expr(), expr_read_names);
            append_live_in_reads(expr_read_names, definitely_assigned, live_in_names);

            bool has_otherwise = false;
            bool merged_initialized = false;
            std::unordered_set<std::string> merged_assigned;
            if (switch_stmt->cases()) {
                for (const ast_ptr& case_node : switch_stmt->cases()->branch) {
                    if (!case_node) {
                        continue;
                    }

                    std::unordered_set<std::string> case_assigned = definitely_assigned;
                    if (case_node->nodetype == node_case && case_node->branch.size() >= 2) {
                        std::vector<std::string> match_read_names;
                        collect_read_names(case_node->branch[0], match_read_names);
                        append_live_in_reads(match_read_names, definitely_assigned, live_in_names);
                        collect_live_in_names(case_node->branch[1], live_in_names, case_assigned);
                    } else if (case_node->nodetype == node_otherwise && !case_node->branch.empty()) {
                        has_otherwise = true;
                        collect_live_in_names(case_node->branch.back(), live_in_names, case_assigned);
                    } else {
                        continue;
                    }

                    if (!merged_initialized) {
                        merged_assigned = case_assigned;
                        merged_initialized = true;
                        continue;
                    }

                    std::unordered_set<std::string> intersection;
                    for (const std::string& name : merged_assigned) {
                        if (case_assigned.find(name) != case_assigned.end()) {
                            intersection.insert(name);
                        }
                    }
                    merged_assigned = std::move(intersection);
                }
            }

            if (has_otherwise && merged_initialized) {
                definitely_assigned = std::move(merged_assigned);
            }
            return;
        }
        case node_for: {
            const auto for_stmt = std::static_pointer_cast<flow>(node);
            std::vector<std::string> iterable_read_names;
            collect_read_names(for_stmt->cond(), iterable_read_names);
            append_live_in_reads(iterable_read_names, definitely_assigned, live_in_names);

            std::unordered_set<std::string> body_assigned = definitely_assigned;
            if (for_stmt->var_ref() && for_stmt->var_ref()->nodetype == node_name) {
                body_assigned.insert(std::static_pointer_cast<symref>(for_stmt->var_ref())->name());
            }
            collect_live_in_names(for_stmt->tl(), live_in_names, body_assigned);
            return;
        }
        case node_flow_while: {
            const auto while_stmt = std::static_pointer_cast<if_flow>(node);
            std::vector<std::string> cond_read_names;
            collect_read_names(while_stmt->cond(), cond_read_names);
            append_live_in_reads(cond_read_names, definitely_assigned, live_in_names);

            std::unordered_set<std::string> body_assigned = definitely_assigned;
            collect_live_in_names(while_stmt->tl(), live_in_names, body_assigned);
            return;
        }
        case node_break:
        case node_continue:
            return;
        default: {
            std::vector<std::string> read_names;
            collect_read_names(node, read_names);
            append_live_in_reads(read_names, definitely_assigned, live_in_names);
            return;
        }
    }
}

bool is_noreturn_call(const std::shared_ptr<multipleFuncCall>& call_node) {
    return call_node != nullptr && call_node->name() == "error" &&
           collect_name_list(call_node->out_args()).empty();
}

bool stmt_guaranteed_noreturn(const ast_ptr& node) {
    if (!node) {
        return false;
    }

    switch (node->nodetype) {
        case node_runlist:
        case node_cmdlist:
        case node_list:
        case node_horz_list:
            for (auto it = node->branch.rbegin(); it != node->branch.rend(); ++it) {
                if (*it != nullptr) {
                    return stmt_guaranteed_noreturn(*it);
                }
            }
            return false;
        case node_multiple_func:
            return is_noreturn_call(std::static_pointer_cast<multipleFuncCall>(node));
        case node_flow_if: {
            const auto if_stmt = std::static_pointer_cast<if_flow>(node);
            return if_stmt->el() != nullptr && if_stmt->el()->nodetype != node_nop &&
                   stmt_guaranteed_noreturn(if_stmt->tl()) &&
                   stmt_guaranteed_noreturn(if_stmt->el());
        }
        case node_flow_switch: {
            const auto switch_stmt = std::static_pointer_cast<switch_flow>(node);
            if (!switch_stmt->cases()) {
                return false;
            }
            bool has_otherwise = false;
            for (const ast_ptr& case_node : switch_stmt->cases()->branch) {
                if (!case_node) {
                    continue;
                }
                if (case_node->nodetype == node_case && case_node->branch.size() >= 2) {
                    if (!stmt_guaranteed_noreturn(case_node->branch[1])) {
                        return false;
                    }
                } else if (case_node->nodetype == node_otherwise && !case_node->branch.empty()) {
                    has_otherwise = true;
                    if (!stmt_guaranteed_noreturn(case_node->branch.back())) {
                        return false;
                    }
                }
            }
            return has_otherwise;
        }
        default:
            return false;
    }
}

ValueRef lower_expr(const ast_ptr& node, LoweringContext& ctx);
void lower_stmt(const ast_ptr& node, LoweringContext& ctx);

// 要求当前路径上某个名字已经绑定到合法 ValueRef。
ValueRef require_symbol_value_ref(const LoweringContext& ctx, const std::string& name) {
    const ValueRef ref = ctx.lookup_symbol_value_ref(name);
    if (!ref.is_valid()) {
        throw std::runtime_error("IR lower 找不到符号 `" + name + "` 的当前 SSA 值。");
    }
    return ref;
}

bool is_internal_debug_name(const std::string& debug_name) {
    return debug_name.rfind("__", 0) == 0;
}

void bind_name_value(LoweringContext& ctx, const std::string& name, ValueRef value_ref,
                     std::optional<SourceLocation> location) {
    (void)location;
    if (name.empty() || !value_ref.is_valid()) {
        return;
    }
    if (const InstValue* value = ctx.function().find_value(value_ref.id);
        value != nullptr &&
        (value->debug_name.empty() || value->debug_name == name ||
         is_internal_debug_name(value->debug_name))) {
        ctx.function().set_value_debug_name(value_ref.id, name);
    }
    ctx.bind_symbol_value(name, value_ref);
}

ValueRef append_undef_value(LoweringContext& ctx, std::optional<SourceLocation> location) {
    Instruction* undef = ctx.function().create_instruction<UndefInstruction>(std::move(location));
    return ctx.append_valued_instruction(undef)->value_ref();
}

void bind_symbol_value_only(LoweringContext& ctx, const std::string& name, ValueRef value_ref) {
    ctx.bind_symbol_value(name, value_ref);
}

std::vector<ValueRef> build_explicit_return_values(const Function& function, LoweringContext& ctx,
                                                   std::optional<SourceLocation> location) {
    (void)location;
    std::vector<ValueRef> values;
    values.reserve(function.output_names().size());
    for (const std::string& name : function.output_names()) {
        values.push_back(require_symbol_value_ref(ctx, name));
    }
    return values;
}

// 捕获后续需要合并的名字在当前路径上的取值。
std::optional<LoweringContext::MergeSnapshot> capture_merge_snapshot(
    LoweringContext& ctx, const std::vector<std::string>& names,
    std::optional<SourceLocation> location) {
    (void)location;
    if (names.empty() || ctx.current_block == nullptr || ctx.current_block->terminal() != nullptr) {
        return std::nullopt;
    }

    LoweringContext::MergeSnapshot snapshot;
    snapshot.predecessor = ctx.current_block;
    snapshot.value_refs.reserve(names.size());
    for (const std::string& name : names) {
        ValueRef current_value = ctx.lookup_symbol_value_ref(name);
        if (!current_value.is_valid()) {
            std::string known_names;
            for (const auto& [known_name, known_ref] : ctx.symbol_table) {
                if (!known_names.empty()) {
                    known_names += ", ";
                }
                known_names += known_name;
                known_names += "=";
                known_names += known_ref.is_valid() ? std::to_string(known_ref.id) : "<invalid>";
            }
            std::string location_text;
            if (location.has_value()) {
                location_text = "，位置: " + location->filename + ":" +
                                std::to_string(location->begin_line);
            }
            throw std::runtime_error("IR lower 在合流点捕获快照时找不到符号 `" + name +
                                     "` 的当前 SSA 值" + location_text + "；当前路径上的符号有: [" +
                                     known_names + "]。");
        }
        snapshot.value_refs.push_back(current_value);
    }
    return snapshot;
}

LoweringContext::MergeSnapshot make_merge_snapshot(
    BasicBlock* predecessor, const LoweringContext::SymbolTable& symbol_table,
    const std::vector<std::string>& names, bool allow_missing = false) {
    LoweringContext::MergeSnapshot snapshot;
    snapshot.predecessor = predecessor;
    snapshot.value_refs.reserve(names.size());
    for (const std::string& name : names) {
        const auto it = symbol_table.find(name);
        if (it == symbol_table.end()) {
            if (!allow_missing) {
                throw std::runtime_error("IR lower 在构造合流快照时找不到符号 `" + name +
                                         "` 的当前 SSA 值。");
            }
            snapshot.value_refs.push_back(ValueRef{});
            continue;
        }
        snapshot.value_refs.push_back(it->second);
    }
    return snapshot;
}

void assign_phi_incoming_debug_names(Function& function, const std::string& name,
                                     const std::vector<PhiInstruction::Incoming>& incomings) {
    if (name.empty()) {
        return;
    }

    std::unordered_map<ValueId, std::string> assigned_names;
    std::size_t next_version = 1;
    for (const PhiInstruction::Incoming& incoming : incomings) {
        if (!incoming.value_ref.is_valid()) {
            continue;
        }

        const InstValue* value = function.find_value(incoming.value_ref.id);
        if (value == nullptr) {
            continue;
        }
        if (!value->debug_name.empty() && value->debug_name != name &&
            !is_internal_debug_name(value->debug_name)) {
            continue;
        }

        const auto [it, inserted] = assigned_names.emplace(
            incoming.value_ref.id, name + "." + std::to_string(next_version));
        if (!inserted) {
            continue;
        }

        function.set_value_debug_name(incoming.value_ref.id, it->second);
        ++next_version;
    }
}

// 插入合并赋值；若有多个前驱到达合流点，则创建 phi 节点。
void append_phi_merge_assignments(LoweringContext& ctx, const std::vector<std::string>& names,
                                  const std::vector<LoweringContext::MergeSnapshot>& snapshots,
                                  std::optional<SourceLocation> location) {
    if (ctx.current_block == nullptr || names.empty()) {
        return;
    }

    std::vector<std::pair<std::string, ValueRef>> merged_values;
    merged_values.reserve(names.size());
    for (std::size_t i = 0; i < names.size(); ++i) {
        std::vector<PhiInstruction::Incoming> incomings;
        incomings.reserve(snapshots.size());
        for (const LoweringContext::MergeSnapshot& snapshot : snapshots) {
            if (snapshot.predecessor == nullptr || i >= snapshot.value_refs.size()) {
                continue;
            }
            incomings.push_back(PhiInstruction::Incoming{snapshot.predecessor,
                                                         snapshot.value_refs[i]});
        }

        if (incomings.empty()) {
            continue;
        }

        if (incomings.size() == 1) {
            merged_values.push_back({names[i], incomings.front().value_ref});
            continue;
        }

        assign_phi_incoming_debug_names(ctx.function(), names[i], incomings);
        // merge phi 是 CFG 合流时合成出来的 SSA 节点，不对应某一条具体源码语句。
        // 若沿用 if/switch/loop 的源码位置，打印 IR 时会错误地把条件行挂到合流块上。
        Instruction* phi =
            ctx.function().create_instruction<PhiInstruction>(std::move(incomings), std::nullopt);
        ctx.append_valued_instruction(phi, names[i]);
        merged_values.push_back({names[i], phi->value_ref()});
    }

    for (const auto& [name, value_ref] : merged_values) {
        bind_symbol_value_only(ctx, name, value_ref);
    }
}

// 在循环头部为 loop-carried 名字插入 phi，并把结果重新绑定回名字表。
std::vector<PhiInstruction*> append_loop_header_phi_bindings(
    LoweringContext& ctx, const std::vector<std::string>& names,
    const std::optional<LoweringContext::MergeSnapshot>& entry_snapshot,
    std::optional<SourceLocation> location) {
    if (ctx.current_block == nullptr || names.empty() || !entry_snapshot.has_value()) {
        return {};
    }

    std::vector<PhiInstruction*> phis;
    phis.reserve(names.size());
    for (std::size_t i = 0; i < names.size(); ++i) {
        std::vector<PhiInstruction::Incoming> incomings;
        if (entry_snapshot->predecessor != nullptr && i < entry_snapshot->value_refs.size()) {
            incomings.push_back(
                PhiInstruction::Incoming{entry_snapshot->predecessor, entry_snapshot->value_refs[i]});
        }

        // loop header phi 同样是 lowering 过程生成的合成节点，不应伪装成某条源码语句。
        Instruction* phi =
            ctx.function().create_instruction<PhiInstruction>(std::move(incomings), std::nullopt);
        ctx.append_valued_instruction(phi, names[i]);
        phis.push_back(static_cast<PhiInstruction*>(phi));
    }

    for (std::size_t i = 0; i < names.size() && i < phis.size(); ++i) {
        bind_symbol_value_only(ctx, names[i], phis[i]->value_ref());
    }
    return phis;
}

// 将循环体回边上的取值补充到循环头 phi 的 incoming 列表里。
void append_loop_backedge_incomings(const std::vector<PhiInstruction*>& phis,
                                    const std::vector<LoweringContext::MergeSnapshot>& snapshots) {
    for (std::size_t i = 0; i < phis.size(); ++i) {
        PhiInstruction* phi = phis[i];
        if (phi == nullptr) {
            continue;
        }

        for (const LoweringContext::MergeSnapshot& snapshot : snapshots) {
            if (snapshot.predecessor == nullptr || i >= snapshot.value_refs.size()) {
                continue;
            }
            phi->append_incoming(PhiInstruction::Incoming{snapshot.predecessor, snapshot.value_refs[i]});
        }
    }
}

// lower 数字字面量，包含 MATLAB 风格的虚数后缀。
ValueRef lower_number(const std::shared_ptr<numval>& number_node, LoweringContext& ctx) {
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

        Instruction* instruction = ctx.function().create_instruction<NumberInstruction>(
            std::complex<double>{0.0, imag_value}, source_location_from(number_node));
        return ctx.append_valued_instruction(instruction)->value_ref();
    }

    Instruction* instruction = ctx.function().create_instruction<NumberInstruction>(
        std::stod(text), source_location_from(number_node));
    return ctx.append_valued_instruction(instruction)->value_ref();
}

// lower 普通函数调用，并为其输出显式生成赋值。
ValueRef lower_call(const std::shared_ptr<multipleFuncCall>& call_node, LoweringContext& ctx) {
    std::vector<std::string> out_names = collect_name_list(call_node->out_args());

    std::vector<ValueRef> in_args;
    if (call_node->in_args()) {
        if (call_node->in_args()->nodetype == node_list || call_node->in_args()->nodetype == node_horz_list) {
            for (const ast_ptr& branch : call_node->in_args()->branch) {
                in_args.push_back(lower_expr(branch, ctx));
            }
        } else {
            in_args.push_back(lower_expr(call_node->in_args(), ctx));
        }
    }

    Instruction* instruction = nullptr;
    const ValueRef callee_ref = ctx.lookup_symbol_value_ref(call_node->name());
    if (call_node->type() == symbol_variable) {
        if (!callee_ref.is_valid()) {
            throw std::runtime_error("IR lower 找不到可调用变量 `" + call_node->name() +
                                     "` 的当前 SSA 值。");
        }
        instruction =
            ctx.create_call_instruction(callee_ref, out_names.size(), std::move(in_args),
                                        source_location_from(call_node));
    } else {
        instruction = ctx.create_call_instruction(call_node->name(), out_names.size(),
                                                  std::move(in_args), source_location_from(call_node));
    }
    Instruction* call = ctx.append_valued_instruction(instruction, out_names);
    for (std::size_t i = 0; i < out_names.size(); ++i) {
        bind_name_value(ctx, out_names[i], call->value_ref(i), source_location_from(call_node->out_args()));
    }
    return call->value_ref();
}

// lower 内置函数调用辅助逻辑，并为命名输出显式生成赋值。
ValueRef lower_builtin_call(const std::string& name, std::vector<std::string> out_names,
                            std::vector<ValueRef> in_args,
                            std::optional<SourceLocation> location, LoweringContext& ctx) {
    const std::optional<SourceLocation> bind_location = location;
    Instruction* instruction =
        ctx.create_call_instruction(name, out_names.size(), std::move(in_args), std::move(location));
    Instruction* call = ctx.append_valued_instruction(instruction, out_names);
    for (std::size_t i = 0; i < out_names.size(); ++i) {
        bind_name_value(ctx, out_names[i], call->value_ref(i), bind_location);
    }
    return call->value_ref();
}

// 将嵌套的列表类 AST 节点拍平成表达式项序列。
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

// 收集元胞数组元素，并拍平成用于 builtin lowering 的列表。
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

// 通过对应 builtin lower 横向或纵向拼接表达式。
ValueRef lower_concat_expr(const ast_ptr& node, const std::string& builtin_name,
                           LoweringContext& ctx) {
    std::vector<ValueRef> in_args;
    in_args.reserve(node->branch.size());
    for (const ast_ptr& branch : node->branch) {
        in_args.push_back(lower_expr(branch, ctx));
    }
    return lower_builtin_call(builtin_name, {}, std::move(in_args), source_location_from(node), ctx);
}

// 构造一个隐藏的局部函数，用来承载匿名函数函数体。
Function* create_anonymous_function(const ast_ptr& node, LoweringContext& ctx) {
    if (node->branch.size() < 2) {
        throw std::runtime_error("IR lower 暂不支持空体匿名函数。");
    }

    Module* module = ctx.function().parent();
    if (module == nullptr) {
        throw std::runtime_error("IR lower 匿名函数时找不到模块。");
    }

    const std::string function_name = ctx.create_hidden_name("anonymous");
    Function* function = module->create_function(function_name, Function::LocalFunction);
    function->set_input_names(collect_name_list(node->branch[0]));
    function->set_output_names({"__anon_result"});

    BasicBlock* entry = function->create_block("entry");
    function->set_entry_block(entry);

    LoweringContext anon_ctx;
    anon_ctx.current_block = entry;
    anon_ctx.next_hidden_id = ctx.next_hidden_id;
    for (std::size_t i = 0; i < function->input_names().size(); ++i) {
        anon_ctx.bind_symbol_value(function->input_names()[i], function->input_ref(i));
    }

    const ValueRef value_ref = lower_expr(node->branch[1], anon_ctx);
    bind_name_value(anon_ctx, "__anon_result", value_ref, source_location_from(node));
    anon_ctx.current_block->set_terminal(anon_ctx.create_return_instruction(
        build_explicit_return_values(*function, anon_ctx, source_location_from(node)),
        source_location_from(node)));

    ctx.next_hidden_id = anon_ctx.next_hidden_id;
    return function;
}

// 将匿名函数 lower 为函数句柄构造调用。
ValueRef lower_anonymous_function(const ast_ptr& node, LoweringContext& ctx) {
    Function* function = create_anonymous_function(node, ctx);
    Instruction* function_name = ctx.function().create_instruction<TextInstruction>(
        function->name(), source_location_from(node));
    const ValueRef function_name_ref = ctx.append_valued_instruction(function_name)->value_ref();
    return lower_builtin_call("__ir_make_function_handle__", {}, {function_name_ref},
                              source_location_from(node), ctx);
}

// 通过运行时元胞构造 builtin 来 lower 元胞表达式。
ValueRef lower_cell_expr(const ast_ptr& node, LoweringContext& ctx) {
    std::vector<ValueRef> in_args;
    for (const ast_ptr& element : collect_cell_elements(node)) {
        in_args.push_back(lower_expr(element, ctx));
    }
    return lower_builtin_call("__ir_make_cell__", {}, std::move(in_args), source_location_from(node),
                              ctx);
}

// 将一个表达式节点 lower 为产值 IR。
ValueRef lower_expr(const ast_ptr& node, LoweringContext& ctx) {
    if (!node) {
        throw std::runtime_error("IR lower 不能处理空表达式节点。");
    }

    switch (node->nodetype) {
        case node_name: {
            const auto sym = std::static_pointer_cast<symref>(node);
            const ValueRef ref = ctx.lookup_symbol_value_ref(sym->name());
            if (ref.is_valid()) {
                return ref;
            }
            if (sym->get_symbol_type() == symbol_variable) {
                throw std::runtime_error("IR lower 找不到符号 `" + sym->name() + "` 的当前 SSA 值。");
            }
            return ctx.append_binding_instruction(sym->name(), source_location_from(node))->value_ref();
        }
        case node_number:
            return lower_number(std::static_pointer_cast<numval>(node), ctx);
        case node_text:
        case node_char_mat: {
            const auto text = std::static_pointer_cast<textNode>(node);
            Instruction* instruction = ctx.function().create_instruction<TextInstruction>(
                text->str, source_location_from(node));
            return ctx.append_valued_instruction(instruction)->value_ref();
        }
        case node_horz_list:
            return lower_concat_expr(node, "horzcat", ctx);
        case node_vert_list:
            return lower_concat_expr(node, "vertcat", ctx);
        case node_cell:
            return lower_cell_expr(node, ctx);
        case node_logic_not: {
            Instruction* instruction = ctx.create_unaryop_instruction(
                UnaryOpInstruction::Logic_Not, lower_expr(node->branch[0], ctx),
                source_location_from(node));
            return ctx.append_valued_instruction(instruction)->value_ref();
        }
        case node_anonymous_func:
            return lower_anonymous_function(node, ctx);
        case node_negative: {
            Instruction* instruction = ctx.create_unaryop_instruction(
                UnaryOpInstruction::UMinus, lower_expr(node->branch[0], ctx),
                source_location_from(node));
            return ctx.append_valued_instruction(instruction)->value_ref();
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

            Instruction* instruction =
                ctx.create_binop_instruction(op, lower_expr(node->branch[0], ctx),
                                             lower_expr(node->branch[1], ctx),
                                             source_location_from(node));
            return ctx.append_valued_instruction(instruction)->value_ref();
        }
        case node_multiple_func:
            return lower_call(std::static_pointer_cast<multipleFuncCall>(node), ctx);
        case node_colon: {
            if (node->branch.size() != 2 && node->branch.size() != 3) {
                throw std::runtime_error("IR lower 暂不支持该冒号表达式。");
            }
            std::vector<ValueRef> in_args;
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

// 若当前基本块尚未终结，则补一条 fallthrough 跳转。
void ensure_fallthrough_to(LoweringContext& ctx, BasicBlock* target, const ast_ptr& node) {
    if (ctx.current_block != nullptr && ctx.current_block->terminal() == nullptr) {
        ctx.current_block->add_successor(target);
        ctx.current_block->set_terminal(
            ctx.function().create_instruction<JumpInstruction>(target, source_location_from(node)));
    }
}

// 依次 lower 顺序语句列表中的每条语句。
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

// lower if/else CFG，并为共享输出插入合并赋值。
void lower_if_stmt(const std::shared_ptr<if_flow>& if_node, LoweringContext& ctx) {
    const ValueRef cond = lower_expr(if_node->cond(), ctx);
    const LoweringContext::SymbolTable entry_symbol_table = ctx.symbol_table;
    std::vector<std::string> assigned_names;
    std::vector<std::string> then_assigned;
    std::vector<std::string> else_assigned;
    collect_assigned_names(if_node->tl(), then_assigned);
    if (if_node->el() && if_node->el()->nodetype != node_nop) {
        collect_assigned_names(if_node->el(), else_assigned);
    }
    assigned_names = then_assigned;
    for (const std::string& name : else_assigned) {
        append_unique_name(assigned_names, name);
    }

    BasicBlock* then_block = ctx.create_block("if_true");
    BasicBlock* else_block = ctx.create_block("if_false");
    BasicBlock* exit_block = ctx.create_block("if_exit");

    ctx.current_block->add_successor(then_block);
    ctx.current_block->add_successor(else_block);
    ctx.current_block->set_terminal(
        ctx.create_cond_jump_instruction(cond, then_block, else_block, source_location_from(if_node)));

    ctx.current_block = then_block;
    ctx.symbol_table = entry_symbol_table;
    lower_stmt(if_node->tl(), ctx);
    BasicBlock* then_end_block = ctx.current_block;
    const LoweringContext::SymbolTable then_symbol_table = ctx.symbol_table;
    ensure_fallthrough_to(ctx, exit_block, if_node->tl());

    ctx.current_block = else_block;
    ctx.symbol_table = entry_symbol_table;
    if (if_node->el() && if_node->el()->nodetype != node_nop) {
        lower_stmt(if_node->el(), ctx);
    }
    BasicBlock* else_end_block = ctx.current_block;
    const LoweringContext::SymbolTable else_symbol_table = ctx.symbol_table;
    ensure_fallthrough_to(ctx, exit_block, if_node->el());

    std::vector<std::string> merge_names;
    ctx.current_block = exit_block;
    ctx.symbol_table = entry_symbol_table;
    const bool then_reaches_exit = then_end_block != nullptr;
    const bool else_reaches_exit = else_end_block != nullptr;
    for (const std::string& name : assigned_names) {
        const bool then_has_name =
            then_reaches_exit && then_symbol_table.find(name) != then_symbol_table.end();
        const bool else_has_name =
            else_reaches_exit && else_symbol_table.find(name) != else_symbol_table.end();
        if ((!then_reaches_exit || then_has_name) && (!else_reaches_exit || else_has_name) &&
            (then_reaches_exit || else_reaches_exit)) {
            merge_names.push_back(name);
        }
    }
    for (const std::string& name : assigned_names) {
        ctx.erase_symbol_value(name);
    }
    std::vector<LoweringContext::MergeSnapshot> snapshots;
    if (then_reaches_exit) {
        snapshots.push_back(make_merge_snapshot(then_end_block, then_symbol_table, merge_names));
    }
    if (else_reaches_exit) {
        snapshots.push_back(make_merge_snapshot(else_end_block, else_symbol_table, merge_names));
    }
    append_phi_merge_assignments(ctx, merge_names, snapshots, source_location_from(if_node));
}

// 通过 foreach 运行时协议来 lower for 循环。
void lower_for_stmt(const std::shared_ptr<flow>& for_node, LoweringContext& ctx) {
    if (for_node->var_ref() == nullptr || for_node->var_ref()->nodetype != node_name) {
        throw std::runtime_error("IR lower 目前只支持名字形式的 for 循环变量。");
    }
    if (for_node->cond() == nullptr) {
        throw std::runtime_error("IR lower 不能处理空的 for 迭代表达式。");
    }
    const auto loop_var = std::static_pointer_cast<symref>(for_node->var_ref());
    const LoweringContext::SymbolTable outer_symbol_table = ctx.symbol_table;
    std::vector<std::string> loop_assigned_names;
    append_unique_name(loop_assigned_names, loop_var->name());
    collect_assigned_names(for_node->tl(), loop_assigned_names);
    std::vector<std::string> loop_exit_names = loop_assigned_names;
    std::vector<std::string> loop_carried_names = loop_assigned_names;
    std::unordered_set<std::string> definitely_assigned = {loop_var->name()};
    std::vector<std::string> loop_live_in_names;
    collect_live_in_names(for_node->tl(), loop_live_in_names, definitely_assigned);
    for (const std::string& name : loop_live_in_names) {
        append_unique_name(loop_carried_names, name);
    }
    BasicBlock* preheader_block = ctx.create_block("for_preheader");
    BasicBlock* header_block = ctx.create_block("for_header");
    BasicBlock* body_block = ctx.create_block("for_body");
    BasicBlock* latch_block = ctx.create_block("for_latch");
    BasicBlock* exit_block = ctx.create_block("for_exit");

    ensure_fallthrough_to(ctx, preheader_block, for_node);

    const std::string state_name = ctx.create_hidden_name("foreach_state");
    const std::string max_iter_name = ctx.create_hidden_name("foreach_max_iter");
    const std::string iter_index_name = ctx.create_hidden_name("foreach_iter_index");
    const std::string current_value_name = ctx.create_hidden_name("foreach_value");

    ctx.current_block = preheader_block;
    const ValueRef iterable = lower_expr(for_node->cond(), ctx);
    std::vector<std::string> init_out_args;
    // foreach_init 的真实返回值是 [state, max_iter]，第二个结果即使当前
    // 解释器阶段还没有直接使用，也需要在 IR 中显式接住，以匹配运行时 ABI。
    init_out_args.push_back(state_name);
    init_out_args.push_back(max_iter_name);
    (void)lower_builtin_call("foreach_init", std::move(init_out_args), {iterable},
                             source_location_from(for_node), ctx);

    Instruction* init_index =
        ctx.function().create_instruction<NumberInstruction>(std::int64_t{1},
                                                             source_location_from(for_node));
    ctx.append_valued_instruction(init_index);
    bind_name_value(ctx, iter_index_name, init_index->value_ref(), source_location_from(for_node));
    LoweringContext::SymbolTable loop_entry_symbol_table = ctx.symbol_table;
    for (const std::string& name : loop_carried_names) {
        if (loop_entry_symbol_table.find(name) == loop_entry_symbol_table.end()) {
            const ValueRef undef_ref = append_undef_value(ctx, source_location_from(for_node));
            loop_entry_symbol_table[name] = undef_ref;
            ctx.bind_symbol_value(name, undef_ref);
        }
    }
    const LoweringContext::MergeSnapshot entry_snapshot =
        make_merge_snapshot(preheader_block, loop_entry_symbol_table, loop_carried_names);
    ensure_fallthrough_to(ctx, header_block, for_node);

    ctx.current_block = header_block;
    ctx.symbol_table = loop_entry_symbol_table;
    std::vector<PhiInstruction::Incoming> iter_index_incomings;
    iter_index_incomings.push_back(
        PhiInstruction::Incoming{preheader_block, require_symbol_value_ref(ctx, iter_index_name)});
    Instruction* iter_index_phi_inst =
        ctx.function().create_instruction<PhiInstruction>(std::move(iter_index_incomings),
                                                          source_location_from(for_node));
    ctx.append_valued_instruction(iter_index_phi_inst, iter_index_name);
    auto* iter_index_phi = static_cast<PhiInstruction*>(iter_index_phi_inst);
    const std::vector<PhiInstruction*> header_phis = append_loop_header_phi_bindings(
        ctx, loop_carried_names, entry_snapshot, source_location_from(for_node));
    bind_name_value(ctx, iter_index_name, iter_index_phi->value_ref(), source_location_from(for_node));

    const LoweringContext::SymbolTable header_symbol_table = ctx.symbol_table;

    // foreach_init 返回的第二个结果是最大迭代次数；循环头只负责用隐藏计数器判断是否越界。
    Instruction* done = ctx.create_binop_instruction(
        BinOpInstruction::Lt, require_symbol_value_ref(ctx, max_iter_name),
        require_symbol_value_ref(ctx, iter_index_name), source_location_from(for_node));
    ctx.append_valued_instruction(done);
    const std::optional<LoweringContext::MergeSnapshot> exit_snapshot =
        capture_merge_snapshot(ctx, loop_exit_names, source_location_from(for_node));
    ctx.current_block->add_successor(exit_block);
    ctx.current_block->add_successor(body_block);
    ctx.current_block->set_terminal(
        ctx.create_cond_jump_instruction(done, exit_block, body_block, source_location_from(for_node)));

    ctx.current_block = body_block;
    ctx.symbol_table = header_symbol_table;
    // foreach_iterate 的输出才是当前轮次的循环变量值，不应直接把 foreach_init 的状态对象赋给用户变量。
    std::vector<std::string> iterate_out_args;
    iterate_out_args.push_back(current_value_name);
    Instruction* iterate_call = ctx.append_valued_instruction(
        ctx.create_call_instruction("foreach_iterate", 1,
                                    {require_symbol_value_ref(ctx, state_name)},
                                    source_location_from(for_node)),
        iterate_out_args);
    bind_name_value(ctx, current_value_name, iterate_call->value_ref(0), source_location_from(for_node));
    bind_name_value(ctx, loop_var->name(), iterate_call->value_ref(0), source_location_from(for_node));
    std::vector<LoweringContext::MergeSnapshot> continue_snapshots;
    std::vector<LoweringContext::MergeSnapshot> break_snapshots;
    ctx.loop_stack.push_back({exit_block,
                              latch_block,
                              loop_carried_names.empty() ? nullptr : &loop_carried_names,
                              loop_carried_names.empty() ? nullptr : &continue_snapshots,
                              loop_exit_names.empty() ? nullptr : &loop_exit_names,
                              loop_exit_names.empty() ? nullptr : &break_snapshots});
    lower_stmt(for_node->tl(), ctx);
    ctx.loop_stack.pop_back();
    std::optional<LoweringContext::MergeSnapshot> backedge_snapshot =
        capture_merge_snapshot(ctx, loop_carried_names, source_location_from(for_node->tl()));
    ensure_fallthrough_to(ctx, latch_block, for_node->tl());

    ctx.current_block = latch_block;
    ctx.symbol_table = header_symbol_table;
    std::vector<LoweringContext::MergeSnapshot> latch_snapshots = continue_snapshots;
    if (backedge_snapshot.has_value()) {
        latch_snapshots.push_back(*backedge_snapshot);
    }
    append_phi_merge_assignments(ctx, loop_carried_names, latch_snapshots, source_location_from(for_node));

    Instruction* one =
        ctx.function().create_instruction<NumberInstruction>(std::int64_t{1},
                                                             source_location_from(for_node));
    ctx.append_valued_instruction(one);
    Instruction* next_index = ctx.create_binop_instruction(
        BinOpInstruction::Add, require_symbol_value_ref(ctx, iter_index_name), one->value_ref(),
        source_location_from(for_node));
    ctx.append_valued_instruction(next_index);
    bind_name_value(ctx, iter_index_name, next_index->value_ref(), source_location_from(for_node));
    iter_index_phi->append_incoming(PhiInstruction::Incoming{latch_block, next_index->value_ref()});
    if (!header_phis.empty()) {
        if (std::optional<LoweringContext::MergeSnapshot> latch_snapshot =
                capture_merge_snapshot(ctx, loop_carried_names, source_location_from(for_node));
            latch_snapshot.has_value()) {
            append_loop_backedge_incomings(header_phis, {*latch_snapshot});
        }
    }
    ensure_fallthrough_to(ctx, header_block, for_node);

    ctx.current_block = exit_block;
    ctx.symbol_table = outer_symbol_table;
    for (const std::string& name : loop_assigned_names) {
        ctx.erase_symbol_value(name);
    }
    std::vector<LoweringContext::MergeSnapshot> exit_snapshots;
    if (exit_snapshot.has_value()) {
        exit_snapshots.push_back(*exit_snapshot);
    }
    exit_snapshots.insert(exit_snapshots.end(), break_snapshots.begin(), break_snapshots.end());
    append_phi_merge_assignments(ctx, loop_exit_names, exit_snapshots,
                                 source_location_from(for_node));
}

// 使用显式的 header、body、exit 基本块来 lower while 循环。
void lower_while_stmt(const std::shared_ptr<if_flow>& while_node, LoweringContext& ctx) {
    if (while_node->cond() == nullptr) {
        throw std::runtime_error("IR lower 不能处理空的 while 条件。");
    }

    const LoweringContext::SymbolTable outer_symbol_table = ctx.symbol_table;
    std::vector<std::string> loop_assigned_names;
    collect_assigned_names(while_node->tl(), loop_assigned_names);
    std::vector<std::string> loop_exit_names = loop_assigned_names;
    LoweringContext::SymbolTable loop_entry_symbol_table = outer_symbol_table;
    for (const std::string& name : loop_exit_names) {
        if (loop_entry_symbol_table.find(name) == loop_entry_symbol_table.end()) {
            const ValueRef undef_ref = append_undef_value(ctx, source_location_from(while_node));
            loop_entry_symbol_table[name] = undef_ref;
            ctx.bind_symbol_value(name, undef_ref);
        }
    }
    const LoweringContext::MergeSnapshot entry_snapshot =
        make_merge_snapshot(ctx.current_block, loop_entry_symbol_table, loop_exit_names);

    BasicBlock* header_block = ctx.create_block("while_header");
    BasicBlock* body_block = ctx.create_block("while_body");
    BasicBlock* exit_block = ctx.create_block("while_exit");

    ensure_fallthrough_to(ctx, header_block, while_node);

    ctx.current_block = header_block;
    ctx.symbol_table = loop_entry_symbol_table;
    const std::vector<PhiInstruction*> header_phis = append_loop_header_phi_bindings(
        ctx, loop_exit_names, entry_snapshot, source_location_from(while_node));
    const ValueRef cond = lower_expr(while_node->cond(), ctx);
    const std::optional<LoweringContext::MergeSnapshot> exit_snapshot =
        capture_merge_snapshot(ctx, loop_exit_names, source_location_from(while_node));
    ctx.current_block->add_successor(body_block);
    ctx.current_block->add_successor(exit_block);
    ctx.current_block->set_terminal(
        ctx.create_cond_jump_instruction(cond, body_block, exit_block, source_location_from(while_node)));

    ctx.current_block = body_block;
    std::vector<LoweringContext::MergeSnapshot> continue_snapshots;
    std::vector<LoweringContext::MergeSnapshot> break_snapshots;
    ctx.loop_stack.push_back({exit_block,
                              header_block,
                              loop_exit_names.empty() ? nullptr : &loop_exit_names,
                              loop_exit_names.empty() ? nullptr : &continue_snapshots,
                              loop_exit_names.empty() ? nullptr : &loop_exit_names,
                              loop_exit_names.empty() ? nullptr : &break_snapshots});
    lower_stmt(while_node->tl(), ctx);
    ctx.loop_stack.pop_back();
    std::optional<LoweringContext::MergeSnapshot> backedge_snapshot =
        capture_merge_snapshot(ctx, loop_exit_names, source_location_from(while_node->tl()));
    ensure_fallthrough_to(ctx, header_block, while_node->tl());
    if (!header_phis.empty()) {
        std::vector<LoweringContext::MergeSnapshot> backedge_snapshots = continue_snapshots;
        if (backedge_snapshot.has_value()) {
            backedge_snapshots.push_back(*backedge_snapshot);
        }
        append_loop_backedge_incomings(header_phis, backedge_snapshots);
    }

    ctx.current_block = exit_block;
    ctx.symbol_table = outer_symbol_table;
    std::vector<LoweringContext::MergeSnapshot> exit_snapshots;
    if (exit_snapshot.has_value()) {
        exit_snapshots.push_back(*exit_snapshot);
    }
    exit_snapshots.insert(exit_snapshots.end(), break_snapshots.begin(), break_snapshots.end());
    append_phi_merge_assignments(ctx, loop_exit_names, exit_snapshots, source_location_from(while_node));
}

// 为一个 switch case 构造匹配条件，包含元胞列表 case。
ValueRef build_switch_match_cond(ValueRef switch_value, const ast_ptr& match_node,
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

    ValueRef combined_cond;
    for (const ast_ptr& item : match_items) {
        const ValueRef rhs = lower_expr(item, ctx);
        const ValueRef eq = lower_builtin_call("__ir_switch_match__", {}, {switch_value, rhs},
                                               source_location_from(item), ctx);

        if (!combined_cond.is_valid()) {
            combined_cond = eq;
            continue;
        }

        Instruction* merged_cond =
            ctx.create_binop_instruction(BinOpInstruction::Or, combined_cond, eq,
                                         source_location_from(item));
        combined_cond = ctx.append_valued_instruction(merged_cond)->value_ref();
    }

    return combined_cond;
}

// 将 switch 语句 lower 为串联的 dispatch 基本块，并处理合流。
void lower_switch_stmt(const std::shared_ptr<switch_flow>& switch_node, LoweringContext& ctx) {
    if (switch_node->expr() == nullptr || switch_node->cases() == nullptr) {
        throw std::runtime_error("IR lower 不能处理空的 switch 语句。");
    }

    const ValueRef switch_value = lower_expr(switch_node->expr(), ctx);

    BasicBlock* exit_block = ctx.create_block("switch_exit");
    BasicBlock* dispatch_block = ctx.current_block;
    const LoweringContext::SymbolTable entry_symbol_table = ctx.symbol_table;
    ast_ptr otherwise_node = nullptr;
    std::vector<std::string> assigned_names;
    std::vector<std::string> merge_names;
    std::vector<std::vector<std::string>> case_assigned_lists;
    std::vector<bool> case_fallthroughs;
    std::vector<std::string> otherwise_assigned;
    std::vector<LoweringContext::MergeSnapshot> merge_snapshots;

    for (const ast_ptr& case_node : switch_node->cases()->branch) {
        if (!case_node) {
            continue;
        }
        if (case_node->nodetype == node_otherwise) {
            otherwise_node = case_node;
            if (!case_node->branch.empty()) {
                collect_assigned_names(case_node->branch.back(), otherwise_assigned);
            }
            for (const std::string& name : otherwise_assigned) {
                append_unique_name(assigned_names, name);
            }
            continue;
        }
        if (case_node->nodetype != node_case || case_node->branch.size() < 2) {
            throw std::runtime_error("IR lower 暂不支持该 switch 分支节点。");
        }

        std::vector<std::string> case_assigned;
        collect_assigned_names(case_node->branch[1], case_assigned);
        case_assigned_lists.push_back(case_assigned);
        case_fallthroughs.push_back(!stmt_guaranteed_noreturn(case_node->branch[1]));
        for (const std::string& name : case_assigned) {
            append_unique_name(assigned_names, name);
        }
    }

    const bool otherwise_fallthrough =
        otherwise_node == nullptr || !stmt_guaranteed_noreturn(otherwise_node->branch.back());
    for (const std::string& name : assigned_names) {
        const bool entry_has_name = entry_symbol_table.find(name) != entry_symbol_table.end();
        bool available_on_all_case_paths = true;
        for (std::size_t i = 0; i < case_assigned_lists.size(); ++i) {
            if (!case_fallthroughs[i]) {
                continue;
            }
            if (!contains_name(case_assigned_lists[i], name) && !entry_has_name) {
                available_on_all_case_paths = false;
                break;
            }
        }
        if (!available_on_all_case_paths) {
            continue;
        }
        if (otherwise_node != nullptr) {
            if (!otherwise_fallthrough || contains_name(otherwise_assigned, name) || entry_has_name) {
                merge_names.push_back(name);
            }
        } else if (entry_has_name) {
            merge_names.push_back(name);
        }
    }

    for (const ast_ptr& case_node : switch_node->cases()->branch) {
        if (!case_node || case_node->nodetype != node_case || case_node->branch.size() < 2) {
            continue;
        }

        ctx.current_block = dispatch_block;
        const ValueRef cond = build_switch_match_cond(switch_value, case_node->branch[0], ctx);
        BasicBlock* body_block = ctx.create_block("switch_case");
        BasicBlock* next_block = ctx.create_block("switch_next");
        ctx.current_block->add_successor(body_block);
        ctx.current_block->add_successor(next_block);
        ctx.current_block->set_terminal(ctx.create_cond_jump_instruction(
            cond, body_block, next_block, source_location_from(case_node)));

        ctx.current_block = body_block;
        ctx.symbol_table = entry_symbol_table;
        lower_stmt(case_node->branch[1], ctx);
        if (std::optional<LoweringContext::MergeSnapshot> snapshot = capture_merge_snapshot(
                ctx, merge_names, source_location_from(case_node->branch[1]));
            snapshot.has_value()) {
            merge_snapshots.push_back(*snapshot);
        }
        ensure_fallthrough_to(ctx, exit_block, case_node->branch[1]);

        dispatch_block = next_block;
    }

    ctx.current_block = dispatch_block;
    ctx.symbol_table = entry_symbol_table;
    if (otherwise_node != nullptr && !otherwise_node->branch.empty()) {
        lower_stmt(otherwise_node->branch.back(), ctx);
    }
    if (std::optional<LoweringContext::MergeSnapshot> snapshot =
            capture_merge_snapshot(ctx, merge_names,
                                   source_location_from(otherwise_node ? otherwise_node : switch_node));
        snapshot.has_value()) {
        merge_snapshots.push_back(*snapshot);
    }
    ensure_fallthrough_to(ctx, exit_block, otherwise_node ? otherwise_node : switch_node);

    ctx.current_block = exit_block;
    ctx.symbol_table = entry_symbol_table;
    for (const std::string& name : assigned_names) {
        ctx.erase_symbol_value(name);
    }
    append_phi_merge_assignments(ctx, merge_names, merge_snapshots, source_location_from(switch_node));
}

// lower 简单变量赋值语句。
void lower_assignment(const std::shared_ptr<symasgn>& assign_node, LoweringContext& ctx) {
    const ValueRef value_ref = lower_expr(assign_node->v(), ctx);
    bind_name_value(ctx, assign_node->name(), value_ref, source_location_from(assign_node));
}

// 将一条语句节点分发到对应的 lowering 逻辑。
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
        case node_multiple_func: {
            const auto call_node = std::static_pointer_cast<multipleFuncCall>(node);
            (void)lower_call(call_node, ctx);
            if (is_noreturn_call(call_node)) {
                ctx.current_block = nullptr;
            }
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
            if (ctx.loop_stack.back().break_snapshots != nullptr &&
                ctx.loop_stack.back().break_names != nullptr) {
                if (std::optional<LoweringContext::MergeSnapshot> snapshot = capture_merge_snapshot(
                        ctx, *ctx.loop_stack.back().break_names, source_location_from(node));
                    snapshot.has_value()) {
                    ctx.loop_stack.back().break_snapshots->push_back(*snapshot);
                }
            }
            ctx.current_block->add_successor(ctx.loop_stack.back().break_target);
            ctx.current_block->set_terminal(ctx.function().create_instruction<JumpInstruction>(
                ctx.loop_stack.back().break_target, source_location_from(node)));
            ctx.current_block = nullptr;
            return;
        case node_continue:
            if (ctx.loop_stack.empty()) {
                throw std::runtime_error("continue 只能出现在循环内部。");
            }
            if (ctx.loop_stack.back().continue_snapshots != nullptr &&
                ctx.loop_stack.back().carried_names != nullptr) {
                if (std::optional<LoweringContext::MergeSnapshot> snapshot = capture_merge_snapshot(
                        ctx, *ctx.loop_stack.back().carried_names, source_location_from(node));
                    snapshot.has_value()) {
                    ctx.loop_stack.back().continue_snapshots->push_back(*snapshot);
                }
            }
            ctx.current_block->add_successor(ctx.loop_stack.back().continue_target);
            ctx.current_block->set_terminal(ctx.function().create_instruction<JumpInstruction>(
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

// 返回脚本单元对应的函数体子树。
ast_ptr script_body_from_unit(const pcdata& unit) {
    return unit.ast;
}

// 返回函数单元对应的可执行函数体子树。
ast_ptr function_body_from_unit(const pcdata& unit) {
    if (unit.ast && unit.ast->nodetype == node_mfile_func) {
        return std::static_pointer_cast<mFileFunc>(unit.ast)->body();
    }
    return unit.ast;
}

// 用 pcdata 填充函数输入输出名字；缺失时回退到函数 AST。
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

// 将一个 parsed unit lower 为挂接到目标模块上的函数。
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
    for (std::size_t i = 0; i < function->input_names().size(); ++i) {
        ctx.bind_symbol_value(function->input_names()[i], function->input_ref(i));
    }

    ast_ptr body = unit.is_mscript() ? script_body_from_unit(unit) : function_body_from_unit(unit);
    lower_stmt(body, ctx);
    if (ctx.current_block != nullptr && ctx.current_block->terminal() == nullptr) {
        // 这里补的是隐式 return：它表示“函数体自然执行结束”这一 IR 语义，
        // 不是源码里显式写出的 return 语句，因此不附着源码位置，避免误挂注释。
        ctx.current_block->set_terminal(
            ctx.create_return_instruction(build_explicit_return_values(*function, ctx, std::nullopt),
                                          std::nullopt));
    }
}

}  // namespace

// 将 parsed units lower 为一个带入口函数的 IR 模块。
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
