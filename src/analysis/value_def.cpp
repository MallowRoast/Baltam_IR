#include "analysis/value_def.h"

#include <stdexcept>
#include <string>

namespace baltam {
namespace analysis {
namespace {

void record_definition(ValueDefAnalysis::Result& result, ValueId value_id,
                       const UntypedSSANode& node, const Function& function) {
    if (value_id == InvalidValueId) {
        return;
    }

    auto [it, inserted] = result.defs_of_value.emplace(value_id, &node);
    if (!inserted && it->second != &node) {
        throw std::runtime_error("ValueDefAnalysis 失败：函数 `" + function.name() +
                                 "` 中值 %" + std::to_string(value_id) +
                                 " 出现了重复定义。");
    }
}

void record_definition_from_node(ValueDefAnalysis::Result& result, const IRNode& node,
                                 const Function& function) {
    const auto* ssa = dynamic_cast<const UntypedSSANode*>(&node);
    if (ssa == nullptr) {
        throw std::runtime_error("ValueDefAnalysis 失败：函数 `" + function.name() +
                                 "` 含有非 `UntypedSSANode` 节点。");
    }

    switch (ssa->type()) {
        case UntypedSSANode::SSA_Number:
            record_definition(result, static_cast<const SSANumberNode*>(ssa)->result(), *ssa,
                              function);
            break;
        case UntypedSSANode::SSA_Text:
            record_definition(result, static_cast<const SSATextNode*>(ssa)->result(), *ssa,
                              function);
            break;
        case UntypedSSANode::SSA_Undef:
            record_definition(result, static_cast<const SSAUndefNode*>(ssa)->result(), *ssa,
                              function);
            break;
        case UntypedSSANode::SSA_Phi:
            record_definition(result, static_cast<const SSAPhiNode*>(ssa)->result(), *ssa,
                              function);
            break;
        case UntypedSSANode::SSA_Copy:
            record_definition(result, static_cast<const SSACopyNode*>(ssa)->result(), *ssa,
                              function);
            break;
        case UntypedSSANode::SSA_GlobalLoad:
            record_definition(result, static_cast<const SSAGlobalLoadNode*>(ssa)->result(), *ssa,
                              function);
            break;
        case UntypedSSANode::SSA_UnaryOp:
            record_definition(result, static_cast<const SSAUnaryOpNode*>(ssa)->result(), *ssa,
                              function);
            break;
        case UntypedSSANode::SSA_BinOp:
            record_definition(result, static_cast<const SSABinOpNode*>(ssa)->result(), *ssa,
                              function);
            break;
        case UntypedSSANode::SSA_Call: {
            const auto& results = static_cast<const SSACallNode*>(ssa)->results();
            for (ValueId result_id : results) {
                record_definition(result, result_id, *ssa, function);
            }
            break;
        }
        case UntypedSSANode::SSA_GlobalStore:
        case UntypedSSANode::SSA_CondJump:
        case UntypedSSANode::SSA_Jump:
        case UntypedSSANode::SSA_Return:
            break;
    }
}

}  // namespace

const UntypedSSANode* ValueDefAnalysis::Result::definition_of(ValueId value_id) const {
    if (value_id == InvalidValueId) {
        return nullptr;
    }

    auto it = defs_of_value.find(value_id);
    if (it == defs_of_value.end()) {
        return nullptr;
    }
    return it->second;
}

ValueDefAnalysis::Result ValueDefAnalysis::run(Function& function,
                                               FunctionAnalysisManager& analysis_manager) const {
    (void)analysis_manager;

    if (function.stage() != IRNode::UntypedSSA) {
        throw std::runtime_error("ValueDefAnalysis 失败：函数 `" + function.name() +
                                 "` 不是 untyped SSA。");
    }

    Result result;
    for (const auto& block : function.blocks()) {
        if (block == nullptr) {
            continue;
        }

        for (IRNode* node : block->phi_nodes()) {
            if (node == nullptr) {
                continue;
            }
            record_definition_from_node(result, *node, function);
        }

        for (IRNode* node : block->instructions()) {
            if (node == nullptr) {
                continue;
            }
            record_definition_from_node(result, *node, function);
        }
    }

    return result;
}

}  // namespace analysis
}  // namespace baltam
