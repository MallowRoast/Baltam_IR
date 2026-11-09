#include "Transform/mem2reg.h"

#include <algorithm>
#include <functional>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace baltam {
namespace {

// 只折叠有确定替代值的平凡 PHI，保留 Undefined 和没有外部来源的循环。
void simplify_phis(IRStageVersion& version) {
    bool changed;
    do {
        changed = false;
        for (const auto& block : version.blocks()) {
            for (std::size_t i = 0; i < block->instructions().size();) {
                auto& instruction = *block->instructions()[i];
                if (instruction.type() != Instruction::Type::Phi)
                    break;
                const auto& phi = static_cast<const PhiInst&>(instruction);
                std::optional<ValueId> replacement;
                bool different = false;
                for (std::size_t n = 0; n < phi.operand_count(); ++n) {
                    const auto value = phi.incoming(n).value;
                    if (!value) {
                        different = true;
                        break;
                    }
                    if (*value == phi.result)
                        continue;
                    if (replacement && replacement != value) {
                        different = true;
                        break;
                    }
                    replacement = value;
                }
                if (replacement && !different) {
                    version.replace_all_uses(phi.result, *replacement);
                    block->erase_instruction(i);
                    changed = true;
                } else {
                    ++i;
                }
            }
        }
    } while (changed);
}

// 已有 SSA 来源的读取直接转发；Undefined 在普通指令实际取值时检查。
void remove_forwarded_loads(IRStageVersion& version) {
    for (const auto& block : version.blocks()) {
        for (std::size_t i = 0; i < block->instructions().size();) {
            const auto& instruction = *block->instructions()[i];
            if (instruction.type() == Instruction::Type::LoadSlot) {
                const auto& load = static_cast<const LoadSlotInst&>(instruction);
                if (load.has_source()) {
                    // 显式 Undefined 没有可替代的 ValueId，保留该读取。
                    if (const auto value = load.source()) {
                        version.replace_all_uses(load.result, *value);
                        block->erase_instruction(i);
                        continue;
                    }
                }
            }
            ++i;
        }
    }
}

} // 匿名命名空间

PreservedAnalyses Mem2RegPass::run(FunctionUnit& function, FunctionAnalysisManager& analyses) {
    if (function.current_stage() != IRStage::NonSSA)
        return PreservedAnalyses::all();
    // 自动捕获分析尚未接入，不能仅凭当前已登记的共享关系放开嵌套函数。
    if (!function.nested_functions().empty() || function.lexical_parent())
        return PreservedAnalyses::all();
    const auto source = function.current_version();
    const auto& cfg = analyses.cfg();
    if (!cfg.entry())
        return PreservedAnalyses::all();
    const auto entry = cfg.index(cfg.entry());
    std::vector<bool> reachable(cfg.size(), false);
    std::vector<std::size_t> pending{entry};
    reachable[entry] = true;
    while (!pending.empty()) {
        const auto block = pending.back();
        pending.pop_back();
        for (const auto successor : cfg.successors(block))
            if (!reachable[successor]) {
                reachable[successor] = true;
                pending.push_back(successor);
            }
    }
    const auto slot_count = function.slot_table().size();
    std::vector<bool> eligible(slot_count, true), used(slot_count, false);
    for (std::size_t i = 0; i < slot_count; ++i) {
        const auto& info = function.slot_table().find(SlotId{static_cast<std::uint32_t>(i)});
        eligible[i] = !info.has_global_decl && !info.has_persistent_decl &&
                      !info.shared_by_nested && !info.shared_target;
    }
    for (const auto slot : function.parameter_slots())
        used[slot.index] = true;
    for (const auto slot : function.return_slots())
        used[slot.index] = true;
    for (std::size_t b = 0; b < cfg.size(); ++b) {
        const auto& instructions = cfg.block(b)->instructions();
        for (std::size_t i = 0; i < instructions.size(); ++i) {
            const auto& instruction = *instructions[i];
            const auto type = instruction.type();
            if (type == Instruction::Type::GlobalDecl ||
                type == Instruction::Type::PersistentDecl ||
                type == Instruction::Type::WorkspaceBarrier)
                continue;
            if (type == Instruction::Type::LoadSlot || type == Instruction::Type::StoreSlot) {
                const auto slot = type == Instruction::Type::LoadSlot
                                      ? static_cast<const LoadSlotInst&>(instruction).slot
                                      : static_cast<const StoreSlotInst&>(instruction).slot;
                used[slot.index] = true;
                if (type == Instruction::Type::LoadSlot &&
                    static_cast<const LoadSlotInst&>(instruction).has_source())
                    eligible[slot.index] = false;
                if (!reachable[b])
                    eligible[slot.index] = false;
                if (type == Instruction::Type::StoreSlot &&
                    source->values()
                            .find(static_cast<const StoreSlotInst&>(instruction).value())
                            .form() != Value::Form::Single)
                    eligible[slot.index] = false;
            } else if (type == Instruction::Type::Record) {
                // 已有绑定记录的槽位不再按普通 Non-SSA 存储提升。
                eligible[static_cast<const RecordInst&>(instruction).slot.index] = false;
            } else if (instruction.effect() == Effect::MayWriteSlot) {
                // 常规重载按不修改当前工作区的假设执行；实际修改交由运行时回退。
                if (type == Instruction::Type::ParenReference ||
                    (type == Instruction::Type::Intrinsic &&
                     static_cast<const IntrinsicInst&>(instruction).id == IntrinsicId::ForeachInit))
                    continue;
                // 显式工作区操作仍按后继屏障保护，不按函数名猜测内置语义。
                if (i + 1 == instructions.size() ||
                    instructions[i + 1]->type() != Instruction::Type::WorkspaceBarrier)
                    return PreservedAnalyses::all();
            }
        }
    }
    for (std::size_t slot = 0; slot < slot_count; ++slot)
        eligible[slot] = eligible[slot] && used[slot];

    // 克隆完成后再建立来源映射，原始 Non-SSA 在整个转换中保持不变。
    auto version = source->clone(function);
    version->initialize_deopt(source.get());
    version->set_return_count(function.return_slots().size());
    std::vector<ValueId> arguments;
    auto& entry_block = *version->blocks()[entry];
    for (std::size_t i = 0; i < function.parameter_slots().size(); ++i) {
        arguments.push_back(version->add_parameter(static_cast<std::uint32_t>(i)));
    }
    // 不能提升的参数仍建立真实存储，后续声明按原顺序改变绑定。
    std::size_t parameter_store = 0;
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        const auto slot = function.parameter_slots()[i];
        if (!eligible[slot.index])
            entry_block.insert_instruction<StoreSlotInst>(parameter_store++, slot, arguments[i]);
        else if (!function.slot_table().find(slot).is_internal)
            entry_block.insert_instruction<RecordInst>(parameter_store++, slot, arguments[i]);
    }
    // 在每个返回位置建立结果读取，随后与普通 load 一同重命名。
    for (const auto& block : version->blocks()) {
        for (std::size_t i = 0; i < block->instructions().size(); ++i) {
            if (block->instructions()[i]->type() != Instruction::Type::Return)
                continue;
            auto& ret = static_cast<ReturnInst&>(*block->instructions()[i]);
            std::vector<ValueId> results;
            for (const auto slot : function.return_slots()) {
                auto& load = block->insert_instruction<LoadSlotInst>(i++, ValueId{0}, slot);
                load.result = version->add_value(load);
                results.push_back(load.result);
            }
            ret.set_values(results);
        }
    }
    // 固定原指令序列，查询期间插入的 PHI 和辅助读取不改变查找边界。
    std::vector<std::vector<Instruction*>> originals(cfg.size());
    for (std::size_t b = 0; b < cfg.size(); ++b)
        for (const auto& instruction : version->blocks()[b]->instructions())
            originals[b].push_back(instruction.get());
    using Cache = std::unordered_map<std::uint32_t, std::optional<ValueId>>;
    std::vector<Cache> incoming(cfg.size()), outgoing(cfg.size());
    std::vector<std::optional<ValueId>> initial(slot_count);
    for (std::size_t i = 0; i < arguments.size(); ++i)
        initial[function.parameter_slots()[i].index] = arguments[i];
    struct Definition {
        std::optional<ValueId> value;
        bool workspace = false;
    };
    std::function<std::optional<ValueId>(std::size_t, SlotId)> read_entry, read_exit;
    const auto find_definition = [&originals, &read_entry, &function](
                                     std::size_t b, SlotId slot, std::size_t before) -> Definition {
        while (before) {
            const auto* instruction = originals[b][--before];
            if (instruction->type() == Instruction::Type::StoreSlot &&
                static_cast<const StoreSlotInst&>(*instruction).slot == slot)
                return {static_cast<const StoreSlotInst&>(*instruction).value(), false};
            if (instruction->type() == Instruction::Type::LoadSlot &&
                static_cast<const LoadSlotInst&>(*instruction).slot == slot)
                return {static_cast<const LoadSlotInst&>(*instruction).result, false};
            // 工作区操作无法访问内部临时槽位，其定义可以跨过屏障。
            if (instruction->type() == Instruction::Type::WorkspaceBarrier &&
                !function.slot_table().find(slot).is_internal)
                return {std::nullopt, true};
        }
        return {read_entry(b, slot), false};
    };
    read_entry = [&incoming, &initial, &read_exit, &cfg, &reachable, &version,
                  entry](std::size_t b, SlotId slot) -> std::optional<ValueId> {
        const auto cached = incoming[b].find(slot.index);
        if (cached != incoming[b].end())
            return cached->second;
        if (b == entry)
            return initial[slot.index];
        auto& phi = version->blocks()[b]->insert_instruction<PhiInst>(0, ValueId{0});
        phi.result = version->add_value(phi);
        // 提升固定类型的内部槽位时，入口 PHI 继承相同的类型与形状。
        const auto& info = version->blocks()[b]->parent()->slot_table().find(slot);
        auto& value = version->values().find(phi.result);
        value.set_type(info.type);
        value.set_shape(info.shape);
        // 先登记占位再查询前驱，单前驱循环同样不会无限递归。
        incoming[b].emplace(slot.index, phi.result);
        for (const auto predecessor : cfg.predecessors(b))
            phi.add_incoming(version->blocks()[predecessor].get(),
                             reachable[predecessor] ? read_exit(predecessor, slot) : std::nullopt);
        return phi.result;
    };
    read_exit = [&outgoing, &find_definition, &originals,
                 &version](std::size_t b, SlotId slot) -> std::optional<ValueId> {
        const auto cached = outgoing[b].find(slot.index);
        if (cached != outgoing[b].end())
            return cached->second;
        auto definition = find_definition(b, slot, originals[b].size());
        if (definition.workspace) {
            auto& block = *version->blocks()[b];
            auto& load = block.insert_instruction<LoadSlotInst>(block.instructions().size() - 1,
                                                                ValueId{0}, slot);
            load.result = version->add_value(load);
            definition.value = load.result;
        }
        outgoing[b].emplace(slot.index, definition.value);
        return definition.value;
    };
    for (std::size_t b = 0; b < cfg.size(); ++b) {
        if (!reachable[b])
            continue;
        auto& block = *version->blocks()[b];
        for (std::size_t i = 0; i < originals[b].size(); ++i) {
            auto* instruction = originals[b][i];
            if (instruction->type() != Instruction::Type::LoadSlot)
                continue;
            auto& load = static_cast<LoadSlotInst&>(*instruction);
            if (!eligible[load.slot.index])
                continue;
            auto definition = find_definition(b, load.slot, i);
            if (definition.workspace) {
                const auto found = std::find_if(
                    block.instructions().begin(), block.instructions().end(),
                    [instruction](const auto& item) { return item.get() == instruction; });
                const auto position =
                    static_cast<std::size_t>(found - block.instructions().begin());
                auto& state =
                    block.insert_instruction<LoadSlotInst>(position, ValueId{0}, load.slot);
                state.result = version->add_value(state);
                definition.value = state.result;
            }
            load.set_source(definition.value);
        }
    }
    std::unordered_set<const Instruction*> removed;
    for (std::size_t b = 0; b < cfg.size(); ++b) {
        auto& block = *version->blocks()[b];
        for (std::size_t i = 0; i < block.instructions().size(); ++i) {
            auto* instruction = block.instructions()[i].get();
            if (instruction->type() != Instruction::Type::StoreSlot)
                continue;
            const auto& store = static_cast<const StoreSlotInst&>(*instruction);
            if (!eligible[store.slot.index])
                continue;
            if (!function.slot_table().find(store.slot).is_internal)
                block.insert_instruction<RecordInst>(i++, store.slot, store.value());
            removed.insert(instruction);
        }
    }
    for (const auto& block : version->blocks())
        for (std::size_t i = block->instructions().size(); i-- > 0;)
            if (removed.count(block->instructions()[i].get()))
                block->erase_instruction(i);

    simplify_phis(*version);
    remove_forwarded_loads(*version);
    simplify_phis(*version);
    version->compact_values();
    function.set_stage_version(IRStage::SSA, std::move(version));
    function.set_current_stage(IRStage::SSA);
    return PreservedAnalyses::none();
}

} // baltam 命名空间
