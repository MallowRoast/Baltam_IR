#include "optimizer/dce.h"

#include <cstddef>
#include <deque>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace baltam {
namespace optimizer {
namespace {

struct NodeInfo {
    UntypedSSANode* node = nullptr;
    bool removed = false;
    std::vector<ValueId> defs;
    std::vector<ValueRef> uses;

    bool all_defs_unused(const std::unordered_map<ValueId, std::size_t>& use_count) const;
};

void append_use(std::vector<ValueRef>& uses, ValueRef value) {
    if (!value.valid()) {
        return;
    }
    uses.push_back(value);
}

std::vector<ValueId> collect_node_defs(const UntypedSSANode& node) {
    switch (node.type()) {
        case UntypedSSANode::SSA_Number:
            return {static_cast<const SSANumberNode&>(node).result()};
        case UntypedSSANode::SSA_Text:
            return {static_cast<const SSATextNode&>(node).result()};
        case UntypedSSANode::SSA_Undef:
            return {static_cast<const SSAUndefNode&>(node).result()};
        case UntypedSSANode::SSA_Phi:
            return {static_cast<const SSAPhiNode&>(node).result()};
        case UntypedSSANode::SSA_Copy:
            return {static_cast<const SSACopyNode&>(node).result()};
        case UntypedSSANode::SSA_GlobalLoad:
            return {static_cast<const SSAGlobalLoadNode&>(node).result()};
        case UntypedSSANode::SSA_UnaryOp:
            return {static_cast<const SSAUnaryOpNode&>(node).result()};
        case UntypedSSANode::SSA_BinOp:
            return {static_cast<const SSABinOpNode&>(node).result()};
        case UntypedSSANode::SSA_Call:
            return static_cast<const SSACallNode&>(node).results();
        case UntypedSSANode::SSA_GlobalStore:
        case UntypedSSANode::SSA_CondJump:
        case UntypedSSANode::SSA_Jump:
        case UntypedSSANode::SSA_Return:
            return {};
    }

    return {};
}

std::vector<ValueRef> collect_node_uses(const UntypedSSANode& node) {
    std::vector<ValueRef> uses;

    switch (node.type()) {
        case UntypedSSANode::SSA_Number:
        case UntypedSSANode::SSA_Text:
        case UntypedSSANode::SSA_Undef:
        case UntypedSSANode::SSA_GlobalLoad:
        case UntypedSSANode::SSA_Jump:
            return uses;
        case UntypedSSANode::SSA_Phi: {
            const auto& phi = static_cast<const SSAPhiNode&>(node);
            uses.reserve(phi.incomings().size());
            for (const SSAPhiNode::Incoming& incoming : phi.incomings()) {
                append_use(uses, incoming.value);
            }
            return uses;
        }
        case UntypedSSANode::SSA_Copy:
            append_use(uses, static_cast<const SSACopyNode&>(node).src());
            return uses;
        case UntypedSSANode::SSA_GlobalStore:
            append_use(uses, static_cast<const SSAGlobalStoreNode&>(node).value());
            return uses;
        case UntypedSSANode::SSA_UnaryOp:
            append_use(uses, static_cast<const SSAUnaryOpNode&>(node).operand());
            return uses;
        case UntypedSSANode::SSA_BinOp: {
            const auto& binop = static_cast<const SSABinOpNode&>(node);
            uses.reserve(2);
            append_use(uses, binop.lhs());
            append_use(uses, binop.rhs());
            return uses;
        }
        case UntypedSSANode::SSA_Call: {
            const auto& call = static_cast<const SSACallNode&>(node);
            uses.reserve(call.inputs().size() +
                         (call.callee().type == SSACallNode::Callee::Indirect ? 1u : 0u));
            if (call.callee().type == SSACallNode::Callee::Indirect) {
                append_use(uses, call.callee().indirect_value);
            }
            for (ValueRef input : call.inputs()) {
                append_use(uses, input);
            }
            return uses;
        }
        case UntypedSSANode::SSA_CondJump:
            append_use(uses, static_cast<const SSACondJumpNode&>(node).cond());
            return uses;
        case UntypedSSANode::SSA_Return: {
            const auto& ret = static_cast<const SSAReturnNode&>(node);
            uses.reserve(ret.values().size());
            for (ValueRef value : ret.values()) {
                append_use(uses, value);
            }
            return uses;
        }
    }

    return uses;
}

bool is_removable_node_type(UntypedSSANode::Type type) {
    switch (type) {
        case UntypedSSANode::SSA_Number:
        case UntypedSSANode::SSA_Text:
        case UntypedSSANode::SSA_Undef:
        case UntypedSSANode::SSA_Phi:
        case UntypedSSANode::SSA_Copy:
        case UntypedSSANode::SSA_UnaryOp:
        case UntypedSSANode::SSA_BinOp:
            return true;
        case UntypedSSANode::SSA_GlobalLoad:
        case UntypedSSANode::SSA_GlobalStore:
        case UntypedSSANode::SSA_Call:
        case UntypedSSANode::SSA_CondJump:
        case UntypedSSANode::SSA_Jump:
        case UntypedSSANode::SSA_Return:
            return false;
    }

    return false;
}

bool NodeInfo::all_defs_unused(
    const std::unordered_map<ValueId, std::size_t>& use_count) const {
    if (node == nullptr || removed || !is_removable_node_type(node->type())) {
        return false;
    }

    for (ValueId def : defs) {
        auto it = use_count.find(def);
        if (it != use_count.end() && it->second != 0) {
            return false;
        }
    }
    return true;
}

void collect_block_nodes(
    const std::vector<IRNode*>& nodes,
    std::unordered_map<UntypedSSANode*, NodeInfo>& node_infos,
    std::unordered_map<ValueId, UntypedSSANode*>& def_nodes,
    std::unordered_map<ValueId, std::size_t>& use_count) {
    for (IRNode* node : nodes) {
        auto* ssa = dynamic_cast<UntypedSSANode*>(node);
        if (ssa == nullptr) {
            throw std::runtime_error("DCE 失败：函数正文中含有非 `UntypedSSANode` 节点。");
        }

        NodeInfo info;
        info.node = ssa;
        info.defs = collect_node_defs(*ssa);
        info.uses = collect_node_uses(*ssa);

        for (ValueId def : info.defs) {
            use_count.try_emplace(def, 0);
            auto [it, inserted] = def_nodes.emplace(def, ssa);
            if (!inserted && it->second != ssa) {
                throw std::runtime_error("DCE 失败：发现重复的 SSA 定义。");
            }
        }

        for (ValueRef use : info.uses) {
            ++use_count[use.id];
        }

        node_infos.emplace(ssa, std::move(info));
    }
}

void collect_terminal_uses(IRNode* node, std::unordered_map<ValueId, std::size_t>& use_count) {
    if (node == nullptr) {
        return;
    }

    const auto* ssa = dynamic_cast<const UntypedSSANode*>(node);
    if (ssa == nullptr) {
        throw std::runtime_error("DCE 失败：基本块终结节点不是 `UntypedSSANode`。");
    }

    for (ValueRef use : collect_node_uses(*ssa)) {
        ++use_count[use.id];
    }
}

}  // namespace

const char* UntypedSSADCEPass::name() const {
    return "untyped-ssa-dce";
}

analysis::PreservedAnalyses UntypedSSADCEPass::run(
    Function& function, analysis::FunctionAnalysisManager& analysis_manager) {
    (void)analysis_manager;

    if (function.stage() != IRNode::UntypedSSA) {
        return analysis::PreservedAnalyses::all();
    }

    std::unordered_map<UntypedSSANode*, NodeInfo> node_infos;
    std::unordered_map<ValueId, UntypedSSANode*> def_nodes;
    std::unordered_map<ValueId, std::size_t> use_count;

    for (const auto& block : function.blocks()) {
        if (block == nullptr) {
            continue;
        }

        collect_block_nodes(block->phi_nodes(), node_infos, def_nodes, use_count);
        collect_block_nodes(block->instructions(), node_infos, def_nodes, use_count);
        collect_terminal_uses(block->terminal(), use_count);
    }

    std::deque<UntypedSSANode*> worklist;
    for (auto& [node, info] : node_infos) {
        if (info.all_defs_unused(use_count)) {
            worklist.push_back(node);
        }
    }

    bool changed = false;
    while (!worklist.empty()) {
        UntypedSSANode* node = worklist.front();
        worklist.pop_front();

        auto info_it = node_infos.find(node);
        if (info_it == node_infos.end()) {
            continue;
        }

        NodeInfo& info = info_it->second;
        if (!info.all_defs_unused(use_count)) {
            continue;
        }

        BasicBlock* block = info.node != nullptr ? info.node->parent() : nullptr;
        if (block == nullptr) {
            throw std::runtime_error("DCE 失败：待删除节点缺少所属基本块。");
        }

        bool erased_from_block = false;
        if (info.node->type() == UntypedSSANode::SSA_Phi) {
            erased_from_block = block->erase_phi(info.node);
        } else {
            erased_from_block = block->erase_instruction(info.node);
        }
        if (!erased_from_block) {
            throw std::runtime_error("DCE 失败：从基本块删除节点失败。");
        }

        for (ValueId def : info.defs) {
            if (!function.erase_value(def)) {
                throw std::runtime_error("DCE 失败：删除 SSA 值槽位失败。");
            }
        }
        info.removed = true;
        changed = true;

        for (ValueRef use : info.uses) {
            auto count_it = use_count.find(use.id);
            if (count_it == use_count.end()) {
                throw std::runtime_error("DCE 失败：内部 use-count 状态不一致。");
            }
            if (count_it->second == 0) {
                throw std::runtime_error("DCE 失败：内部 use-count 发生下溢。");
            }

            --count_it->second;
            if (count_it->second != 0) {
                continue;
            }

            auto def_it = def_nodes.find(use.id);
            if (def_it == def_nodes.end()) {
                continue;
            }

            auto predecessor_info = node_infos.find(def_it->second);
            if (predecessor_info == node_infos.end()) {
                continue;
            }
            if (predecessor_info->second.all_defs_unused(use_count)) {
                worklist.push_back(def_it->second);
            }
        }
    }

    return changed ? analysis::PreservedAnalyses::none() : analysis::PreservedAnalyses::all();
}

}  // namespace optimizer
}  // namespace baltam
