#include "analysis/verifier.h"

#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace baltam {
namespace analysis {
namespace {

const char* node_type_name(NonSSANode::Type type) {
    switch (type) {
        case NonSSANode::Number:
            return "Number";
        case NonSSANode::Text:
            return "Text";
        case NonSSANode::Assign:
            return "Assign";
        case NonSSANode::UnaryOp:
            return "UnaryOp";
        case NonSSANode::BinOp:
            return "BinOp";
        case NonSSANode::Call:
            return "Call";
        case NonSSANode::CondJump:
            return "CondJump";
        case NonSSANode::Jump:
            return "Jump";
        case NonSSANode::Return:
            return "Return";
    }

    return "Unknown";
}

const char* node_type_name(UntypedSSANode::Type type) {
    switch (type) {
        case UntypedSSANode::SSA_Number:
            return "SSANumber";
        case UntypedSSANode::SSA_Text:
            return "SSAText";
        case UntypedSSANode::SSA_Undef:
            return "SSAUndef";
        case UntypedSSANode::SSA_Phi:
            return "SSAPhi";
        case UntypedSSANode::SSA_Copy:
            return "SSACopy";
        case UntypedSSANode::SSA_UnaryOp:
            return "SSAUnaryOp";
        case UntypedSSANode::SSA_BinOp:
            return "SSABinOp";
        case UntypedSSANode::SSA_Call:
            return "SSACall";
        case UntypedSSANode::SSA_CondJump:
            return "SSACondJump";
        case UntypedSSANode::SSA_Jump:
            return "SSAJump";
        case UntypedSSANode::SSA_Return:
            return "SSAReturn";
    }

    return "Unknown";
}

const char* stage_name(IRNode::Stage stage) {
    switch (stage) {
        case IRNode::NonSSA:
            return "NonSSA";
        case IRNode::UntypedSSA:
            return "UntypedSSA";
        case IRNode::TypedSSA:
            return "TypedSSA";
    }

    return "Unknown";
}

bool is_non_ssa_terminator_type(NonSSANode::Type type) {
    return type == NonSSANode::CondJump || type == NonSSANode::Jump || type == NonSSANode::Return;
}

bool is_untyped_ssa_terminator_type(UntypedSSANode::Type type) {
    return type == UntypedSSANode::SSA_CondJump || type == UntypedSSANode::SSA_Jump ||
           type == UntypedSSANode::SSA_Return;
}

std::string node_kind_name(const IRNode& node) {
    const auto* non_ssa = dynamic_cast<const NonSSANode*>(&node);
    if (non_ssa != nullptr) {
        return node_type_name(non_ssa->type());
    }

    const auto* untyped_ssa = dynamic_cast<const UntypedSSANode*>(&node);
    if (untyped_ssa != nullptr) {
        return node_type_name(untyped_ssa->type());
    }

    std::ostringstream oss;
    oss << "IRNode(stage=" << stage_name(node.stage()) << ")";
    return oss.str();
}

std::string block_name(const BasicBlock* block) {
    return block != nullptr ? block->name() : "<null>";
}

void add_error(VerificationResult& result, std::string message) {
    result.add_error(std::move(message));
}

void verify_cfg_edges(VerificationResult& result, const Function& function,
                      const std::unordered_set<const BasicBlock*>& known_blocks) {
    for (const auto& block : function.blocks()) {
        for (BasicBlock* successor : block->successors()) {
            if (successor == nullptr) {
                add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block->name() +
                                      "` 含有空 successor。");
                continue;
            }
            if (known_blocks.find(successor) == known_blocks.end()) {
                add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block->name() +
                                      "` 指向了不属于本函数的 successor `" +
                                      block_name(successor) + "`。");
            }
            if (!BasicBlock::contains_block(successor->predecessors(), block.get())) {
                add_error(result, "函数 `" + function.name() + "` 中 CFG 不一致：块 `" +
                                      block->name() + "` 声明 successor `" +
                                      successor->name() + "`，但对方前驱列表中缺少该块。");
            }
        }

        for (BasicBlock* predecessor : block->predecessors()) {
            if (predecessor == nullptr) {
                add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block->name() +
                                      "` 含有空 predecessor。");
                continue;
            }
            if (known_blocks.find(predecessor) == known_blocks.end()) {
                add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block->name() +
                                      "` 含有不属于本函数的 predecessor `" +
                                      block_name(predecessor) + "`。");
            }
            if (!BasicBlock::contains_block(predecessor->successors(), block.get())) {
                add_error(result, "函数 `" + function.name() + "` 中 CFG 不一致：块 `" +
                                      block->name() + "` 声明 predecessor `" +
                                      predecessor->name() + "`，但对方后继列表中缺少该块。");
            }
        }
    }
}

bool verify_node_basic(VerificationResult& result, const Function& function, const BasicBlock& block,
                       IRNode* node, std::unordered_set<const IRNode*>& seen_nodes,
                       const char* position) {
    if (node == nullptr) {
        add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() + "` 含有空" +
                              std::string(position) + "节点。");
        return false;
    }

    if (node->parent() != &block) {
        add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                              "` 中存在 parent 不一致的" + position + "节点。");
    }

    if (node->stage() != function.stage()) {
        add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                              "` 中 " + position + " 节点 `" + node_kind_name(*node) +
                              "` 的 stage 不一致：期望 `" +
                              stage_name(function.stage()) + "`，实际为 `" +
                              stage_name(node->stage()) + "`。");
    }

    if (!seen_nodes.insert(node).second) {
        add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                              "` 重复引用了同一条" + position + "节点。");
    }

    return true;
}

void verify_non_ssa_phi_section(VerificationResult& result, const Function& function,
                                const BasicBlock& block,
                                std::unordered_set<const IRNode*>& seen_nodes) {
    for (IRNode* node : block.phi_nodes()) {
        if (!verify_node_basic(result, function, block, node, seen_nodes, "phi")) {
            continue;
        }
        add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                              "` 在 `NonSSA` 阶段不允许出现 phi 节点。");
    }
}

void verify_non_ssa_block(VerificationResult& result, const Function& function,
                          const BasicBlock& block,
                          const std::unordered_set<const BasicBlock*>& known_blocks,
                          std::unordered_set<const IRNode*>& seen_nodes) {
    verify_non_ssa_phi_section(result, function, block, seen_nodes);

    for (IRNode* node : block.instructions()) {
        if (!verify_node_basic(result, function, block, node, seen_nodes, "正文")) {
            continue;
        }

        const auto* non_ssa = dynamic_cast<const NonSSANode*>(node);
        if (non_ssa == nullptr) {
            add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                                  "` 在正文中含有非 `NonSSANode` 节点 `" +
                                  node_kind_name(*node) + "`。");
            continue;
        }

        if (is_non_ssa_terminator_type(non_ssa->type())) {
            add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                                  "` 在正文中包含终结节点 `" +
                                  node_type_name(non_ssa->type()) + "`。");
        }
    }

    IRNode* terminal = block.terminal();
    if (terminal == nullptr) {
        add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                              "` 缺少终结节点。");
        return;
    }
    if (!verify_node_basic(result, function, block, terminal, seen_nodes, "终结")) {
        return;
    }

    const auto* terminal_non_ssa = dynamic_cast<const NonSSANode*>(terminal);
    if (terminal_non_ssa == nullptr) {
        add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                              "` 的终结节点不是 `NonSSANode`：`" +
                              node_kind_name(*terminal) + "`。");
        return;
    }
    if (!is_non_ssa_terminator_type(terminal_non_ssa->type())) {
        add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                              "` 的终结节点类型非法：`" +
                              node_type_name(terminal_non_ssa->type()) + "`。");
        return;
    }

    switch (terminal_non_ssa->type()) {
        case NonSSANode::CondJump: {
            const auto* cond_jump = static_cast<const CondJumpNode*>(terminal_non_ssa);
            BasicBlock* true_block = cond_jump->true_block();
            BasicBlock* false_block = cond_jump->false_block();
            if (true_block == nullptr || false_block == nullptr) {
                add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                                      "` 的条件跳转缺少目标块。");
                return;
            }
            if (known_blocks.find(true_block) == known_blocks.end() ||
                known_blocks.find(false_block) == known_blocks.end()) {
                add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                                      "` 的条件跳转目标不属于当前函数。");
            }
            if (!BasicBlock::contains_block(block.successors(), true_block)) {
                add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                                      "` 的 true 目标未出现在 successor 列表中。");
            }
            if (!BasicBlock::contains_block(block.successors(), false_block)) {
                add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                                      "` 的 false 目标未出现在 successor 列表中。");
            }
            return;
        }
        case NonSSANode::Jump: {
            BasicBlock* target = static_cast<const JumpNode*>(terminal_non_ssa)->target();
            if (target == nullptr) {
                add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                                      "` 的跳转缺少目标块。");
                return;
            }
            if (known_blocks.find(target) == known_blocks.end()) {
                add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                                      "` 的跳转目标不属于当前函数。");
            }
            if (!BasicBlock::contains_block(block.successors(), target)) {
                add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                                      "` 的跳转目标未出现在 successor 列表中。");
            }
            return;
        }
        case NonSSANode::Return:
            return;
        case NonSSANode::Number:
        case NonSSANode::Text:
        case NonSSANode::Assign:
        case NonSSANode::UnaryOp:
        case NonSSANode::BinOp:
        case NonSSANode::Call:
            return;
    }
}

void verify_untyped_ssa_phi_section(VerificationResult& result, const Function& function,
                                    const BasicBlock& block,
                                    const std::unordered_set<const BasicBlock*>& known_blocks,
                                    std::unordered_set<const IRNode*>& seen_nodes) {
    for (IRNode* node : block.phi_nodes()) {
        if (!verify_node_basic(result, function, block, node, seen_nodes, "phi")) {
            continue;
        }

        const auto* phi = dynamic_cast<const SSAPhiNode*>(node);
        if (phi == nullptr) {
            add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                                  "` 的 phi 区域中含有非 `SSAPhiNode` 节点 `" +
                                  node_kind_name(*node) + "`。");
            continue;
        }

        std::unordered_set<const BasicBlock*> incoming_predecessors;
        for (const SSAPhiNode::Incoming& incoming : phi->incomings()) {
            if (incoming.predecessor == nullptr) {
                add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                                      "` 的 phi 含有空 predecessor incoming。");
                continue;
            }
            if (known_blocks.find(incoming.predecessor) == known_blocks.end()) {
                add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                                      "` 的 phi incoming 来自当前函数之外的块 `" +
                                      incoming.predecessor->name() + "`。");
                continue;
            }
            if (!BasicBlock::contains_block(block.predecessors(), incoming.predecessor)) {
                add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                                      "` 的 phi incoming 引用了非前驱块 `" +
                                      incoming.predecessor->name() + "`。");
                continue;
            }
            if (!incoming_predecessors.insert(incoming.predecessor).second) {
                add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                                      "` 的 phi 对前驱块 `" +
                                      incoming.predecessor->name() + "` 重复添加 incoming。");
            }
        }

        if (incoming_predecessors.size() != block.predecessors().size()) {
            add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                                  "` 的 phi incoming 集合与前驱列表不一致。");
        }
    }
}

void verify_untyped_ssa_block(VerificationResult& result, const Function& function,
                              const BasicBlock& block,
                              const std::unordered_set<const BasicBlock*>& known_blocks,
                              std::unordered_set<const IRNode*>& seen_nodes) {
    verify_untyped_ssa_phi_section(result, function, block, known_blocks, seen_nodes);

    for (IRNode* node : block.instructions()) {
        if (!verify_node_basic(result, function, block, node, seen_nodes, "正文")) {
            continue;
        }

        const auto* untyped_ssa = dynamic_cast<const UntypedSSANode*>(node);
        if (untyped_ssa == nullptr) {
            add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                                  "` 在正文中含有非 `UntypedSSANode` 节点 `" +
                                  node_kind_name(*node) + "`。");
            continue;
        }

        if (untyped_ssa->type() == UntypedSSANode::SSA_Phi) {
            add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                                  "` 在正文中包含 phi 节点。");
        }
        if (is_untyped_ssa_terminator_type(untyped_ssa->type())) {
            add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                                  "` 在正文中包含终结节点 `" +
                                  node_type_name(untyped_ssa->type()) + "`。");
        }
    }

    IRNode* terminal = block.terminal();
    if (terminal == nullptr) {
        add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                              "` 缺少终结节点。");
        return;
    }
    if (!verify_node_basic(result, function, block, terminal, seen_nodes, "终结")) {
        return;
    }

    const auto* terminal_ssa = dynamic_cast<const UntypedSSANode*>(terminal);
    if (terminal_ssa == nullptr) {
        add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                              "` 的终结节点不是 `UntypedSSANode`：`" +
                              node_kind_name(*terminal) + "`。");
        return;
    }
    if (!is_untyped_ssa_terminator_type(terminal_ssa->type())) {
        add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                              "` 的终结节点类型非法：`" +
                              node_type_name(terminal_ssa->type()) + "`。");
        return;
    }

    switch (terminal_ssa->type()) {
        case UntypedSSANode::SSA_CondJump: {
            const auto* cond_jump = static_cast<const SSACondJumpNode*>(terminal_ssa);
            BasicBlock* true_block = cond_jump->true_block();
            BasicBlock* false_block = cond_jump->false_block();
            if (true_block == nullptr || false_block == nullptr) {
                add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                                      "` 的条件跳转缺少目标块。");
                return;
            }
            if (known_blocks.find(true_block) == known_blocks.end() ||
                known_blocks.find(false_block) == known_blocks.end()) {
                add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                                      "` 的条件跳转目标不属于当前函数。");
            }
            if (!BasicBlock::contains_block(block.successors(), true_block)) {
                add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                                      "` 的 true 目标未出现在 successor 列表中。");
            }
            if (!BasicBlock::contains_block(block.successors(), false_block)) {
                add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                                      "` 的 false 目标未出现在 successor 列表中。");
            }
            return;
        }
        case UntypedSSANode::SSA_Jump: {
            BasicBlock* target = static_cast<const SSAJumpNode*>(terminal_ssa)->target();
            if (target == nullptr) {
                add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                                      "` 的跳转缺少目标块。");
                return;
            }
            if (known_blocks.find(target) == known_blocks.end()) {
                add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                                      "` 的跳转目标不属于当前函数。");
            }
            if (!BasicBlock::contains_block(block.successors(), target)) {
                add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                                      "` 的跳转目标未出现在 successor 列表中。");
            }
            return;
        }
        case UntypedSSANode::SSA_Return:
            return;
        case UntypedSSANode::SSA_Number:
        case UntypedSSANode::SSA_Text:
        case UntypedSSANode::SSA_Undef:
        case UntypedSSANode::SSA_Phi:
        case UntypedSSANode::SSA_Copy:
        case UntypedSSANode::SSA_UnaryOp:
        case UntypedSSANode::SSA_BinOp:
        case UntypedSSANode::SSA_Call:
            return;
    }
}

void verify_unsupported_stage_block(VerificationResult& result, const Function& function,
                                    const BasicBlock& block,
                                    std::unordered_set<const IRNode*>& seen_nodes) {
    for (IRNode* node : block.phi_nodes()) {
        verify_node_basic(result, function, block, node, seen_nodes, "phi");
    }
    for (IRNode* node : block.instructions()) {
        verify_node_basic(result, function, block, node, seen_nodes, "正文");
    }
    if (block.terminal() == nullptr) {
        add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                              "` 缺少终结节点。");
        return;
    }
    verify_node_basic(result, function, block, block.terminal(), seen_nodes, "终结");
}

void verify_ssa_value_use(VerificationResult& result, const Function& function, ValueRef value,
                          const std::string& context,
                          std::unordered_set<ValueId>& used_values) {
    if (!value.valid()) {
        add_error(result, "函数 `" + function.name() + "` 的 " + context + " 引用了无效的 SSA 值。");
        return;
    }

    const SSAValueInfo* info = function.find_value_info(value.id);
    if (info == nullptr) {
        add_error(result, "函数 `" + function.name() + "` 的 " + context + " 引用了未知的 SSA 值 %" +
                              std::to_string(value.id) + "。");
        return;
    }

    used_values.insert(value.id);
}

void verify_ssa_value_def(VerificationResult& result, const Function& function, ValueId value_id,
                          const IRNode& node, const std::string& context,
                          const std::unordered_set<ValueId>& argument_values,
                          std::unordered_map<ValueId, const IRNode*>& node_defs) {
    if (value_id == InvalidValueId) {
        add_error(result, "函数 `" + function.name() + "` 的 " + context + " 产生了无效的 SSA 值。");
        return;
    }

    const SSAValueInfo* info = function.find_value_info(value_id);
    if (info == nullptr) {
        add_error(result, "函数 `" + function.name() + "` 的 " + context + " 产生了未知的 SSA 值 %" +
                              std::to_string(value_id) + "。");
        return;
    }

    if (argument_values.find(value_id) != argument_values.end()) {
        add_error(result, "函数 `" + function.name() + "` 的 " + context + " 重新定义了参数值 %" +
                              std::to_string(value_id) + "。");
        return;
    }

    if (!node_defs.emplace(value_id, &node).second) {
        add_error(result, "函数 `" + function.name() + "` 的 SSA 值 %" +
                              std::to_string(value_id) + " 被重复定义。");
    }
}

void verify_untyped_ssa_values(VerificationResult& result, const Function& function,
                               const std::unordered_set<const BasicBlock*>& known_blocks) {
    if (function.inputs().size() != function.argument_values().size()) {
        add_error(result, "函数 `" + function.name() +
                              "` 的输入名字个数与 SSA 参数值个数不一致。");
    }

    std::unordered_set<ValueId> argument_values;
    for (ValueId value_id : function.argument_values()) {
        if (value_id == InvalidValueId) {
            add_error(result, "函数 `" + function.name() + "` 含有无效的参数值。");
            continue;
        }

        const SSAValueInfo* info = function.find_value_info(value_id);
        if (info == nullptr) {
            add_error(result, "函数 `" + function.name() + "` 的参数值 %" +
                                  std::to_string(value_id) + " 不存在于 value table 中。");
            continue;
        }
        if (!argument_values.insert(value_id).second) {
            add_error(result, "函数 `" + function.name() + "` 的参数值 %" +
                                  std::to_string(value_id) + " 重复出现。");
        }
    }

    std::unordered_map<ValueId, const IRNode*> node_defs;
    std::unordered_set<ValueId> used_values;

    for (const auto& block : function.blocks()) {
        if (block == nullptr) {
            continue;
        }

        for (IRNode* node : block->phi_nodes()) {
            const auto* phi = dynamic_cast<const SSAPhiNode*>(node);
            if (phi == nullptr) {
                continue;
            }

            verify_ssa_value_def(result, function, phi->result(), *phi,
                                 "phi 节点 `" + block->name() + "`", argument_values, node_defs);

            for (const SSAPhiNode::Incoming& incoming : phi->incomings()) {
                if (incoming.predecessor != nullptr &&
                    known_blocks.find(incoming.predecessor) != known_blocks.end() &&
                    BasicBlock::contains_block(block->predecessors(), incoming.predecessor)) {
                    verify_ssa_value_use(result, function, incoming.value,
                                         "phi incoming `" + incoming.predecessor->name() + " -> " +
                                             block->name() + "`",
                                         used_values);
                }
            }
        }

        for (IRNode* node : block->instructions()) {
            const auto* ssa = dynamic_cast<const UntypedSSANode*>(node);
            if (ssa == nullptr) {
                continue;
            }

            switch (ssa->type()) {
                case UntypedSSANode::SSA_Number:
                    verify_ssa_value_def(result, function, static_cast<const SSANumberNode*>(ssa)->result(),
                                         *ssa, "常量节点", argument_values, node_defs);
                    break;
                case UntypedSSANode::SSA_Text:
                    verify_ssa_value_def(result, function, static_cast<const SSATextNode*>(ssa)->result(),
                                         *ssa, "文本常量节点", argument_values, node_defs);
                    break;
                case UntypedSSANode::SSA_Undef:
                    verify_ssa_value_def(result, function, static_cast<const SSAUndefNode*>(ssa)->result(),
                                         *ssa, "undef 节点", argument_values, node_defs);
                    break;
                case UntypedSSANode::SSA_Copy: {
                    const auto* copy = static_cast<const SSACopyNode*>(ssa);
                    verify_ssa_value_def(result, function, copy->result(), *ssa, "copy 节点",
                                         argument_values, node_defs);
                    verify_ssa_value_use(result, function, copy->src(), "copy 源操作数",
                                         used_values);
                    break;
                }
                case UntypedSSANode::SSA_UnaryOp: {
                    const auto* unary = static_cast<const SSAUnaryOpNode*>(ssa);
                    verify_ssa_value_def(result, function, unary->result(), *ssa, "单目运算节点",
                                         argument_values, node_defs);
                    verify_ssa_value_use(result, function, unary->operand(), "单目运算操作数",
                                         used_values);
                    break;
                }
                case UntypedSSANode::SSA_BinOp: {
                    const auto* binop = static_cast<const SSABinOpNode*>(ssa);
                    verify_ssa_value_def(result, function, binop->result(), *ssa, "二元运算节点",
                                         argument_values, node_defs);
                    verify_ssa_value_use(result, function, binop->lhs(), "二元运算 lhs",
                                         used_values);
                    verify_ssa_value_use(result, function, binop->rhs(), "二元运算 rhs",
                                         used_values);
                    break;
                }
                case UntypedSSANode::SSA_Call: {
                    const auto* call = static_cast<const SSACallNode*>(ssa);
                    if (call->callee().type == SSACallNode::Callee::Direct &&
                        call->callee().direct_symbol.empty()) {
                        add_error(result, "函数 `" + function.name() +
                                              "` 的直接调用节点缺少 callee 符号名。");
                    }
                    if (call->callee().type == SSACallNode::Callee::Indirect) {
                        verify_ssa_value_use(result, function, call->callee().indirect_value,
                                             "间接调用 callee", used_values);
                    }
                    for (ValueId result_id : call->results()) {
                        verify_ssa_value_def(result, function, result_id, *ssa, "调用结果",
                                             argument_values, node_defs);
                    }
                    for (const ValueRef& input : call->inputs()) {
                        verify_ssa_value_use(result, function, input, "调用参数", used_values);
                    }
                    break;
                }
                case UntypedSSANode::SSA_Phi:
                case UntypedSSANode::SSA_CondJump:
                case UntypedSSANode::SSA_Jump:
                case UntypedSSANode::SSA_Return:
                    break;
            }
        }

        IRNode* terminal = block->terminal();
        const auto* ssa_terminal = dynamic_cast<const UntypedSSANode*>(terminal);
        if (ssa_terminal == nullptr) {
            continue;
        }

        switch (ssa_terminal->type()) {
            case UntypedSSANode::SSA_CondJump: {
                const auto* cond_jump = static_cast<const SSACondJumpNode*>(ssa_terminal);
                verify_ssa_value_use(result, function, cond_jump->cond(), "条件跳转条件值",
                                     used_values);
                break;
            }
            case UntypedSSANode::SSA_Return: {
                const auto* ret = static_cast<const SSAReturnNode*>(ssa_terminal);
                if (ret->values().size() != function.outputs().size()) {
                    add_error(result, "函数 `" + function.name() +
                                          "` 的返回值个数与输出签名不一致。");
                }
                for (const ValueRef& value : ret->values()) {
                    verify_ssa_value_use(result, function, value, "返回值", used_values);
                }
                break;
            }
            case UntypedSSANode::SSA_Jump:
            case UntypedSSANode::SSA_Number:
            case UntypedSSANode::SSA_Text:
            case UntypedSSANode::SSA_Undef:
            case UntypedSSANode::SSA_Phi:
            case UntypedSSANode::SSA_Copy:
            case UntypedSSANode::SSA_UnaryOp:
            case UntypedSSANode::SSA_BinOp:
            case UntypedSSANode::SSA_Call:
                break;
        }
    }

    for (const SSAValueInfo& info : function.values()) {
        if (info.id == InvalidValueId) {
            add_error(result, "函数 `" + function.name() + "` 的 value table 含有无效 id。");
            continue;
        }

        if (node_defs.find(info.id) == node_defs.end() &&
            argument_values.find(info.id) == argument_values.end()) {
            add_error(result, "函数 `" + function.name() + "` 的 SSA 值 %" +
                                  std::to_string(info.id) +
                                  " 既不是参数值，也没有对应定义节点。");
        }
    }

    for (ValueId used_value : used_values) {
        if (node_defs.find(used_value) == node_defs.end() &&
            argument_values.find(used_value) == argument_values.end()) {
            add_error(result, "函数 `" + function.name() + "` 使用了未定义的 SSA 值 %" +
                                  std::to_string(used_value) + "。");
        }
    }
}

void verify_function_stage_metadata(VerificationResult& result, const Function& function) {
    switch (function.stage()) {
        case IRNode::NonSSA:
            if (!function.argument_values().empty()) {
                add_error(result, "函数 `" + function.name() +
                                      "` 处于 `NonSSA` 阶段，不应携带 SSA 参数值。");
            }
            if (!function.values().empty()) {
                add_error(result, "函数 `" + function.name() +
                                      "` 处于 `NonSSA` 阶段，不应携带 SSA value table。");
            }
            return;
        case IRNode::UntypedSSA:
            return;
        case IRNode::TypedSSA:
            add_error(result, "函数 `" + function.name() +
                                  "` 的 stage 为 `TypedSSA`，当前 verifier 尚未支持。");
            return;
    }
}

void verify_block(VerificationResult& result, const Function& function, const BasicBlock& block,
                  const std::unordered_set<const BasicBlock*>& known_blocks,
                  std::unordered_set<const IRNode*>& seen_nodes) {
    if (block.parent() != &function) {
        add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                              "` 没有正确回指到所属函数。");
    }

    switch (function.stage()) {
        case IRNode::NonSSA:
            verify_non_ssa_block(result, function, block, known_blocks, seen_nodes);
            return;
        case IRNode::UntypedSSA:
            verify_untyped_ssa_block(result, function, block, known_blocks, seen_nodes);
            return;
        case IRNode::TypedSSA:
            verify_unsupported_stage_block(result, function, block, seen_nodes);
            return;
    }
}

}  // namespace

bool VerificationResult::ok() const {
    return diagnostics.empty();
}

void VerificationResult::add_error(std::string message) {
    diagnostics.push_back(VerificationDiagnostic{std::move(message)});
}

std::string VerificationResult::format() const {
    std::ostringstream oss;
    for (std::size_t i = 0; i < diagnostics.size(); ++i) {
        if (i != 0) {
            oss << '\n';
        }
        oss << diagnostics[i].message;
    }
    return oss.str();
}

VerificationResult verify_function(const Function& function) {
    VerificationResult result;

    if (function.entry_block() == nullptr) {
        add_error(result, "函数 `" + function.name() + "` 缺少入口基本块。");
    }

    verify_function_stage_metadata(result, function);

    std::unordered_set<const BasicBlock*> known_blocks;
    for (const auto& block : function.blocks()) {
        if (block == nullptr) {
            add_error(result, "函数 `" + function.name() + "` 含有空基本块。");
            continue;
        }
        known_blocks.insert(block.get());
    }

    if (function.entry_block() != nullptr &&
        known_blocks.find(function.entry_block()) == known_blocks.end()) {
        add_error(result, "函数 `" + function.name() + "` 的入口块不属于该函数。");
    }

    verify_cfg_edges(result, function, known_blocks);

    std::unordered_set<const IRNode*> seen_nodes;
    for (const auto& block : function.blocks()) {
        if (block == nullptr) {
            continue;
        }
        verify_block(result, function, *block, known_blocks, seen_nodes);
    }

    if (function.stage() == IRNode::UntypedSSA) {
        verify_untyped_ssa_values(result, function, known_blocks);
    }

    return result;
}

VerificationResult verify_module(const Module& module) {
    VerificationResult result;

    std::unordered_set<const Function*> known_functions;
    for (const auto& function : module.functions()) {
        if (function == nullptr) {
            add_error(result, "模块 `" + module.name() + "` 含有空函数。");
            continue;
        }
        known_functions.insert(function.get());
        if (function->parent() != &module) {
            add_error(result, "模块 `" + module.name() + "` 中函数 `" + function->name() +
                                  "` 没有正确回指到所属模块。");
        }

        VerificationResult function_result = verify_function(*function);
        result.diagnostics.insert(result.diagnostics.end(), function_result.diagnostics.begin(),
                                  function_result.diagnostics.end());
    }

    if (module.entry_function() == nullptr) {
        add_error(result, "模块 `" + module.name() + "` 缺少入口函数。");
    } else if (known_functions.find(module.entry_function()) == known_functions.end()) {
        add_error(result, "模块 `" + module.name() + "` 的入口函数不属于该模块。");
    }

    return result;
}

void verify_function_or_throw(const Function& function) {
    VerificationResult result = verify_function(function);
    if (!result.ok()) {
        throw std::runtime_error(result.format());
    }
}

void verify_module_or_throw(const Module& module) {
    VerificationResult result = verify_module(module);
    if (!result.ok()) {
        throw std::runtime_error(result.format());
    }
}

}  // namespace analysis
}  // namespace baltam
