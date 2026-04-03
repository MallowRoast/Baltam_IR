#ifndef BALTAM_IR_ANALYSIS_NAME_ANALYSIS_UTILS_H
#define BALTAM_IR_ANALYSIS_NAME_ANALYSIS_UTILS_H

#include "ir/ir.h"

namespace baltam {
namespace analysis {
namespace detail {

template <typename Callback>
void for_each_block_node(const BasicBlock& block, Callback&& callback) {
    for (NonSSANode* node : block.instructions()) {
        if (node != nullptr) {
            callback(*node);
        }
    }

    if (block.terminal() != nullptr) {
        callback(*block.terminal());
    }
}

template <typename Callback>
void for_each_node_use(const NonSSANode& node, Callback&& callback) {
    switch (node.type()) {
    case NonSSANode::Number:
    case NonSSANode::Text:
    case NonSSANode::Jump:
        return;

    case NonSSANode::Assign: {
        const AssignNode& assign = static_cast<const AssignNode&>(node);
        if (!assign.src().name.empty()) {
            callback(assign.src().name);
        }
        return;
    }

    case NonSSANode::UnaryOp: {
        const UnaryOpNode& unary = static_cast<const UnaryOpNode&>(node);
        if (!unary.operand().name.empty()) {
            callback(unary.operand().name);
        }
        return;
    }

    case NonSSANode::BinOp: {
        const BinOpNode& binop = static_cast<const BinOpNode&>(node);
        if (!binop.lhs().name.empty()) {
            callback(binop.lhs().name);
        }
        if (!binop.rhs().name.empty()) {
            callback(binop.rhs().name);
        }
        return;
    }

    case NonSSANode::Call: {
        const CallNode& call = static_cast<const CallNode&>(node);
        if (call.callee_type() == CallNode::Indirect && !call.callee().empty()) {
            callback(call.callee());
        }
        for (const NamedValue& input : call.inputs()) {
            if (!input.name.empty()) {
                callback(input.name);
            }
        }
        return;
    }

    case NonSSANode::CondJump: {
        const CondJumpNode& cond_jump = static_cast<const CondJumpNode&>(node);
        if (!cond_jump.cond().name.empty()) {
            callback(cond_jump.cond().name);
        }
        return;
    }

    case NonSSANode::Return: {
        const ReturnNode& return_node = static_cast<const ReturnNode&>(node);
        for (const NamedValue& value : return_node.values()) {
            if (!value.name.empty()) {
                callback(value.name);
            }
        }
        return;
    }
    }
}

template <typename Callback>
void for_each_node_def(const NonSSANode& node, Callback&& callback) {
    switch (node.type()) {
    case NonSSANode::Number: {
        const NumberNode& number = static_cast<const NumberNode&>(node);
        if (!number.result().name.empty()) {
            callback(number.result().name);
        }
        return;
    }

    case NonSSANode::Text: {
        const TextNode& text = static_cast<const TextNode&>(node);
        if (!text.result().name.empty()) {
            callback(text.result().name);
        }
        return;
    }

    case NonSSANode::Assign: {
        const AssignNode& assign = static_cast<const AssignNode&>(node);
        if (!assign.dst().name.empty()) {
            callback(assign.dst().name);
        }
        return;
    }

    case NonSSANode::UnaryOp: {
        const UnaryOpNode& unary = static_cast<const UnaryOpNode&>(node);
        if (!unary.result().name.empty()) {
            callback(unary.result().name);
        }
        return;
    }

    case NonSSANode::BinOp: {
        const BinOpNode& binop = static_cast<const BinOpNode&>(node);
        if (!binop.result().name.empty()) {
            callback(binop.result().name);
        }
        return;
    }

    case NonSSANode::Call: {
        const CallNode& call = static_cast<const CallNode&>(node);
        for (const NamedValue& output : call.outputs()) {
            if (!output.name.empty()) {
                callback(output.name);
            }
        }
        return;
    }

    case NonSSANode::CondJump:
    case NonSSANode::Jump:
    case NonSSANode::Return:
        return;
    }
}

}  // namespace detail
}  // namespace analysis
}  // namespace baltam

#endif
