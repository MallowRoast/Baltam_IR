#include "analysis/verifier.h"

#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace baltam {
namespace analysis {
namespace {

const char* instruction_type_name(Instruction::Type type) {
    switch (type) {
        case Instruction::Text:
            return "Text";
        case Instruction::Binding:
            return "Binding";
        case Instruction::Number:
            return "Number";
        case Instruction::Undef:
            return "Undef";
        case Instruction::UnaryOp:
            return "UnaryOp";
        case Instruction::BinOp:
            return "BinOp";
        case Instruction::Phi:
            return "Phi";
        case Instruction::Asgn:
            return "Asgn";
        case Instruction::Call:
            return "Call";
        case Instruction::CondJump:
            return "CondJump";
        case Instruction::Jump:
            return "Jump";
        case Instruction::Return:
            return "Return";
    }

    return "Unknown";
}

std::string block_name(const BasicBlock* block) {
    return block != nullptr ? block->name() : "<null>";
}

bool is_terminator_type(Instruction::Type type) {
    return type == Instruction::CondJump || type == Instruction::Jump || type == Instruction::Return;
}

void add_error(VerificationResult& result, std::string message) {
    result.add_error(std::move(message));
}

void verify_value_def(VerificationResult& result, const Function& function, const Instruction& instruction,
                      const InstValue& value, std::unordered_map<ValueId, std::string>& owners) {
    if (!value.is_valid()) {
        add_error(result, "函数 `" + function.name() + "` 中的指令 `" +
                              instruction_type_name(instruction.type()) +
                              "` 挂接了非法 ValueId。");
        return;
    }

    const std::string owner_text =
        "instruction " + std::string(instruction_type_name(instruction.type()));
    const auto [it, inserted] = owners.emplace(value.id, owner_text);
    if (!inserted) {
        add_error(result, "函数 `" + function.name() + "` 中的 ValueId `" +
                              std::to_string(value.id) + "` 被重复定义：`" + it->second +
                              "` 和 `" + owner_text + "`。");
    }
}

void collect_value_defs(VerificationResult& result, const Function& function,
                        std::unordered_map<ValueId, std::string>& owners) {
    if (function.input_names().size() != function.input_values().size()) {
        add_error(result, "函数 `" + function.name() +
                              "` 的 input_names 与 input_values 个数不一致。");
    }

    for (std::size_t i = 0; i < function.input_values().size(); ++i) {
        const InstValue& value = function.input_values()[i];
        if (!value.is_valid()) {
            add_error(result, "函数 `" + function.name() + "` 的输入参数 #" +
                                  std::to_string(i) + " 没有合法 ValueId。");
            continue;
        }

        const std::string owner_text = "function input";
        const auto [it, inserted] = owners.emplace(value.id, owner_text);
        if (!inserted) {
            add_error(result, "函数 `" + function.name() + "` 的输入参数 ValueId `" +
                                  std::to_string(value.id) + "` 与 `" + it->second +
                                  "` 重复。");
        }
    }

    for (const auto& block : function.blocks()) {
        for (Instruction* instruction : block->instructions()) {
            if (instruction == nullptr) {
                add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block->name() +
                                      "` 含有空指令指针。");
                continue;
            }
            for (const InstValue& value : instruction->value_defs()) {
                verify_value_def(result, function, *instruction, value, owners);
            }
        }

        if (Instruction* terminal = block->terminal(); terminal != nullptr) {
            for (const InstValue& value : terminal->value_defs()) {
                verify_value_def(result, function, *terminal, value, owners);
            }
        }
    }
}

void verify_value_ref(VerificationResult& result, const Function& function, ValueRef ref,
                      const std::string& context) {
    if (!ref.is_valid()) {
        add_error(result, "函数 `" + function.name() + "` 中 `" + context + "` 使用了非法 ValueRef。");
        return;
    }

    if (function.find_value(ref.id) == nullptr) {
        add_error(result, "函数 `" + function.name() + "` 中 `" + context + "` 引用了不存在的 ValueId `" +
                              std::to_string(ref.id) + "`。");
    }
}

void verify_instruction_uses(VerificationResult& result, const Function& function,
                             const Instruction& instruction, const std::string& context) {
    switch (instruction.type()) {
        case Instruction::Text:
        case Instruction::Binding:
        case Instruction::Number:
        case Instruction::Undef:
        case Instruction::Jump:
            return;
        case Instruction::UnaryOp:
            verify_value_ref(result, function,
                             static_cast<const UnaryOpInstruction&>(instruction).operand_ref(), context);
            return;
        case Instruction::BinOp: {
            const auto& binop = static_cast<const BinOpInstruction&>(instruction);
            verify_value_ref(result, function, binop.lhs_ref(), context + " 的 lhs");
            verify_value_ref(result, function, binop.rhs_ref(), context + " 的 rhs");
            return;
        }
        case Instruction::Phi: {
            const auto& phi = static_cast<const PhiInstruction&>(instruction);
            for (std::size_t i = 0; i < phi.incoming_count(); ++i) {
                const PhiInstruction::Incoming* incoming = phi.incoming(i);
                if (incoming == nullptr) {
                    add_error(result, "函数 `" + function.name() + "` 中 `" + context +
                                          "` 含有空的 phi incoming。");
                    continue;
                }
                verify_value_ref(result, function, incoming->value_ref,
                                 context + " 的 incoming #" + std::to_string(i));
            }
            return;
        }
        case Instruction::Asgn:
            verify_value_ref(result, function,
                             static_cast<const AssignInstruction&>(instruction).value_ref(), context);
            return;
        case Instruction::Call: {
            const auto& call = static_cast<const CallInstruction&>(instruction);
            if (call.is_indirect()) {
                verify_value_ref(result, function, call.callee_ref(), context + " 的 callee");
            }
            for (std::size_t i = 0; i < call.input_count(); ++i) {
                verify_value_ref(result, function, call.input_ref(i),
                                 context + " 的 input #" + std::to_string(i));
            }
            return;
        }
        case Instruction::CondJump:
            verify_value_ref(result, function,
                             static_cast<const CondJumpInstruction&>(instruction).cond_ref(), context);
            return;
        case Instruction::Return: {
            const auto& ret = static_cast<const ReturnInstruction&>(instruction);
            for (std::size_t i = 0; i < ret.return_value_count(); ++i) {
                verify_value_ref(result, function, ret.return_value_ref(i),
                                 context + " 的 return value #" + std::to_string(i));
            }
            return;
        }
    }
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
                add_error(result, "函数 `" + function.name() + "` 中 CFG 不一致：块 `" + block->name() +
                                      "` 声明 successor `" + successor->name() +
                                      "`，但对方前驱列表中缺少该块。");
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
                add_error(result, "函数 `" + function.name() + "` 中 CFG 不一致：块 `" + block->name() +
                                      "` 声明 predecessor `" + predecessor->name() +
                                      "`，但对方后继列表中缺少该块。");
            }
        }
    }
}

void verify_phi_nodes(VerificationResult& result, const Function& function, const BasicBlock& block) {
    std::unordered_set<const BasicBlock*> predecessor_set;
    for (BasicBlock* predecessor : block.predecessors()) {
        if (predecessor != nullptr) {
            predecessor_set.insert(predecessor);
        }
    }

    bool seen_non_phi = false;
    for (Instruction* instruction : block.instructions()) {
        if (instruction == nullptr) {
            continue;
        }

        if (instruction->type() == Instruction::Phi) {
            if (seen_non_phi) {
                add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                                      "` 在非 phi 指令之后出现了 phi。");
            }

            if (!instruction->has_values() || instruction->value_count() != 1) {
                add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                                      "` 中 phi 节点必须且只能定义一个结果值。");
            }

            const auto* phi = static_cast<const PhiInstruction*>(instruction);
            if (phi->incoming_count() != predecessor_set.size()) {
                add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                                      "` 中 phi incoming 数量与前驱数量不匹配。");
            }

            std::unordered_set<const BasicBlock*> seen_predecessors;
            for (const PhiInstruction::Incoming& incoming : phi->incomings()) {
                if (incoming.predecessor == nullptr) {
                    add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                                          "` 中 phi 含有空 predecessor。");
                    continue;
                }
                if (predecessor_set.find(incoming.predecessor) == predecessor_set.end()) {
                    add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                                          "` 中 phi 引用了非前驱块 `" +
                                          incoming.predecessor->name() + "`。");
                }
                if (!seen_predecessors.insert(incoming.predecessor).second) {
                    add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                                          "` 中 phi 对前驱块 `" +
                                          incoming.predecessor->name() + "` 重复建边。");
                }
            }
            continue;
        }

        seen_non_phi = true;
    }
}

void verify_block(VerificationResult& result, const Function& function, const BasicBlock& block,
                  const std::unordered_set<const BasicBlock*>& known_blocks,
                  std::unordered_set<const Instruction*>& seen_instructions) {
    if (block.parent() != &function) {
        add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                              "` 没有正确回指到所属函数。");
    }

    verify_phi_nodes(result, function, block);

    bool seen_non_phi = false;
    for (Instruction* instruction : block.instructions()) {
        if (instruction == nullptr) {
            add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                                  "` 含有空指令。");
            continue;
        }

        if (instruction->parent() != &block) {
            add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                                  "` 中存在 parent 不一致的指令。");
        }
        if (!seen_instructions.insert(instruction).second) {
            add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                                  "` 重复引用了同一条指令。");
        }
        if (is_terminator_type(instruction->type())) {
            add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                                  "` 在正文中包含终结指令 `" +
                                  instruction_type_name(instruction->type()) + "`。");
        }
        if (instruction->type() != Instruction::Phi) {
            seen_non_phi = true;
        } else if (seen_non_phi) {
            add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                                  "` 在普通指令之后出现了 phi。");
        }

        verify_instruction_uses(
            result, function, *instruction,
            "基本块 `" + block.name() + "` 的指令 `" + instruction_type_name(instruction->type()) + "`");
    }

    Instruction* terminal = block.terminal();
    if (terminal == nullptr) {
        add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                              "` 缺少终结指令。");
        if (!block.successors().empty()) {
            add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                                  "` 缺少终结指令，但仍声明了 successor。");
        }
        return;
    }

    if (terminal->parent() != &block) {
        add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                              "` 的 terminal parent 不一致。");
    }
    if (!seen_instructions.insert(terminal).second) {
        add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                              "` 的 terminal 指令被重复挂接。");
    }
    if (!is_terminator_type(terminal->type())) {
        add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                              "` 的 terminal 不是合法终结指令，而是 `" +
                              instruction_type_name(terminal->type()) + "`。");
    }

    verify_instruction_uses(
        result, function, *terminal,
        "基本块 `" + block.name() + "` 的 terminal `" + instruction_type_name(terminal->type()) + "`");

    switch (terminal->type()) {
        case Instruction::CondJump: {
            const auto* cond_jump = static_cast<const CondJumpInstruction*>(terminal);
            BasicBlock* true_block = cond_jump->true_block();
            BasicBlock* false_block = cond_jump->false_block();
            if (true_block == nullptr || false_block == nullptr) {
                add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                                      "` 的条件跳转缺少目标块。");
                break;
            }
            if (known_blocks.find(true_block) == known_blocks.end() ||
                known_blocks.find(false_block) == known_blocks.end()) {
                add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                                      "` 的条件跳转指向了本函数之外的块。");
            }
            if (!BasicBlock::contains_block(block.successors(), true_block)) {
                add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                                      "` 的条件跳转 true 目标不在 successor 列表里。");
            }
            if (!BasicBlock::contains_block(block.successors(), false_block)) {
                add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                                      "` 的条件跳转 false 目标不在 successor 列表里。");
            }
            break;
        }
        case Instruction::Jump: {
            BasicBlock* target = static_cast<const JumpInstruction*>(terminal)->target();
            if (target == nullptr) {
                add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                                      "` 的无条件跳转缺少目标块。");
                break;
            }
            if (known_blocks.find(target) == known_blocks.end()) {
                add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                                      "` 的无条件跳转指向了本函数之外的块。");
            }
            if (!BasicBlock::contains_block(block.successors(), target)) {
                add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                                      "` 的跳转目标不在 successor 列表里。");
            }
            break;
        }
        case Instruction::Return:
            if (!block.successors().empty()) {
                add_error(result, "函数 `" + function.name() + "` 的基本块 `" + block.name() +
                                      "` 是 return 块，但 successor 列表非空。");
            }
            break;
        case Instruction::Text:
        case Instruction::Binding:
        case Instruction::Number:
        case Instruction::Undef:
        case Instruction::UnaryOp:
        case Instruction::BinOp:
        case Instruction::Phi:
        case Instruction::Asgn:
        case Instruction::Call:
            break;
    }
}

std::string prefix_lines(const std::string& prefix, const std::string& text) {
    std::ostringstream oss;
    std::istringstream iss(text);
    std::string line;
    bool first = true;
    while (std::getline(iss, line)) {
        if (!first) {
            oss << '\n';
        }
        first = false;
        oss << prefix << line;
    }
    return oss.str();
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
    std::unordered_set<std::string> block_names;
    for (const auto& block : function.blocks()) {
        if (block == nullptr) {
            add_error(result, "函数 `" + function.name() + "` 含有空基本块。");
            continue;
        }
        known_blocks.insert(block.get());
        if (!block_names.insert(block->name()).second) {
            add_error(result, "函数 `" + function.name() + "` 中出现了重复基本块名 `" +
                                  block->name() + "`。");
        }
    }

    if (function.entry_block() != nullptr &&
        known_blocks.find(function.entry_block()) == known_blocks.end()) {
        add_error(result, "函数 `" + function.name() + "` 的入口基本块不属于该函数。");
    }

    std::unordered_map<ValueId, std::string> value_owners;
    collect_value_defs(result, function, value_owners);

    verify_cfg_edges(result, function, known_blocks);

    std::unordered_set<const Instruction*> seen_instructions;
    for (const auto& block : function.blocks()) {
        if (block == nullptr) {
            continue;
        }
        verify_block(result, function, *block, known_blocks, seen_instructions);
    }

    return result;
}

VerificationResult verify_module(const Module& module) {
    VerificationResult result;

    std::unordered_set<const Function*> known_functions;
    std::unordered_set<std::string> function_names;
    for (const auto& function : module.functions()) {
        if (function == nullptr) {
            add_error(result, "模块 `" + module.name() + "` 含有空函数。");
            continue;
        }

        known_functions.insert(function.get());
        if (!function_names.insert(function->name()).second) {
            add_error(result, "模块 `" + module.name() + "` 中出现了重复函数名 `" +
                                  function->name() + "`。");
        }
        if (function->parent() != &module) {
            add_error(result, "模块 `" + module.name() + "` 中函数 `" + function->name() +
                                  "` 没有正确回指到所属模块。");
        }

        VerificationResult function_result = verify_function(*function);
        for (VerificationDiagnostic& diagnostic : function_result.diagnostics) {
            add_error(result, "在函数 `" + function->name() + "` 中：" + diagnostic.message);
        }
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
        throw std::runtime_error("IR verifier 失败：\n" + prefix_lines("  ", result.format()));
    }
}

void verify_module_or_throw(const Module& module) {
    VerificationResult result = verify_module(module);
    if (!result.ok()) {
        throw std::runtime_error("IR verifier 失败：\n" + prefix_lines("  ", result.format()));
    }
}

}  // namespace analysis
}  // namespace baltam
