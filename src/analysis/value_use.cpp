#include "analysis/value_use.h"

#include <stdexcept>
#include <vector>

namespace baltam {
namespace analysis {
namespace {

std::size_t call_use_index_base(const SSACallNode& call) {
    return call.callee().type == SSACallNode::Callee::Indirect ? 1u : 0u;
}

void append_use(ValueUseAnalysis::Result& result, ValueRef value, ValueUseAnalysis::Use use) {
    if (!value.valid()) {
        return;
    }
    result.uses_of_value[value.id].push_back(std::move(use));
}

void collect_uses_from_node(ValueUseAnalysis::Result& result, IRNode* node,
                            const Function& function) {
    auto* ssa = dynamic_cast<UntypedSSANode*>(node);
    if (ssa == nullptr) {
        throw std::runtime_error("ValueUseAnalysis 失败：函数 `" + function.name() +
                                 "` 含有非 `UntypedSSANode` 节点。");
    }

    switch (ssa->type()) {
        case UntypedSSANode::SSA_Number:
        case UntypedSSANode::SSA_Text:
        case UntypedSSANode::SSA_Undef:
        case UntypedSSANode::SSA_GlobalLoad:
        case UntypedSSANode::SSA_Jump:
            return;
        case UntypedSSANode::SSA_Phi: {
            auto* phi = static_cast<SSAPhiNode*>(ssa);
            for (std::size_t i = 0; i < phi->incomings().size(); ++i) {
                append_use(result, phi->incomings()[i].value, {phi, i});
            }
            return;
        }
        case UntypedSSANode::SSA_Copy: {
            auto* copy = static_cast<SSACopyNode*>(ssa);
            append_use(result, copy->src(), {copy, 0});
            return;
        }
        case UntypedSSANode::SSA_GlobalStore: {
            auto* store = static_cast<SSAGlobalStoreNode*>(ssa);
            append_use(result, store->value(), {store, 0});
            return;
        }
        case UntypedSSANode::SSA_UnaryOp: {
            auto* unary = static_cast<SSAUnaryOpNode*>(ssa);
            append_use(result, unary->operand(), {unary, 0});
            return;
        }
        case UntypedSSANode::SSA_BinOp: {
            auto* binary = static_cast<SSABinOpNode*>(ssa);
            append_use(result, binary->lhs(), {binary, 0});
            append_use(result, binary->rhs(), {binary, 1});
            return;
        }
        case UntypedSSANode::SSA_Call: {
            auto* call = static_cast<SSACallNode*>(ssa);
            if (call->callee().type == SSACallNode::Callee::Indirect) {
                append_use(result, call->callee().indirect_value, {call, 0});
            }
            for (std::size_t i = 0; i < call->inputs().size(); ++i) {
                append_use(result, call->inputs()[i], {call, i + call_use_index_base(*call)});
            }
            return;
        }
        case UntypedSSANode::SSA_CondJump: {
            auto* cond_jump = static_cast<SSACondJumpNode*>(ssa);
            append_use(result, cond_jump->cond(), {cond_jump, 0});
            return;
        }
        case UntypedSSANode::SSA_Return: {
            auto* ret = static_cast<SSAReturnNode*>(ssa);
            for (std::size_t i = 0; i < ret->values().size(); ++i) {
                append_use(result, ret->values()[i], {ret, i});
            }
            return;
        }
    }
}

bool replace_if_equal(ValueRef current, ValueRef old_value, ValueRef new_value) {
    return current.valid() && old_value.valid() && current.id == old_value.id &&
           current.id != new_value.id;
}

}  // namespace

bool ValueUseAnalysis::Use::replace_with(ValueRef old_value, ValueRef new_value) const {
    if (!new_value.valid() || user == nullptr) {
        return false;
    }

    switch (user->type()) {
        case UntypedSSANode::SSA_Phi: {
            auto* phi = dynamic_cast<SSAPhiNode*>(user);
            if (phi == nullptr) {
                return false;
            }
            return phi->replace_value(old_value, new_value);
        }
        case UntypedSSANode::SSA_Copy: {
            auto* copy = dynamic_cast<SSACopyNode*>(user);
            if (copy == nullptr || !replace_if_equal(copy->src(), old_value, new_value)) {
                return false;
            }
            copy->set_src(new_value);
            return true;
        }
        case UntypedSSANode::SSA_GlobalStore: {
            auto* store = dynamic_cast<SSAGlobalStoreNode*>(user);
            if (store == nullptr || !replace_if_equal(store->value(), old_value, new_value)) {
                return false;
            }
            store->set_value(new_value);
            return true;
        }
        case UntypedSSANode::SSA_UnaryOp: {
            auto* unary = dynamic_cast<SSAUnaryOpNode*>(user);
            if (unary == nullptr || !replace_if_equal(unary->operand(), old_value, new_value)) {
                return false;
            }
            unary->set_operand(new_value);
            return true;
        }
        case UntypedSSANode::SSA_BinOp: {
            auto* binary = dynamic_cast<SSABinOpNode*>(user);
            if (binary == nullptr) {
                return false;
            }

            if (index == 0 && replace_if_equal(binary->lhs(), old_value, new_value)) {
                binary->set_lhs(new_value);
                return true;
            }
            if (index == 1 && replace_if_equal(binary->rhs(), old_value, new_value)) {
                binary->set_rhs(new_value);
                return true;
            }
            return false;
        }
        case UntypedSSANode::SSA_Call: {
            auto* call = dynamic_cast<SSACallNode*>(user);
            if (call == nullptr) {
                return false;
            }

            if (call->callee().type == SSACallNode::Callee::Indirect && index == 0 &&
                replace_if_equal(call->callee().indirect_value, old_value, new_value)) {
                call->set_callee_indirect_value(new_value);
                return true;
            }

            const std::size_t input_base = call_use_index_base(*call);
            if (index < input_base) {
                return false;
            }

            const std::size_t input_index = index - input_base;
            if (input_index >= call->inputs().size() ||
                !replace_if_equal(call->inputs()[input_index], old_value, new_value)) {
                return false;
            }
            call->set_input(input_index, new_value);
            return true;
        }
        case UntypedSSANode::SSA_CondJump: {
            auto* cond_jump = dynamic_cast<SSACondJumpNode*>(user);
            if (cond_jump == nullptr ||
                !replace_if_equal(cond_jump->cond(), old_value, new_value)) {
                return false;
            }
            cond_jump->set_cond(new_value);
            return true;
        }
        case UntypedSSANode::SSA_Return: {
            auto* ret = dynamic_cast<SSAReturnNode*>(user);
            if (ret == nullptr || index >= ret->values().size() ||
                !replace_if_equal(ret->values()[index], old_value, new_value)) {
                return false;
            }
            ret->set_value(index, new_value);
            return true;
        }
        case UntypedSSANode::SSA_Number:
        case UntypedSSANode::SSA_Text:
        case UntypedSSANode::SSA_Undef:
        case UntypedSSANode::SSA_GlobalLoad:
        case UntypedSSANode::SSA_Jump:
            return false;
    }

    return false;
}

const std::vector<ValueUseAnalysis::Use>& ValueUseAnalysis::Result::uses_of(ValueId value_id) const {
    static const std::vector<Use> kEmptyUses;

    if (value_id == InvalidValueId) {
        return kEmptyUses;
    }

    auto it = uses_of_value.find(value_id);
    if (it == uses_of_value.end()) {
        return kEmptyUses;
    }
    return it->second;
}

ValueUseAnalysis::Result ValueUseAnalysis::run(Function& function,
                                               FunctionAnalysisManager& analysis_manager) const {
    (void)analysis_manager;

    if (function.stage() != IRNode::UntypedSSA) {
        throw std::runtime_error("ValueUseAnalysis 失败：函数 `" + function.name() +
                                 "` 不是 untyped SSA。");
    }

    Result result;
    for (const auto& block : function.blocks()) {
        if (block == nullptr) {
            continue;
        }

        for (IRNode* node : block->phi_nodes()) {
            if (node != nullptr) {
                collect_uses_from_node(result, node, function);
            }
        }

        for (IRNode* node : block->instructions()) {
            if (node != nullptr) {
                collect_uses_from_node(result, node, function);
            }
        }

        if (block->terminal() != nullptr) {
            collect_uses_from_node(result, block->terminal(), function);
        }
    }

    return result;
}

}  // namespace analysis
}  // namespace baltam
