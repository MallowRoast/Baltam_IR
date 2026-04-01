#include "analysis/verifier.h"

#include <sstream>
#include <stdexcept>
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

bool is_terminator_type(NonSSANode::Type type) {
    return type == NonSSANode::CondJump || type == NonSSANode::Jump || type == NonSSANode::Return;
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

void verify_block(VerificationResult& result, const Function& function, const BasicBlock& block,
                  const std::unordered_set<const BasicBlock*>& known_blocks,
                  std::unordered_set<const NonSSANode*>& seen_nodes) {
    if (block.parent() != &function) {
        add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                              "` 没有正确回指到所属函数。");
    }

    for (NonSSANode* node : block.instructions()) {
        if (node == nullptr) {
            add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                                  "` 含有空指令。");
            continue;
        }
        if (node->parent() != &block) {
            add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                                  "` 中存在 parent 不一致的节点。");
        }
        if (!seen_nodes.insert(node).second) {
            add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                                  "` 重复引用了同一条节点。");
        }
        if (is_terminator_type(node->type())) {
            add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                                  "` 在正文中包含终结节点 `" + node_type_name(node->type()) +
                                  "`。");
        }
    }

    NonSSANode* terminal = block.terminal();
    if (terminal == nullptr) {
        add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                              "` 缺少终结节点。");
        return;
    }
    if (terminal->parent() != &block) {
        add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                              "` 的终结节点 parent 不一致。");
    }
    if (!seen_nodes.insert(terminal).second) {
        add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                              "` 重复引用了终结节点。");
    }
    if (!is_terminator_type(terminal->type())) {
        add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                              "` 的终结节点类型非法：`" + node_type_name(terminal->type()) + "`。");
        return;
    }

    switch (terminal->type()) {
        case NonSSANode::CondJump: {
            const auto* cond_jump = static_cast<const CondJumpNode*>(terminal);
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
            BasicBlock* target = static_cast<const JumpNode*>(terminal)->target();
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

    std::unordered_set<const NonSSANode*> seen_nodes;
    for (const auto& block : function.blocks()) {
        if (block == nullptr) {
            continue;
        }
        verify_block(result, function, *block, known_blocks, seen_nodes);
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
