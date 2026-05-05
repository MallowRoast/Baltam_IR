#include "ir/ir_verify.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace baltam {
namespace {

class IRVerifier final {
public:
    explicit IRVerifier(const IRVerifyOptions& options) : options_(options) {}

    [[nodiscard]] IRVerifyResult verify(const MFileUnit& mfile) {
        result_ = {};
        verify_mfile(mfile);
        return std::move(result_);
    }

private:
    void error(std::string message, SourceSpan source_span = SourceSpan::invalid()) {
        result_.diagnostics.push_back({
            IRVerifyDiagnostic::Error,
            InternedString(std::move(message)),
            source_span,
        });
    }

    void warning(std::string message, SourceSpan source_span = SourceSpan::invalid()) {
        result_.diagnostics.push_back({
            IRVerifyDiagnostic::Warning,
            InternedString(std::move(message)),
            source_span,
        });
    }

    [[nodiscard]] bool contains_unit(const MFileUnit& mfile, const CodeUnit* unit) const {
        return unit != nullptr &&
            std::any_of(
                mfile.code_units.begin(),
                mfile.code_units.end(),
                [unit](const std::unique_ptr<CodeUnit>& candidate) {
                    return candidate.get() == unit;
                });
    }

    [[nodiscard]] bool contains_block(const CodeUnit& unit, const BasicBlock* block) const {
        return block != nullptr &&
            std::any_of(
                unit.basic_blocks.begin(),
                unit.basic_blocks.end(),
                [block](const std::unique_ptr<BasicBlock>& candidate) {
                    return candidate.get() == block;
                });
    }

    [[nodiscard]] bool has_successor(const BasicBlock& block, const BasicBlock* successor) const {
        return std::find(block.successors.begin(), block.successors.end(), successor) !=
            block.successors.end();
    }

    [[nodiscard]] bool has_predecessor(
        const BasicBlock& block,
        const BasicBlock* predecessor) const {
        return std::find(block.predecessors.begin(), block.predecessors.end(), predecessor) !=
            block.predecessors.end();
    }

    [[nodiscard]] bool has_slot(const CodeUnit& unit, SlotId slot_id) const {
        return slot_id.is_valid() && unit.slot_table.find_slot(slot_id) != nullptr;
    }

    [[nodiscard]] bool has_value(ValueId value_id) const {
        return value_id.is_valid() && defined_values_.find(value_id) != defined_values_.end();
    }

    void verify_mfile(const MFileUnit& mfile) {
        if (mfile.entry_unit == nullptr) {
            error("文件入口代码单元不能为空");
        } else if (!contains_unit(mfile, mfile.entry_unit)) {
            error("文件入口代码单元必须属于当前文件");
        }

        for (const auto& unit_ptr : mfile.code_units) {
            if (unit_ptr == nullptr) {
                error("文件中不能包含空代码单元");
                continue;
            }

            if (unit_ptr->parent != &mfile) {
                error("代码单元 parent 必须指回所属文件", unit_ptr->source_span);
            }
        }

        for (const auto& [name, function] : mfile.local_function_map) {
            if (function == nullptr) {
                error("local_function_map 不能包含空函数目标");
                continue;
            }
            if (!contains_unit(mfile, function)) {
                error("local_function_map 的函数目标必须属于当前文件", function->source_span);
            }
            if (function->name != name) {
                error("local_function_map 的名字必须与目标函数名一致", function->source_span);
            }
        }

        for (const auto& unit_ptr : mfile.code_units) {
            if (unit_ptr != nullptr) {
                verify_code_unit(*unit_ptr, mfile);
            }
        }
    }

    void verify_code_unit(const CodeUnit& unit, const MFileUnit& mfile) {
        current_unit_ = &unit;
        defined_values_.clear();

        if (unit.type() == CodeUnit::Script) {
            if (dynamic_cast<const ScriptUnit*>(&unit) == nullptr) {
                error("type() 为 Script 的代码单元必须是 ScriptUnit", unit.source_span);
            }
        } else if (unit.type() == CodeUnit::Function) {
            if (dynamic_cast<const FunctionUnit*>(&unit) == nullptr) {
                error("type() 为 Function 的代码单元必须是 FunctionUnit", unit.source_span);
            }
        } else {
            error("代码单元 type() 返回了未知类型", unit.source_span);
        }

        if (unit.entry_block == nullptr) {
            error("代码单元入口基本块不能为空", unit.source_span);
        } else if (!contains_block(unit, unit.entry_block)) {
            error("代码单元入口基本块必须属于当前代码单元", unit.entry_block->source_span);
        }

        for (const auto& block_ptr : unit.basic_blocks) {
            if (block_ptr == nullptr) {
                error("代码单元中不能包含空基本块", unit.source_span);
                continue;
            }
            if (block_ptr->parent != &unit) {
                error("基本块 parent 必须指回所属代码单元", block_ptr->source_span);
            }
        }

        verify_slots(unit);
        verify_value_table(unit);
        if (unit.is_function()) {
            verify_function_unit(static_cast<const FunctionUnit&>(unit));
        }

        for (const auto& block_ptr : unit.basic_blocks) {
            if (block_ptr != nullptr) {
                verify_block(*block_ptr, unit, mfile);
            }
        }

        verify_cfg_edges(unit);
        current_unit_ = nullptr;
        defined_values_.clear();
    }

    void verify_slots(const CodeUnit& unit) {
        std::unordered_set<SlotId> seen_slots;
        std::unordered_map<std::uint8_t, SlotId> hidden_roles;

        for (std::size_t index = 0; index < unit.slot_table.slots.size(); ++index) {
            const Slot& slot = unit.slot_table.slots[index];
            if (!slot.slot_id.is_valid()) {
                error("slot_id 不能为空", slot.source_span);
                continue;
            }
            if (!seen_slots.insert(slot.slot_id).second) {
                error("同一个代码单元内 slot_id 不能重复", slot.source_span);
            }
            if (options_.require_dense_slot_ids && slot.slot_id.value() != index) {
                warning("slot_id 不符合当前 builder 的递增分配顺序", slot.source_span);
            }

            if (slot.is_hidden()) {
                if (slot.attrs.hidden_role == SlotAttrs::None) {
                    error("Hidden slot 必须设置非 None 的 hidden_role", slot.source_span);
                } else {
                    const auto [it, inserted] = hidden_roles.emplace(
                        slot.attrs.hidden_role,
                        slot.slot_id);
                    if (!inserted) {
                        error("同一个代码单元中同一 hidden_role 至多出现一次", slot.source_span);
                    }
                }
            } else if (slot.attrs.hidden_role != SlotAttrs::None) {
                error("非 Hidden slot 的 hidden_role 必须为 None", slot.source_span);
            }

            if (slot.attrs.hidden_role == SlotAttrs::WorkspaceHandle && !unit.is_script()) {
                error("WorkspaceHandle hidden slot 只能出现在脚本代码单元中", slot.source_span);
            }

            if ((slot.attrs.hidden_role == SlotAttrs::Nargin ||
                 slot.attrs.hidden_role == SlotAttrs::Nargout ||
                 slot.attrs.hidden_role == SlotAttrs::Varargin ||
                 slot.attrs.hidden_role == SlotAttrs::Varargout) &&
                !unit.is_function()) {
                error("函数调用约定 hidden slot 只能出现在函数代码单元中", slot.source_span);
            }
        }
    }

    void verify_value_table(const CodeUnit& unit) {
        for (std::size_t index = 0; index < unit.value_table.values.size(); ++index) {
            const ValueInfo& value_info = unit.value_table.values[index];
            if (!value_info.value_id.is_valid()) {
                error("value_table 中的 value_id 不能为空", unit.source_span);
                continue;
            }
            if (value_info.value_id.value() != index) {
                warning("value_table 未按 ValueId 稠密顺序排列", unit.source_span);
            }
            if (value_info.def == nullptr) {
                continue;
            }
            if (value_info.def->parent == nullptr || value_info.def->parent->parent != &unit) {
                error("ValueInfo.def 必须属于当前代码单元", unit.source_span);
            }
            if (!instruction_defines_value(*value_info.def, value_info.value_id, value_info.result_index)) {
                error("ValueInfo.def 与记录的 ValueId/result_index 不一致", value_info.def->source_span);
            }
        }
    }

    [[nodiscard]] bool instruction_defines_value(
        const Instruction& instruction,
        ValueId value_id,
        std::size_t result_index) const {
        switch (instruction.type()) {
            case Instruction::Const:
                return static_cast<const ConstInst&>(instruction).result == value_id && result_index == 0;
            case Instruction::LoadSlot:
                return static_cast<const LoadSlotInst&>(instruction).result == value_id && result_index == 0;
            case Instruction::LoadWorkspace:
                return static_cast<const LoadWorkspaceInst&>(instruction).result == value_id && result_index == 0;
            case Instruction::Copy:
                return static_cast<const CopyInst&>(instruction).result == value_id && result_index == 0;
            case Instruction::Unary:
                return static_cast<const UnaryInst&>(instruction).result == value_id && result_index == 0;
            case Instruction::Binary:
                return static_cast<const BinaryInst&>(instruction).result == value_id && result_index == 0;
            case Instruction::Apply: {
                const auto& inst = static_cast<const ApplyInst&>(instruction);
                return result_index < inst.results.size() && inst.results[result_index] == value_id;
            }
            case Instruction::Call: {
                const auto& inst = static_cast<const CallInst&>(instruction);
                return result_index < inst.results.size() && inst.results[result_index] == value_id;
            }
            case Instruction::StoreSlot:
            case Instruction::StoreWorkspace:
            case Instruction::Goto:
            case Instruction::Branch:
            case Instruction::Return:
                return false;
        }
        return false;
    }

    void verify_function_unit(const FunctionUnit& function) {
        std::unordered_set<SlotId> seen_params;
        for (SlotId slot_id : function.param_slots) {
            const Slot* slot = function.slot_table.find_slot(slot_id);
            if (slot == nullptr) {
                error("函数参数列表引用了不存在的 slot", function.source_span);
                continue;
            }
            if (!slot->is_arg()) {
                error("函数参数列表只能引用 Arg slot", slot->source_span);
            }
            if (!seen_params.insert(slot_id).second) {
                error("函数参数列表不能重复引用同一个 slot", slot->source_span);
            }
        }

        std::unordered_set<SlotId> seen_returns;
        for (SlotId slot_id : function.return_slots) {
            const Slot* slot = function.slot_table.find_slot(slot_id);
            if (slot == nullptr) {
                error("函数返回值列表引用了不存在的 slot", function.source_span);
                continue;
            }
            if (!slot->is_ret()) {
                error("函数返回值列表只能引用 Ret slot", slot->source_span);
            }
            if (!seen_returns.insert(slot_id).second) {
                error("函数返回值列表不能重复引用同一个 slot", slot->source_span);
            }
        }
    }

    void verify_block(const BasicBlock& block, const CodeUnit& unit, const MFileUnit& mfile) {
        if (options_.require_terminated_blocks && !block.has_terminator()) {
            error("基本块必须以终结指令结束", block.source_span);
        }

        for (std::size_t index = 0; index < block.instructions.size(); ++index) {
            const auto& inst_ptr = block.instructions[index];
            if (inst_ptr == nullptr) {
                error("基本块中不能包含空指令", block.source_span);
                continue;
            }

            const Instruction& instruction = *inst_ptr;
            if (instruction.parent != &block) {
                error("指令 parent 必须指回所属基本块", instruction.source_span);
            }

            if (instruction.is_terminator() && index + 1U != block.instructions.size()) {
                error("终结指令只能出现在基本块末尾", instruction.source_span);
            }

            verify_instruction(instruction, block, unit, mfile);
        }
    }

    void verify_cfg_edges(const CodeUnit& unit) {
        for (const auto& block_ptr : unit.basic_blocks) {
            if (block_ptr == nullptr) {
                continue;
            }

            const BasicBlock& block = *block_ptr;
            for (BasicBlock* successor : block.successors) {
                if (!contains_block(unit, successor)) {
                    error("基本块 successor 必须属于同一个代码单元", block.source_span);
                    continue;
                }
                if (!has_predecessor(*successor, &block)) {
                    error("successor 的 predecessors 必须反向包含当前基本块", successor->source_span);
                }
            }

            for (BasicBlock* predecessor : block.predecessors) {
                if (!contains_block(unit, predecessor)) {
                    error("基本块 predecessor 必须属于同一个代码单元", block.source_span);
                    continue;
                }
                if (!has_successor(*predecessor, &block)) {
                    error("predecessor 的 successors 必须反向包含当前基本块", predecessor->source_span);
                }
            }

            std::vector<BasicBlock*> terminator_successors;
            if (const Instruction* terminator = block.terminator()) {
                switch (terminator->type()) {
                    case Instruction::Goto: {
                        const auto& inst = static_cast<const GotoInst&>(*terminator);
                        if (inst.target != nullptr) {
                            terminator_successors.push_back(inst.target);
                        }
                        break;
                    }
                    case Instruction::Branch: {
                        const auto& inst = static_cast<const BranchInst&>(*terminator);
                        if (inst.true_target != nullptr) {
                            terminator_successors.push_back(inst.true_target);
                        }
                        if (inst.false_target != nullptr &&
                            inst.false_target != inst.true_target) {
                            terminator_successors.push_back(inst.false_target);
                        }
                        break;
                    }
                    case Instruction::Return:
                        break;
                    default:
                        break;
                }
            }

            for (BasicBlock* successor : terminator_successors) {
                if (!has_successor(block, successor)) {
                    error("CFG successors 必须包含终结指令指向的目标", block.source_span);
                }
            }

            for (BasicBlock* successor : block.successors) {
                if (std::find(
                        terminator_successors.begin(),
                        terminator_successors.end(),
                        successor) == terminator_successors.end()) {
                    error("CFG successors 不能包含终结指令未声明的目标", block.source_span);
                }
            }
        }
    }

    void verify_instruction(
        const Instruction& instruction,
        const BasicBlock& block,
        const CodeUnit& unit,
        const MFileUnit& mfile) {
        switch (instruction.type()) {
            case Instruction::Const: {
                const auto& inst = static_cast<const ConstInst&>(instruction);
                define_value(inst.result, inst.source_span);
                break;
            }
            case Instruction::LoadSlot: {
                const auto& inst = static_cast<const LoadSlotInst&>(instruction);
                define_value(inst.result, inst.source_span);
                verify_slot_ref(inst.slot_id, "load_slot 引用了不存在的 slot", inst.source_span);
                break;
            }
            case Instruction::StoreSlot: {
                const auto& inst = static_cast<const StoreSlotInst&>(instruction);
                verify_slot_ref(inst.slot_id, "store_slot 引用了不存在的 slot", inst.source_span);
                verify_operand(inst.value, "store_slot value", inst.source_span);
                break;
            }
            case Instruction::LoadWorkspace: {
                const auto& inst = static_cast<const LoadWorkspaceInst&>(instruction);
                define_value(inst.result, inst.source_span);
                verify_workspace_handle(inst.workspace_handle_slot, inst.source_span);
                if (inst.symbol.empty()) {
                    error("load_workspace 的 symbol 不能为空", inst.source_span);
                }
                break;
            }
            case Instruction::StoreWorkspace: {
                const auto& inst = static_cast<const StoreWorkspaceInst&>(instruction);
                verify_workspace_handle(inst.workspace_handle_slot, inst.source_span);
                if (inst.symbol.empty()) {
                    error("store_workspace 的 symbol 不能为空", inst.source_span);
                }
                verify_operand(inst.value, "store_workspace value", inst.source_span);
                break;
            }
            case Instruction::Apply: {
                const auto& inst = static_cast<const ApplyInst&>(instruction);
                define_values(inst.results, "apply results", inst.source_span);
                verify_operand(inst.callee_or_base, "apply callee_or_base", inst.source_span);
                verify_operands(inst.arguments, "apply argument", inst.source_span);
                break;
            }
            case Instruction::Call: {
                const auto& inst = static_cast<const CallInst&>(instruction);
                define_values(inst.results, "call results", inst.source_span);
                verify_call(inst, unit, mfile);
                verify_operands(inst.arguments, "call argument", inst.source_span);
                break;
            }
            case Instruction::Copy: {
                const auto& inst = static_cast<const CopyInst&>(instruction);
                define_value(inst.result, inst.source_span);
                verify_operand(inst.value, "copy value", inst.source_span);
                break;
            }
            case Instruction::Unary: {
                const auto& inst = static_cast<const UnaryInst&>(instruction);
                define_value(inst.result, inst.source_span);
                verify_operand(inst.operand, "unary operand", inst.source_span);
                break;
            }
            case Instruction::Binary: {
                const auto& inst = static_cast<const BinaryInst&>(instruction);
                define_value(inst.result, inst.source_span);
                verify_operand(inst.lhs, "binary lhs", inst.source_span);
                verify_operand(inst.rhs, "binary rhs", inst.source_span);
                break;
            }
            case Instruction::Goto: {
                const auto& inst = static_cast<const GotoInst&>(instruction);
                verify_target(inst.target, unit, "goto target", inst.source_span);
                break;
            }
            case Instruction::Branch: {
                const auto& inst = static_cast<const BranchInst&>(instruction);
                verify_operand(inst.condition, "branch condition", inst.source_span);
                verify_target(inst.true_target, unit, "branch true_target", inst.source_span);
                verify_target(inst.false_target, unit, "branch false_target", inst.source_span);
                break;
            }
            case Instruction::Return: {
                const auto& inst = static_cast<const ReturnInst&>(instruction);
                verify_operands(inst.values, "return value", inst.source_span);
                if (!block.successors.empty()) {
                    error("return 所在基本块不能有 successors", inst.source_span);
                }
                break;
            }
        }
    }

    void define_value(ValueId value_id, SourceSpan source_span) {
        if (!value_id.is_valid()) {
            error("指令结果 ValueId 不能为空", source_span);
            return;
        }
        if (!defined_values_.insert(value_id).second) {
            error("同一个代码单元内 ValueId 只能被定义一次", source_span);
        }
    }

    void define_values(
        const std::vector<ValueId>& values,
        const char* label,
        SourceSpan source_span) {
        std::unordered_set<ValueId> local_seen;
        for (ValueId value_id : values) {
            if (!local_seen.insert(value_id).second) {
                error(std::string(label) + " 内部不能重复包含同一个 ValueId", source_span);
            }
            define_value(value_id, source_span);
        }
    }

    void verify_slot_ref(SlotId slot_id, const char* message, SourceSpan source_span) {
        if (current_unit_ == nullptr || !has_slot(*current_unit_, slot_id)) {
            error(message, source_span);
        }
    }

    void verify_workspace_handle(SlotId slot_id, SourceSpan source_span) {
        if (current_unit_ == nullptr) {
            error("workspace handle slot 缺少当前代码单元上下文", source_span);
            return;
        }

        const Slot* slot = current_unit_->slot_table.find_slot(slot_id);
        if (slot == nullptr) {
            error("workspace 指令引用了不存在的 workspace handle slot", source_span);
            return;
        }
        if (!slot->is_hidden() || slot->attrs.hidden_role != SlotAttrs::WorkspaceHandle) {
            error("workspace 指令必须引用 WorkspaceHandle hidden slot", source_span);
        }
    }

    void verify_operand(
        const Operand& operand,
        const char* label,
        SourceSpan source_span) {
        if (std::holds_alternative<ValueId>(operand)) {
            const ValueId value_id = std::get<ValueId>(operand);
            if (!has_value(value_id)) {
                error(std::string(label) + " 引用了未定义的 ValueId", source_span);
            }
            return;
        }

        if (std::holds_alternative<SlotId>(operand)) {
            const SlotId slot_id = std::get<SlotId>(operand);
            if (current_unit_ == nullptr || !has_slot(*current_unit_, slot_id)) {
                error(std::string(label) + " 引用了不存在的 SlotId", source_span);
            }
            return;
        }

        const InternedString& name = std::get<InternedString>(operand);
        if (name.empty()) {
            error(std::string(label) + " 的名字操作数不能为空", source_span);
        }
    }

    void verify_operands(
        const std::vector<Operand>& operands,
        const char* label,
        SourceSpan source_span) {
        for (const Operand& operand : operands) {
            verify_operand(operand, label, source_span);
        }
    }

    void verify_target(
        const BasicBlock* target,
        const CodeUnit& unit,
        const char* label,
        SourceSpan source_span) {
        if (target == nullptr) {
            error(std::string(label) + " 不能为空", source_span);
            return;
        }
        if (!contains_block(unit, target)) {
            error(std::string(label) + " 必须属于同一个代码单元", source_span);
        }
    }

    void verify_call(const CallInst& inst, const CodeUnit& unit, const MFileUnit& mfile) {
        switch (inst.callee_kind) {
            case CallInst::Direct:
                if (inst.local_target != nullptr) {
                    error("Direct call 的 local_target 必须为空", inst.source_span);
                }
                if (!std::holds_alternative<InternedString>(inst.callee) ||
                    std::get<InternedString>(inst.callee).empty()) {
                    error("Direct call 的 callee 必须是非空名字", inst.source_span);
                }
                break;
            case CallInst::Local:
                if (inst.local_target == nullptr) {
                    error("Local call 的 local_target 不能为空", inst.source_span);
                } else if (!contains_unit(mfile, inst.local_target)) {
                    error("Local call 的 local_target 必须属于同一个文件", inst.source_span);
                }
                if (!std::holds_alternative<InternedString>(inst.callee) ||
                    std::get<InternedString>(inst.callee).empty()) {
                    error("Local call 的 callee 必须是非空名字", inst.source_span);
                } else if (inst.local_target != nullptr &&
                           std::get<InternedString>(inst.callee) != inst.local_target->name) {
                    error("Local call 的 callee 名字必须与 local_target 一致", inst.source_span);
                }
                break;
            case CallInst::Indirect:
                if (inst.local_target != nullptr) {
                    error("Indirect call 的 local_target 必须为空", inst.source_span);
                }
                if (std::holds_alternative<InternedString>(inst.callee)) {
                    error("Indirect call 的 callee 不能是名字操作数", inst.source_span);
                } else {
                    verify_operand(inst.callee, "indirect call callee", inst.source_span);
                }
                break;
            default:
                error("CallInst 使用了未知的 callee_kind", inst.source_span);
                break;
        }

        (void)unit;
    }

    IRVerifyOptions options_;
    IRVerifyResult result_;
    const CodeUnit* current_unit_ = nullptr;
    std::unordered_set<ValueId> defined_values_;
};

} // namespace

bool IRVerifyResult::ok() const noexcept {
    return std::none_of(
        diagnostics.begin(),
        diagnostics.end(),
        [](const IRVerifyDiagnostic& diagnostic) {
            return diagnostic.severity == IRVerifyDiagnostic::Error;
        });
}

IRVerifyResult verify_ir(const MFileUnit& mfile, const IRVerifyOptions& options) {
    IRVerifier verifier(options);
    return verifier.verify(mfile);
}

} // namespace baltam
