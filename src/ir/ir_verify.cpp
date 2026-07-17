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

    [[nodiscard]] IRVerifyResult verify(const IRModule& module) {
        result_ = {};
        verify_module(module);
        return std::move(result_);
    }

    [[nodiscard]] IRVerifyResult verify(const MFileUnit& mfile) {
        result_ = {};
        if (mfile.module != nullptr) {
            verify_module(*mfile.module);
        } else {
            verify_mfile(mfile);
        }
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

    [[nodiscard]] bool contains_non_anonymous_unit(
        const MFileUnit& mfile,
        const CodeUnit* unit) const {
        return unit != nullptr &&
            std::any_of(
                mfile.code_units.begin(),
                mfile.code_units.end(),
                [unit](const std::unique_ptr<CodeUnit>& candidate) {
                    return candidate.get() == unit;
                });
    }

    [[nodiscard]] bool contains_anonymous_unit(
        const IRModule& module,
        const CodeUnit* unit) const {
        return unit != nullptr &&
            std::any_of(
                module.anonymous_functions.functions.begin(),
                module.anonymous_functions.functions.end(),
                [unit](const std::unique_ptr<AnonymousFunctionUnit>& candidate) {
                    return candidate.get() == unit;
                });
    }

    [[nodiscard]] bool contains_module_unit(
        const IRModule& module,
        const CodeUnit* unit) const {
        if (unit == nullptr) {
            return false;
        }

        for (const auto& file_ptr : module.files) {
            if (file_ptr != nullptr && contains_non_anonymous_unit(*file_ptr, unit)) {
                return true;
            }
        }

        return contains_anonymous_unit(module, unit);
    }

    [[nodiscard]] bool contains_unit(
        const IRModule& module,
        const MFileUnit& mfile,
        const CodeUnit* unit) const {
        return contains_non_anonymous_unit(mfile, unit) ||
            contains_anonymous_unit(module, unit);
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

    [[nodiscard]] bool has_slot(const CodeUnit& unit, Slot slot) const {
        return slot.is_valid() && unit.slot_table.find_slot(slot) != nullptr;
    }

    [[nodiscard]] bool has_value(ValueId value_id) const {
        return value_id.is_valid() && defined_values_.find(value_id) != defined_values_.end();
    }

    [[nodiscard]] bool has_anonymous_function(
        const IRModule& module,
        AnonymousFunctionId function_id) const {
        return module.anonymous_functions.find(function_id) != nullptr;
    }

    [[nodiscard]] const IRModule* module_for(const CodeUnit& unit) const {
        const MFileUnit* file = source_file_for(unit);
        return file != nullptr ? file->module : nullptr;
    }

    void verify_module(const IRModule& module) {
        if (module.files.empty()) {
            error("IR module 至少应包含一个文件单元");
        }

        for (const auto& file_ptr : module.files) {
            if (file_ptr == nullptr) {
                error("IR module 不能包含空文件单元");
                continue;
            }
            if (file_ptr->module != &module) {
                error("文件单元 module 必须指回所属 IR module");
            }
            verify_mfile(*file_ptr);
        }

        std::unordered_set<AnonymousFunctionId> seen_anonymous_functions;
        for (const auto& function_ptr : module.anonymous_functions.functions) {
            if (function_ptr == nullptr) {
                error("匿名函数表不能包含空函数体");
                continue;
            }

            if (function_ptr->lexical_parent == nullptr) {
                error("匿名函数体 lexical_parent 不能为空", function_ptr->source_span);
            } else if (module_for(*function_ptr->lexical_parent) != &module) {
                error("匿名函数体 lexical_parent 必须属于当前 IR module", function_ptr->source_span);
            } else if (!contains_module_unit(module, function_ptr->lexical_parent)) {
                error("匿名函数体 lexical_parent 必须由当前 IR module 拥有", function_ptr->source_span);
            }
            if (!function_ptr->id.is_valid()) {
                error("匿名函数体 ID 不能为空", function_ptr->source_span);
            } else if (!seen_anonymous_functions.insert(function_ptr->id).second) {
                error("匿名函数体 ID 不能重复", function_ptr->source_span);
            }
        }

        for (const auto& function_ptr : module.anonymous_functions.functions) {
            if (function_ptr != nullptr) {
                const MFileUnit* source_file = function_ptr->lexical_parent != nullptr
                    ? source_file_for(*function_ptr->lexical_parent)
                    : nullptr;
                verify_code_unit(*function_ptr, source_file);
            }
        }
    }

    void verify_mfile(const MFileUnit& mfile) {
        const IRModule* module = mfile.module;
        if (mfile.entry_unit == nullptr) {
            error("文件入口代码单元不能为空");
        } else if (!contains_non_anonymous_unit(mfile, mfile.entry_unit)) {
            error("文件入口代码单元必须属于当前文件");
        }

        for (const auto& unit_ptr : mfile.code_units) {
            if (unit_ptr == nullptr) {
                error("文件中不能包含空代码单元");
                continue;
            }

            if (unit_ptr->is_script()) {
                const auto* script = static_cast<const ScriptUnit*>(unit_ptr.get());
                if (script->file != &mfile) {
                    error("脚本代码单元 file 必须指回所属文件", unit_ptr->source_span);
                }
            } else if (unit_ptr->is_function()) {
                const auto* function = static_cast<const FunctionUnit*>(unit_ptr.get());
                if (function->file != &mfile) {
                    error("函数代码单元 file 必须指回所属文件", unit_ptr->source_span);
                }
            }
        }

        for (const auto& [name, function] : mfile.local_function_map) {
            if (function == nullptr) {
                error("local_function_map 不能包含空函数目标");
                continue;
            }
            if (module != nullptr && !contains_unit(*module, mfile, function)) {
                error("local_function_map 的函数目标必须属于当前文件", function->source_span);
            } else if (module == nullptr && !contains_non_anonymous_unit(mfile, function)) {
                error("local_function_map 的函数目标必须属于当前文件", function->source_span);
            }
            if (function->name != name) {
                error("local_function_map 的名字必须与目标函数名一致", function->source_span);
            }
        }

        for (const auto& unit_ptr : mfile.code_units) {
            if (unit_ptr != nullptr) {
                verify_code_unit(*unit_ptr, &mfile);
            }
        }
    }

    [[nodiscard]] const MFileUnit* source_file_for(const CodeUnit& unit) const {
        if (unit.is_script()) {
            return static_cast<const ScriptUnit&>(unit).file;
        }
        if (unit.is_function()) {
            return static_cast<const FunctionUnit&>(unit).file;
        }
        if (unit.is_anonymous_function()) {
            const auto& function = static_cast<const AnonymousFunctionUnit&>(unit);
            return function.lexical_parent != nullptr
                ? source_file_for(*function.lexical_parent)
                : nullptr;
        }
        return nullptr;
    }

    void verify_code_unit(const CodeUnit& unit, const MFileUnit* mfile) {
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
        } else if (unit.type() == CodeUnit::AnonymousFunction) {
            if (dynamic_cast<const AnonymousFunctionUnit*>(&unit) == nullptr) {
                error("type() 为 AnonymousFunction 的代码单元必须是 AnonymousFunctionUnit",
                      unit.source_span);
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
        } else if (unit.is_anonymous_function()) {
            verify_anonymous_function_unit(static_cast<const AnonymousFunctionUnit&>(unit));
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
        std::unordered_map<SlotTag, SlotId> unique_tags;

        for (std::size_t index = 0; index < unit.slot_table.slots.size(); ++index) {
            const SlotInfo& slot = unit.slot_table.slots[index];
            if (!slot.slot.is_valid()) {
                error("slot 不能为空", slot.source_span);
                continue;
            }
            if (!seen_slots.insert(slot.slot.id).second) {
                error("同一个代码单元内 slot_id 不能重复", slot.source_span);
            }
            if (options_.require_dense_slot_ids && slot.slot.id.value() != index) {
                warning("slot_id 不符合当前 builder 的递增分配顺序", slot.source_span);
            }

            if (slot.slot.tag == SlotTag::ScriptVar && !unit.is_script()) {
                error("ScriptVar slot 只能出现在脚本代码单元中", slot.source_span);
            }

            if ((slot.slot.tag == SlotTag::Nargin ||
                 slot.slot.tag == SlotTag::Nargout ||
                 slot.slot.tag == SlotTag::Varargin ||
                 slot.slot.tag == SlotTag::Varargout) &&
                !unit.is_function()) {
                error("函数调用约定 slot 只能出现在函数代码单元中", slot.source_span);
            }

            if (slot.slot.tag == SlotTag::Capture && !unit.is_anonymous_function()) {
                error("Capture slot 只能出现在匿名函数体单元中", slot.source_span);
            }

            if (slot.slot.tag == SlotTag::Nargin ||
                slot.slot.tag == SlotTag::Nargout ||
                slot.slot.tag == SlotTag::Varargin ||
                slot.slot.tag == SlotTag::Varargout) {
                const auto [it, inserted] = unique_tags.emplace(slot.slot.tag, slot.slot.id);
                if (!inserted) {
                    error("同一个代码单元中同一 ABI slot tag 至多出现一次", slot.source_span);
                }
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
            case Instruction::CreateNamedFunctionHandle:
                return static_cast<const CreateNamedFunctionHandleInst&>(instruction).result == value_id && result_index == 0;
            case Instruction::CreateAnonymousFunctionHandle:
                return static_cast<const CreateAnonymousFunctionHandleInst&>(instruction).result == value_id && result_index == 0;
            case Instruction::Unary:
                return static_cast<const UnaryInst&>(instruction).result == value_id && result_index == 0;
            case Instruction::Binary:
                return static_cast<const BinaryInst&>(instruction).result == value_id && result_index == 0;
            case Instruction::Apply: {
                const auto& inst = static_cast<const ApplyInst&>(instruction);
                return result_index < inst.results.size() && inst.results[result_index] == value_id;
            }
            case Instruction::ValueApply: {
                const auto& inst = static_cast<const ValueApplyInst&>(instruction);
                return result_index < inst.results.size() && inst.results[result_index] == value_id;
            }
            case Instruction::MagicEnd:
                return static_cast<const MagicEndInst&>(instruction).result == value_id && result_index == 0;
            case Instruction::Call: {
                const auto& inst = static_cast<const CallInst&>(instruction);
                return result_index < inst.results.size() && inst.results[result_index] == value_id;
            }
            case Instruction::StoreSlot:
            case Instruction::GlobalDecl:
            case Instruction::PersistentDecl:
            case Instruction::Goto:
            case Instruction::Branch:
            case Instruction::Return:
                return false;
        }
        return false;
    }

    void verify_function_unit(const FunctionUnit& function) {
        std::unordered_set<SlotId> seen_params;
        for (Slot slot : function.param_slots) {
            const SlotInfo* info = function.slot_table.find_slot(slot);
            if (info == nullptr) {
                error("函数参数列表引用了不存在的 slot", function.source_span);
                continue;
            }
            if (info->slot.tag != SlotTag::Arg) {
                error("函数参数列表只能引用 Arg slot", info->source_span);
            }
            if (!seen_params.insert(slot.id).second) {
                error("函数参数列表不能重复引用同一个 slot", info->source_span);
            }
        }

        std::unordered_set<SlotId> seen_returns;
        for (Slot slot : function.return_slots) {
            const SlotInfo* info = function.slot_table.find_slot(slot);
            if (info == nullptr) {
                error("函数返回值列表引用了不存在的 slot", function.source_span);
                continue;
            }
            if (info->slot.tag != SlotTag::Ret) {
                error("函数返回值列表只能引用 Ret slot", info->source_span);
            }
            if (!seen_returns.insert(slot.id).second) {
                error("函数返回值列表不能重复引用同一个 slot", info->source_span);
            }
        }
    }

    void verify_anonymous_function_unit(const AnonymousFunctionUnit& function) {
        if (!function.id.is_valid()) {
            error("匿名函数体 ID 不能为空", function.source_span);
        }

        std::unordered_set<SlotId> seen_params;
        for (Slot slot : function.param_slots) {
            const SlotInfo* info = function.slot_table.find_slot(slot);
            if (info == nullptr) {
                error("匿名函数参数列表引用了不存在的 slot", function.source_span);
                continue;
            }
            if (info->slot.tag != SlotTag::Arg) {
                error("匿名函数参数列表只能引用 Arg slot", info->source_span);
            }
            if (!seen_params.insert(slot.id).second) {
                error("匿名函数参数列表不能重复引用同一个 slot", info->source_span);
            }
        }

        std::unordered_set<SlotId> seen_captures;
        for (Slot slot : function.capture_slots) {
            const SlotInfo* info = function.slot_table.find_slot(slot);
            if (info == nullptr) {
                error("匿名函数捕获列表引用了不存在的 slot", function.source_span);
                continue;
            }
            if (info->slot.tag != SlotTag::Capture) {
                error("匿名函数捕获列表只能引用 Capture slot", info->source_span);
            }
            if (!seen_captures.insert(slot.id).second) {
                error("匿名函数捕获列表不能重复引用同一个 slot", info->source_span);
            }
        }
    }

    void verify_block(const BasicBlock& block, const CodeUnit& unit, const MFileUnit* mfile) {
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
        const MFileUnit* mfile) {
        switch (instruction.type()) {
            case Instruction::Const: {
                const auto& inst = static_cast<const ConstInst&>(instruction);
                define_value(inst.result, inst.source_span);
                break;
            }
            case Instruction::LoadSlot: {
                const auto& inst = static_cast<const LoadSlotInst&>(instruction);
                define_value(inst.result, inst.source_span);
                verify_slot_ref(inst.slot, "load_slot 引用了不存在的 slot", inst.source_span);
                break;
            }
            case Instruction::StoreSlot: {
                const auto& inst = static_cast<const StoreSlotInst&>(instruction);
                verify_slot_ref(inst.slot, "store_slot 引用了不存在的 slot", inst.source_span);
                if (current_unit_ != nullptr) {
                    const SlotInfo* slot = current_unit_->slot_table.find_slot(inst.slot);
                    if (slot != nullptr && slot->slot.tag == SlotTag::Capture) {
                        error("匿名函数 capture slot 是只读的，不能 store_slot", inst.source_span);
                    }
                }
                verify_value_ref(inst.value, "store_slot value", inst.source_span);
                break;
            }
            case Instruction::GlobalDecl: {
                const auto& inst = static_cast<const GlobalDeclInst&>(instruction);
                verify_global_decl(inst);
                break;
            }
            case Instruction::PersistentDecl: {
                const auto& inst = static_cast<const PersistentDeclInst&>(instruction);
                verify_persistent_decl(inst);
                break;
            }
            case Instruction::CreateNamedFunctionHandle: {
                const auto& inst = static_cast<const CreateNamedFunctionHandleInst&>(instruction);
                define_value(inst.result, inst.source_span);
                verify_create_named_function_handle(inst, mfile);
                break;
            }
            case Instruction::CreateAnonymousFunctionHandle: {
                const auto& inst =
                    static_cast<const CreateAnonymousFunctionHandleInst&>(instruction);
                define_value(inst.result, inst.source_span);
                verify_create_anonymous_function_handle(inst, unit, mfile);
                break;
            }
            case Instruction::Apply: {
                const auto& inst = static_cast<const ApplyInst&>(instruction);
                define_values(inst.results, "apply results", inst.source_span);
                verify_operand(inst.callee_or_base, "apply callee_or_base", inst.source_span);
                verify_operands(inst.arguments, "apply argument", inst.source_span);
                break;
            }
            case Instruction::ValueApply: {
                const auto& inst = static_cast<const ValueApplyInst&>(instruction);
                define_values(inst.results, "value_apply results", inst.source_span);
                verify_value_ref(inst.base, "value_apply base", inst.source_span);
                verify_operands(inst.arguments, "value_apply argument", inst.source_span);
                break;
            }
            case Instruction::MagicEnd: {
                const auto& inst = static_cast<const MagicEndInst&>(instruction);
                define_value(inst.result, inst.source_span);
                verify_magic_end(inst);
                break;
            }
            case Instruction::Call: {
                const auto& inst = static_cast<const CallInst&>(instruction);
                define_values(inst.results, "call results", inst.source_span);
                verify_call(inst, unit, mfile);
                verify_operands(inst.arguments, "call argument", inst.source_span);
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
                for (ValueId value : inst.values) {
                    verify_value_ref(value, "return value", inst.source_span);
                }
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
            if (!value_id.is_valid()) {
                continue;
            }
            if (!local_seen.insert(value_id).second) {
                error(std::string(label) + " 内部不能重复包含同一个 ValueId", source_span);
            }
            define_value(value_id, source_span);
        }
    }

    void verify_slot_ref(Slot slot, const char* message, SourceSpan source_span) {
        if (current_unit_ == nullptr || !has_slot(*current_unit_, slot)) {
            error(message, source_span);
        }
    }

    void verify_value_ref(
        ValueId value_id,
        const char* label,
        SourceSpan source_span) {
        if (!has_value(value_id)) {
            error(std::string(label) + " 引用了未定义的 ValueId", source_span);
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

        if (std::holds_alternative<Slot>(operand)) {
            const Slot slot = std::get<Slot>(operand);
            if (current_unit_ == nullptr || !has_slot(*current_unit_, slot)) {
                error(std::string(label) + " 引用了不存在的 Slot", source_span);
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

    void verify_call(const CallInst& inst, const CodeUnit& unit, const MFileUnit* mfile) {
        if (inst.dispatch_type == Internal && inst.callee_kind != CallInst::Direct) {
            error("Internal call 必须使用 Direct callee kind", inst.source_span);
        }
        if (inst.dispatch_type == MFunction) {
            if (inst.m_function_target == nullptr) {
                error("MFunction call 必须记录 m_function_target", inst.source_span);
            } else if (mfile == nullptr) {
                error("MFunction call 需要文件上下文", inst.source_span);
            } else if (!contains_non_anonymous_unit(*mfile, inst.m_function_target)) {
                error("MFunction call 的 m_function_target 必须属于同一个文件", inst.source_span);
            }
        } else if (inst.m_function_target != nullptr) {
            error("非 MFunction call 的 m_function_target 必须为空", inst.source_span);
        }

        switch (inst.callee_kind) {
            case CallInst::Direct:
                if (!std::holds_alternative<InternedString>(inst.callee) ||
                    std::get<InternedString>(inst.callee).empty()) {
                    error("Direct call 的 callee 必须是非空名字", inst.source_span);
                }
                if (inst.dispatch_type == MFunction &&
                    inst.m_function_target != nullptr &&
                    std::get<InternedString>(inst.callee) != inst.m_function_target->name) {
                    error("MFunction call 的 callee 名字必须与 m_function_target 一致", inst.source_span);
                }
                break;
            case CallInst::Indirect:
                if (inst.dispatch_type == MFunction || inst.dispatch_type == Internal) {
                    error("Indirect call 不能使用静态 MFunction/Internal 分派", inst.source_span);
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

    void verify_magic_end_base(
        const Operand& base,
        const char* label,
        SourceSpan source_span) {
        if (std::holds_alternative<ValueId>(base)) {
            const ValueId value_id = std::get<ValueId>(base);
            if (!has_value(value_id)) {
                error(std::string(label) + " 引用了未定义的 ValueId", source_span);
            }
            return;
        }

        if (std::holds_alternative<Slot>(base)) {
            const Slot slot = std::get<Slot>(base);
            if (current_unit_ == nullptr || !has_slot(*current_unit_, slot)) {
                error(std::string(label) + " 引用了不存在的 Slot", source_span);
            }
            return;
        }

        const InternedString& name = std::get<InternedString>(base);
        if (name.empty()) {
            error(std::string(label) + " 的名字操作数不能为空", source_span);
        }
    }

    void verify_magic_end(const MagicEndInst& inst) {
        if (inst.candidate_contexts.empty()) {
            error("MagicEndInst 必须至少保留一个候选索引上下文", inst.source_span);
            return;
        }

        for (const MagicEndInst::MagicEndContext& context : inst.candidate_contexts) {
            verify_magic_end_base(context.callee_or_base, "magic_end context", inst.source_span);
            if (context.dim == 0U) {
                error("magic_end context 的 dim 必须大于 0", inst.source_span);
            }
            if (context.nindices == 0U) {
                error("magic_end context 的 nindices 必须大于 0", inst.source_span);
            }
        }
    }

    void verify_global_decl(const GlobalDeclInst& inst) {
        verify_decl_slots(
            inst.slots,
            SlotTag::Global,
            "GlobalDeclInst",
            "Global",
            inst.source_span);
    }

    void verify_persistent_decl(const PersistentDeclInst& inst) {
        verify_decl_slots(
            inst.slots,
            SlotTag::Persistent,
            "PersistentDeclInst",
            "Persistent",
            inst.source_span);
    }

    void verify_decl_slots(
        const std::vector<Slot>& slots,
        SlotTag expected_tag,
        const char* instruction_name,
        const char* expected_tag_name,
        SourceSpan source_span) {
        if (slots.empty()) {
            error(std::string(instruction_name) + " 必须至少声明一个 slot", source_span);
            return;
        }

        std::unordered_set<SlotId> seen_slots;
        for (Slot slot : slots) {
            if (!slot.is_valid()) {
                error(std::string(instruction_name) + " 不能引用无效 slot", source_span);
                continue;
            }
            if (!seen_slots.insert(slot.id).second) {
                error(std::string(instruction_name) + " 不能重复声明同一个 slot", source_span);
            }
            if (current_unit_ == nullptr || !has_slot(*current_unit_, slot)) {
                error(std::string(instruction_name) + " 引用了不存在的 slot", source_span);
                continue;
            }

            const SlotInfo* info = current_unit_->slot_table.find_slot(slot);
            if (info != nullptr && info->slot.tag != expected_tag) {
                error(
                    std::string(instruction_name) + " 只能引用 " +
                        expected_tag_name + " slot",
                    source_span);
            }
        }
    }

    void verify_create_named_function_handle(
        const CreateNamedFunctionHandleInst& inst,
        const MFileUnit* mfile) {
        if (inst.name.empty()) {
            error("CreateNamedFunctionHandleInst 的 name 不能为空", inst.source_span);
        }

        switch (inst.resolution_mode) {
            case CreateNamedFunctionHandleInst::Runtime:
                if (inst.bound_dispatch_type != Dynamic) {
                    error("runtime function handle 不能携带静态 dispatch type", inst.source_span);
                }
                if (inst.m_function_target != nullptr) {
                    error("runtime function handle 的 m_function_target 必须为空", inst.source_span);
                }
                break;
            case CreateNamedFunctionHandleInst::Static:
                if (inst.bound_dispatch_type == Builtin) {
                    if (inst.m_function_target != nullptr) {
                        error("static builtin function handle 的 m_function_target 必须为空",
                              inst.source_span);
                    }
                } else if (inst.bound_dispatch_type == MFunction) {
                    if (inst.m_function_target == nullptr) {
                        error("static mfunc function handle 必须记录 m_function_target",
                              inst.source_span);
                    } else if (mfile == nullptr) {
                        error("static mfunc function handle 需要文件上下文",
                              inst.source_span);
                    } else if (!contains_non_anonymous_unit(*mfile, inst.m_function_target)) {
                        error("static mfunc function handle 的 target 必须属于同一个文件",
                              inst.source_span);
                    }
                } else {
                    error("static function handle 只能绑定到 Builtin 或 MFunction",
                          inst.source_span);
                }
                break;
            default:
                error("CreateNamedFunctionHandleInst 使用了未知的 resolution_mode",
                      inst.source_span);
                break;
        }
    }

    void verify_create_anonymous_function_handle(
        const CreateAnonymousFunctionHandleInst& inst,
        const CodeUnit& unit,
        const MFileUnit* mfile) {
        (void)mfile;
        if (!inst.function_id.is_valid()) {
            error("create_anon_func 的 function_id 不能为空", inst.source_span);
        } else {
            const IRModule* module = module_for(unit);
            if (module == nullptr) {
                error("create_anon_func 需要所属 IR module", inst.source_span);
            } else if (!has_anonymous_function(*module, inst.function_id)) {
                error("create_anon_func 的 function_id 必须能在匿名函数表中解析",
                      inst.source_span);
            }
        }

        std::unordered_set<InternedString> seen_names;
        for (const auto& capture : inst.captures) {
            if (capture.name.empty()) {
                error("create_anon_func 的 capture name 不能为空", inst.source_span);
            } else if (!seen_names.insert(capture.name).second) {
                error("create_anon_func 不能重复捕获同名变量", inst.source_span);
            }

            if (!capture.source_slot.is_valid()) {
                error("create_anon_func 的 source_slot 不能为空", inst.source_span);
            } else if (!has_slot(unit, capture.source_slot)) {
                error("create_anon_func 的 source_slot 必须属于当前代码单元",
                      inst.source_span);
            }
            verify_value_ref(
                capture.captured_value,
                "create_anon_func capture value",
                inst.source_span);
        }
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

IRVerifyResult verify_ir(const IRModule& module, const IRVerifyOptions& options) {
    IRVerifier verifier(options);
    return verifier.verify(module);
}

IRVerifyResult verify_ir(const MFileUnit& mfile, const IRVerifyOptions& options) {
    IRVerifier verifier(options);
    return verifier.verify(mfile);
}

} // namespace baltam
