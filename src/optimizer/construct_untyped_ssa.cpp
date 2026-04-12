#include "optimizer/construct_untyped_ssa.h"

#include <algorithm>
#include <deque>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "analysis/analysis_manager.h"
#include "analysis/cfg_analysis.h"
#include "analysis/def_use.h"
#include "analysis/dominance_frontier.h"
#include "analysis/dominator_tree.h"
#include "analysis/liveness.h"
#include "analysis/verifier.h"

namespace baltam {
namespace optimizer {
namespace {

using BlockMap = std::unordered_map<const BasicBlock*, BasicBlock*>;

struct PhiPlacement {
    std::string name;
    SSAPhiNode* node = nullptr;
};

struct RenameState {
    Function& src_function;
    Function& dst_function;
    const analysis::DominatorTree::Result& dominator_tree;
    BlockMap block_map;
    std::unordered_map<const BasicBlock*, std::vector<PhiPlacement>> phis_by_block;
    std::unordered_map<std::string, std::vector<ValueId>> value_stacks;
    BasicBlock* current_block = nullptr;
};

const NonSSANode& require_non_ssa_node(const IRNode& node) {
    const auto* non_ssa = dynamic_cast<const NonSSANode*>(&node);
    if (non_ssa == nullptr) {
        throw std::runtime_error("construct_untyped_ssa 收到了非 `NonSSANode` 节点。");
    }
    return *non_ssa;
}

std::optional<SourceLocation> clone_location(const IRNode& node) {
    if (!node.source_location().has_value()) {
        return std::nullopt;
    }
    return *node.source_location();
}

std::vector<std::string> collect_name_list(const std::vector<NamedValue>& values) {
    std::vector<std::string> names;
    names.reserve(values.size());
    for (const NamedValue& value : values) {
        if (!value.name.empty()) {
            names.push_back(value.name);
        }
    }
    return names;
}

void append_unique_block(std::vector<const BasicBlock*>& blocks, const BasicBlock* block) {
    if (block == nullptr) {
        return;
    }
    if (std::find(blocks.begin(), blocks.end(), block) == blocks.end()) {
        blocks.push_back(block);
    }
}

std::vector<std::string> collect_ssa_names(const Function& function,
                                           const analysis::DefUse::Result& def_use) {
    std::unordered_set<std::string> name_set;
    for (const auto& entry : def_use.defs_of_name) {
        if (!entry.first.empty()) {
            name_set.insert(entry.first);
        }
    }
    for (const auto& entry : def_use.uses_of_name) {
        if (!entry.first.empty()) {
            name_set.insert(entry.first);
        }
    }
    for (const NamedValue& input : function.inputs()) {
        if (!input.name.empty()) {
            name_set.insert(input.name);
        }
    }
    for (const NamedValue& output : function.outputs()) {
        if (!output.name.empty()) {
            name_set.insert(output.name);
        }
    }

    std::vector<std::string> names(name_set.begin(), name_set.end());
    std::sort(names.begin(), names.end());
    return names;
}

bool is_live_in_name(const analysis::Liveness::Result& liveness, const BasicBlock* block,
                     const std::string& name) {
    const std::vector<std::string>& live_in = liveness.live_in_of(block);
    return std::binary_search(live_in.begin(), live_in.end(), name);
}

std::unordered_map<std::string, std::vector<const BasicBlock*>> collect_definition_blocks(
    const Function& function, const std::vector<std::string>& names,
    const analysis::DefUse::Result& def_use,
    const analysis::CFGAnalysis::Result& cfg) {
    std::unordered_map<std::string, std::vector<const BasicBlock*>> definition_blocks;
    const BasicBlock* entry = function.entry_block();

    std::unordered_set<std::string> input_names;
    for (const NamedValue& input : function.inputs()) {
        if (!input.name.empty()) {
            input_names.insert(input.name);
        }
    }

    for (const std::string& name : names) {
        std::vector<const BasicBlock*> blocks = def_use.definition_blocks_of(name);
        if (input_names.find(name) != input_names.end() && entry != nullptr && cfg.is_reachable(entry)) {
            append_unique_block(blocks, entry);
        }
        if (!blocks.empty()) {
            definition_blocks.emplace(name, std::move(blocks));
        }
    }

    return definition_blocks;
}

BlockMap clone_function_shell(const Function& src_function, Function& dst_function) {
    BlockMap block_map;
    for (const auto& block : src_function.blocks()) {
        if (block == nullptr) {
            continue;
        }
        block_map.emplace(block.get(), dst_function.create_block(block->name()));
    }

    if (src_function.entry_block() != nullptr) {
        auto it = block_map.find(src_function.entry_block());
        if (it == block_map.end()) {
            throw std::runtime_error("construct_untyped_ssa 失败：入口块映射不存在。");
        }
        dst_function.set_entry_block(it->second);
    }

    return block_map;
}

void initialize_argument_values(const Function& src_function, Function& dst_function,
                                std::unordered_map<std::string, std::vector<ValueId>>& value_stacks) {
    std::vector<ValueId> argument_values;
    argument_values.reserve(src_function.inputs().size());

    for (const NamedValue& input : src_function.inputs()) {
        const ValueId value_id = dst_function.create_value(input.name);
        argument_values.push_back(value_id);
        if (!input.name.empty()) {
            value_stacks[input.name].push_back(value_id);
        }
    }

    dst_function.set_argument_values(std::move(argument_values));
}

void place_phi_nodes(RenameState& state,
                     const std::vector<std::string>& names,
                     const std::unordered_map<std::string, std::vector<const BasicBlock*>>& definition_blocks,
                     const analysis::DominanceFrontier::Result& frontier,
                     const analysis::Liveness::Result& liveness) {
    for (const std::string& name : names) {
        auto def_it = definition_blocks.find(name);
        if (def_it == definition_blocks.end()) {
            continue;
        }

        std::deque<const BasicBlock*> worklist(def_it->second.begin(), def_it->second.end());
        std::unordered_set<const BasicBlock*> expanded(def_it->second.begin(), def_it->second.end());
        std::unordered_set<const BasicBlock*> phi_blocks;

        while (!worklist.empty()) {
            const BasicBlock* block = worklist.front();
            worklist.pop_front();

            for (const BasicBlock* frontier_block : frontier.frontier_of(block)) {
                if (!is_live_in_name(liveness, frontier_block, name)) {
                    continue;
                }
                if (!phi_blocks.insert(frontier_block).second) {
                    continue;
                }

                const ValueId phi_result = state.dst_function.create_value(name);
                auto* phi = state.dst_function.create_node<SSAPhiNode>(phi_result);
                state.block_map.at(frontier_block)->append_phi(phi);
                state.phis_by_block[frontier_block].push_back(PhiPlacement{name, phi});

                if (expanded.insert(frontier_block).second) {
                    worklist.push_back(frontier_block);
                }
            }
        }
    }
}

void push_value(RenameState& state, const std::string& name, ValueId value_id,
                std::vector<std::string>& pushed_names) {
    state.value_stacks[name].push_back(value_id);
    pushed_names.push_back(name);
}

ValueId ensure_current_value(RenameState& state, const std::string& name,
                             std::vector<std::string>& pushed_names,
                             std::optional<SourceLocation> location = std::nullopt) {
    if (name.empty()) {
        throw std::runtime_error("construct_untyped_ssa 遇到了空名字操作数。");
    }

    auto it = state.value_stacks.find(name);
    if (it != state.value_stacks.end() && !it->second.empty()) {
        return it->second.back();
    }

    if (state.current_block == nullptr) {
        throw std::runtime_error("construct_untyped_ssa 当前没有激活的目标基本块。");
    }

    const ValueId undef_value = state.dst_function.create_value(name);
    auto* undef = state.dst_function.create_node<SSAUndefNode>(undef_value, std::move(location));
    state.current_block->append_instruction(undef);
    push_value(state, name, undef_value, pushed_names);
    return undef_value;
}

ValueId define_value(RenameState& state, const std::string& name,
                     std::vector<std::string>& pushed_names) {
    if (name.empty()) {
        throw std::runtime_error("construct_untyped_ssa 遇到了空名字定义。");
    }

    const ValueId value_id = state.dst_function.create_value(name);
    push_value(state, name, value_id, pushed_names);
    return value_id;
}

template <typename T, typename... Args>
T* append_instruction(RenameState& state, Args&&... args) {
    if (state.current_block == nullptr) {
        throw std::runtime_error("construct_untyped_ssa 当前没有激活的目标基本块。");
    }

    T* node = state.dst_function.create_node<T>(std::forward<Args>(args)...);
    state.current_block->append_instruction(node);
    return node;
}

template <typename T, typename... Args>
T* set_terminal(RenameState& state, Args&&... args) {
    if (state.current_block == nullptr) {
        throw std::runtime_error("construct_untyped_ssa 当前没有激活的目标基本块。");
    }

    T* node = state.dst_function.create_node<T>(std::forward<Args>(args)...);
    state.current_block->set_terminal(node);
    return node;
}

void rewrite_non_ssa_instruction(const NonSSANode& node, RenameState& state,
                                 std::vector<std::string>& pushed_names) {
    const std::optional<SourceLocation> location = clone_location(node);

    switch (node.type()) {
        case NonSSANode::Number: {
            const auto& number = static_cast<const NumberNode&>(node);
            const ValueId result = define_value(state, number.result().name, pushed_names);
            append_instruction<SSANumberNode>(state, result, number.value(), location);
            return;
        }
        case NonSSANode::Text: {
            const auto& text = static_cast<const TextNode&>(node);
            const ValueId result = define_value(state, text.result().name, pushed_names);
            append_instruction<SSATextNode>(state, result, text.text(), location);
            return;
        }
        case NonSSANode::Assign: {
            const auto& assign = static_cast<const AssignNode&>(node);
            const ValueId src = ensure_current_value(state, assign.src().name, pushed_names, location);
            const ValueId dst = define_value(state, assign.dst().name, pushed_names);
            append_instruction<SSACopyNode>(state, dst, ValueRef{src}, location);
            return;
        }
        case NonSSANode::GlobalLoad: {
            const auto& load = static_cast<const GlobalLoadNode&>(node);
            const ValueId result = define_value(state, load.result().name, pushed_names);
            append_instruction<SSAGlobalLoadNode>(state, result, load.symbol(), location);
            return;
        }
        case NonSSANode::GlobalStore: {
            const auto& store = static_cast<const GlobalStoreNode&>(node);
            const ValueId value =
                ensure_current_value(state, store.value().name, pushed_names, location);
            append_instruction<SSAGlobalStoreNode>(state, store.symbol(), ValueRef{value}, location);
            return;
        }
        case NonSSANode::UnaryOp: {
            const auto& unary = static_cast<const UnaryOpNode&>(node);
            const ValueId operand =
                ensure_current_value(state, unary.operand().name, pushed_names, location);
            const ValueId result = define_value(state, unary.result().name, pushed_names);
            append_instruction<SSAUnaryOpNode>(state, unary.op(), result, ValueRef{operand}, location);
            return;
        }
        case NonSSANode::BinOp: {
            const auto& binop = static_cast<const BinOpNode&>(node);
            const ValueId lhs = ensure_current_value(state, binop.lhs().name, pushed_names, location);
            const ValueId rhs = ensure_current_value(state, binop.rhs().name, pushed_names, location);
            const ValueId result = define_value(state, binop.result().name, pushed_names);
            append_instruction<SSABinOpNode>(state, binop.op(), result, ValueRef{lhs}, ValueRef{rhs},
                                             location);
            return;
        }
        case NonSSANode::Call: {
            const auto& call = static_cast<const CallNode&>(node);
            SSACallNode::Callee callee;
            if (call.callee_type() == CallNode::Direct) {
                callee.type = SSACallNode::Callee::Direct;
                callee.direct_symbol = call.callee();
            } else {
                callee.type = SSACallNode::Callee::Indirect;
                callee.indirect_value = ValueRef{
                    ensure_current_value(state, call.callee(), pushed_names, location)};
            }

            std::vector<ValueRef> inputs;
            inputs.reserve(call.inputs().size());
            for (const NamedValue& input : call.inputs()) {
                inputs.push_back(ValueRef{
                    ensure_current_value(state, input.name, pushed_names, location)});
            }

            std::vector<ValueId> results;
            results.reserve(call.outputs().size());
            for (const NamedValue& output : call.outputs()) {
                results.push_back(define_value(state, output.name, pushed_names));
            }

            append_instruction<SSACallNode>(state, std::move(callee), std::move(results),
                                            std::move(inputs), location);
            return;
        }
        case NonSSANode::CondJump:
        case NonSSANode::Jump:
        case NonSSANode::Return:
            throw std::runtime_error("construct_untyped_ssa 试图把 non-SSA 终结节点写入正文。");
    }
}

void add_successor_phi_incomings(const BasicBlock& src_block, RenameState& state,
                                 std::vector<std::string>& pushed_names) {
    const std::optional<SourceLocation> location =
        src_block.terminal() != nullptr ? clone_location(*src_block.terminal()) : std::nullopt;

    for (BasicBlock* successor : src_block.successors()) {
        if (successor == nullptr) {
            continue;
        }

        auto phi_it = state.phis_by_block.find(successor);
        if (phi_it == state.phis_by_block.end()) {
            continue;
        }

        for (const PhiPlacement& placement : phi_it->second) {
            const ValueId value =
                ensure_current_value(state, placement.name, pushed_names, location);
            placement.node->add_incoming(state.block_map.at(&src_block), ValueRef{value});
        }
    }
}

void rewrite_terminal(const BasicBlock& src_block, RenameState& state,
                      std::vector<std::string>& pushed_names) {
    if (src_block.terminal() == nullptr) {
        throw std::runtime_error("construct_untyped_ssa 遇到了缺少终结节点的源基本块。");
    }

    const NonSSANode& terminal = require_non_ssa_node(*src_block.terminal());
    const std::optional<SourceLocation> location = clone_location(*src_block.terminal());

    switch (terminal.type()) {
        case NonSSANode::CondJump: {
            const auto& jump = static_cast<const CondJumpNode&>(terminal);
            const ValueId cond = ensure_current_value(state, jump.cond().name, pushed_names, location);
            BasicBlock* true_block = state.block_map.at(jump.true_block());
            BasicBlock* false_block = state.block_map.at(jump.false_block());
            state.current_block->add_successor(true_block);
            state.current_block->add_successor(false_block);
            set_terminal<SSACondJumpNode>(state, ValueRef{cond}, true_block, false_block, location);
            return;
        }
        case NonSSANode::Jump: {
            const auto& jump = static_cast<const JumpNode&>(terminal);
            BasicBlock* target = state.block_map.at(jump.target());
            state.current_block->add_successor(target);
            set_terminal<SSAJumpNode>(state, target, location);
            return;
        }
        case NonSSANode::Return: {
            const auto& ret = static_cast<const ReturnNode&>(terminal);
            std::vector<ValueRef> values;
            values.reserve(ret.values().size());
            for (const NamedValue& value : ret.values()) {
                values.push_back(
                    ValueRef{ensure_current_value(state, value.name, pushed_names, location)});
            }
            set_terminal<SSAReturnNode>(state, std::move(values), location);
            return;
        }
        case NonSSANode::Number:
        case NonSSANode::Text:
        case NonSSANode::Assign:
        case NonSSANode::GlobalLoad:
        case NonSSANode::GlobalStore:
        case NonSSANode::UnaryOp:
        case NonSSANode::BinOp:
        case NonSSANode::Call:
            throw std::runtime_error("construct_untyped_ssa 遇到了非终结型 terminal 节点。");
    }
}

void rename_block(const BasicBlock& src_block, RenameState& state) {
    state.current_block = state.block_map.at(&src_block);

    std::vector<std::string> pushed_names;

    auto phi_it = state.phis_by_block.find(&src_block);
    if (phi_it != state.phis_by_block.end()) {
        for (const PhiPlacement& placement : phi_it->second) {
            push_value(state, placement.name, placement.node->result(), pushed_names);
        }
    }

    for (IRNode* node : src_block.instructions()) {
        if (node == nullptr) {
            continue;
        }
        rewrite_non_ssa_instruction(require_non_ssa_node(*node), state, pushed_names);
    }

    add_successor_phi_incomings(src_block, state, pushed_names);
    rewrite_terminal(src_block, state, pushed_names);

    for (const BasicBlock* child : state.dominator_tree.children_of(&src_block)) {
        if (child != nullptr) {
            rename_block(*child, state);
        }
    }

    for (auto it = pushed_names.rbegin(); it != pushed_names.rend(); ++it) {
        auto stack_it = state.value_stacks.find(*it);
        if (stack_it == state.value_stacks.end() || stack_it->second.empty()) {
            throw std::runtime_error("construct_untyped_ssa 在版本栈回溯时遇到了不一致状态。");
        }
        stack_it->second.pop_back();
    }
}

Function* clone_function_header(const Function& src_function, Module& dst_module) {
    Function* dst_function = dst_module.create_function(src_function.name(), src_function.type());
    dst_function->set_stage(IRNode::UntypedSSA);
    dst_function->set_input_names(collect_name_list(src_function.inputs()));
    dst_function->set_output_names(collect_name_list(src_function.outputs()));
    dst_function->set_has_varargin(src_function.has_varargin());
    dst_function->set_has_varargout(src_function.has_varargout());
    return dst_function;
}

void convert_function_to_untyped_ssa(Function& src_function, Function& dst_function,
                                     analysis::FunctionAnalysisManager& analysis_manager) {
    if (src_function.stage() != IRNode::NonSSA) {
        throw std::runtime_error("construct_untyped_ssa 当前只支持从 `NonSSA` 转换。");
    }

    const analysis::CFGAnalysis::Result& cfg = analysis_manager.get<analysis::CFGAnalysis>(src_function);
    if (cfg.reverse_postorder.size() != src_function.blocks().size()) {
        throw std::runtime_error("construct_untyped_ssa 当前尚不支持含不可达块的函数 `" +
                                 src_function.name() + "`。");
    }

    const analysis::DefUse::Result& def_use = analysis_manager.get<analysis::DefUse>(src_function);
    const analysis::DominatorTree::Result& dominator_tree =
        analysis_manager.get<analysis::DominatorTree>(src_function);
    const analysis::DominanceFrontier::Result& frontier =
        analysis_manager.get<analysis::DominanceFrontier>(src_function);
    const analysis::Liveness::Result& liveness =
        analysis_manager.get<analysis::Liveness>(src_function);

    RenameState state{
        src_function,
        dst_function,
        dominator_tree,
        clone_function_shell(src_function, dst_function),
        {},
        {},
        nullptr,
    };

    initialize_argument_values(src_function, dst_function, state.value_stacks);

    const std::vector<std::string> names = collect_ssa_names(src_function, def_use);
    const auto definition_blocks = collect_definition_blocks(src_function, names, def_use, cfg);
    place_phi_nodes(state, names, definition_blocks, frontier, liveness);

    if (src_function.entry_block() == nullptr) {
        throw std::runtime_error("construct_untyped_ssa 失败：源函数缺少入口块。");
    }
    rename_block(*src_function.entry_block(), state);
}

}  // namespace

Module construct_untyped_ssa_module(Module& non_ssa_module) {
    analysis::FunctionAnalysisManager analysis_manager;

    Module ssa_module(non_ssa_module.name(), non_ssa_module.source_path(), non_ssa_module.type());
    std::unordered_map<const Function*, Function*> function_map;

    for (const auto& function : non_ssa_module.functions()) {
        if (function == nullptr) {
            continue;
        }

        Function* dst_function = clone_function_header(*function, ssa_module);
        function_map.emplace(function.get(), dst_function);
    }

    if (non_ssa_module.entry_function() != nullptr) {
        auto entry_it = function_map.find(non_ssa_module.entry_function());
        if (entry_it == function_map.end()) {
            throw std::runtime_error("construct_untyped_ssa 失败：入口函数映射不存在。");
        }
        ssa_module.set_entry_function(entry_it->second);
    }

    for (const auto& function : non_ssa_module.functions()) {
        if (function == nullptr) {
            continue;
        }
        convert_function_to_untyped_ssa(*function, *function_map.at(function.get()), analysis_manager);
    }

    analysis::verify_module_or_throw(ssa_module);
    return ssa_module;
}

}  // namespace optimizer
}  // namespace baltam
