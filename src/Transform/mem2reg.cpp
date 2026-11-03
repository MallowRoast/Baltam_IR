#include "Transform/mem2reg.h"

#include <algorithm>
#include <optional>

namespace baltam {

PreservedAnalyses Mem2RegPass::run(FunctionUnit& function, FunctionAnalysisManager& analyses) {
    if (function.current_stage() != IRStage::NonSSA)
        return PreservedAnalyses::all();
    // 捕获变量及嵌套函数的存储语义尚未建模，暂不提升相关函数。
    if (!function.nested_functions().empty())
        return PreservedAnalyses::all();
    if (function.parent()) {
        const auto& functions = function.parent()->functions();
        if (std::none_of(functions.begin(), functions.end(),
                         [&function](const auto& item) { return item.get() == &function; }))
            return PreservedAnalyses::all();
    }
    const auto source = function.current_version();
    const auto& cfg = analyses.cfg();
    const auto& dominators = analyses.dominators();
    if (!cfg.entry())
        return PreservedAnalyses::all();
    const auto entry = cfg.index(cfg.entry());
    const auto slot_count = function.slot_table().size();
    std::vector<bool> eligible(slot_count, true), used(slot_count, false);
    for (const auto slot : function.parameter_slots())
        eligible[slot.index] = false;
    for (const auto slot : function.return_slots())
        eligible[slot.index] = false;
    for (std::size_t b = 0; b < cfg.size(); ++b) {
        for (const auto& instruction : cfg.block(b)->instructions()) {
            const auto type = instruction->type();
            if (type == Instruction::Type::GlobalDecl ||
                type == Instruction::Type::PersistentDecl) {
                const auto& slots =
                    type == Instruction::Type::GlobalDecl
                        ? static_cast<const GlobalDeclInst&>(*instruction).slots
                        : static_cast<const PersistentDeclInst&>(*instruction).slots;
                for (const auto slot : slots)
                    eligible[slot.index] = false;
            } else if (type == Instruction::Type::LoadSlot ||
                       type == Instruction::Type::StoreSlot) {
                const auto slot = type == Instruction::Type::LoadSlot
                                      ? static_cast<const LoadSlotInst&>(*instruction).slot
                                      : static_cast<const StoreSlotInst&>(*instruction).slot;
                used[slot.index] = true;
                if (!dominators.reachable(b))
                    eligible[slot.index] = false;
                if (type == Instruction::Type::StoreSlot &&
                    source->values()
                            .find(static_cast<const StoreSlotInst&>(*instruction).value())
                            .form() != Value::Form::Single)
                    eligible[slot.index] = false;
            } else if (instruction->effect() == Effect::MayWriteSlot) {
                // 动态调用及重载操作可能隐式修改工作区，不能穿越它们提升槽位。
                return PreservedAnalyses::all();
            }
        }
    }

    // 必然已定义分析：只有每条可达路径都完成写入，才能移除原有读取检查。
    for (std::size_t slot = 0; slot < slot_count; ++slot) {
        eligible[slot] = eligible[slot] && used[slot];
        if (!eligible[slot])
            continue;
        std::vector<bool> in(cfg.size(), true), out(cfg.size(), true);
        bool changed;
        do {
            changed = false;
            for (std::size_t b = 0; b < cfg.size(); ++b) {
                if (!dominators.reachable(b))
                    continue;
                bool defined = b != entry;
                for (const auto predecessor : cfg.predecessors(b))
                    if (dominators.reachable(predecessor))
                        defined = defined && out[predecessor];
                in[b] = defined;
                for (const auto& instruction : cfg.block(b)->instructions())
                    if (instruction->type() == Instruction::Type::StoreSlot &&
                        static_cast<const StoreSlotInst&>(*instruction).slot.index == slot)
                        defined = true;
                if (out[b] != defined) {
                    out[b] = defined;
                    changed = true;
                }
            }
        } while (changed);
        for (std::size_t b = 0; b < cfg.size() && eligible[slot]; ++b) {
            if (!dominators.reachable(b))
                continue;
            bool defined = in[b];
            for (const auto& instruction : cfg.block(b)->instructions()) {
                if (instruction->type() == Instruction::Type::StoreSlot &&
                    static_cast<const StoreSlotInst&>(*instruction).slot.index == slot)
                    defined = true;
                if (instruction->type() == Instruction::Type::LoadSlot &&
                    static_cast<const LoadSlotInst&>(*instruction).slot.index == slot && !defined)
                    eligible[slot] = false;
            }
        }
    }
    if (std::none_of(eligible.begin(), eligible.end(), [](bool value) { return value; }))
        return PreservedAnalyses::all();

    // 在独立副本中操作，成功后再发布，不破坏源码对应的 Non-SSA 版本。
    auto version = source->clone(function);
    std::vector<std::vector<PhiInst*>> phis(cfg.size(), std::vector<PhiInst*>(slot_count, nullptr));
    for (std::size_t slot = 0; slot < slot_count; ++slot) {
        if (!eligible[slot])
            continue;
        std::vector<std::size_t> work;
        std::vector<bool> queued(cfg.size(), false);
        for (std::size_t b = 0; b < cfg.size(); ++b) {
            for (const auto& instruction : cfg.block(b)->instructions()) {
                if (instruction->type() == Instruction::Type::StoreSlot &&
                    static_cast<const StoreSlotInst&>(*instruction).slot.index == slot &&
                    !queued[b]) {
                    work.push_back(b);
                    queued[b] = true;
                }
            }
        }
        while (!work.empty()) {
            const auto b = work.back();
            work.pop_back();
            for (const auto boundary : dominators.frontier(b)) {
                if (phis[boundary][slot])
                    continue;
                auto& block = *version->blocks()[boundary];
                auto& phi = block.insert_instruction<PhiInst>(0, ValueId{0});
                phi.result = version->add_value(phi, 0);
                phis[boundary][slot] = &phi;
                if (!queued[boundary]) {
                    queued[boundary] = true;
                    work.push_back(boundary);
                }
            }
        }
    }
    std::vector<std::vector<ValueId>> definitions(slot_count);
    std::vector<std::vector<std::size_t>> removed(cfg.size());
    struct Frame {
        std::size_t block;
        bool exit;
        std::vector<std::size_t> depths;
    };
    std::vector<Frame> work{{entry, false, {}}};
    while (!work.empty()) {
        auto frame = std::move(work.back());
        work.pop_back();
        if (frame.exit) {
            for (std::size_t slot = 0; slot < slot_count; ++slot)
                definitions[slot].resize(frame.depths[slot]);
            continue;
        }
        frame.depths.resize(slot_count);
        for (std::size_t slot = 0; slot < slot_count; ++slot) {
            frame.depths[slot] = definitions[slot].size();
            if (phis[frame.block][slot])
                definitions[slot].push_back(phis[frame.block][slot]->result);
        }
        const auto& instructions = version->blocks()[frame.block]->instructions();
        for (std::size_t i = 0; i < instructions.size(); ++i) {
            auto& instruction = *instructions[i];
            if (instruction.type() == Instruction::Type::StoreSlot) {
                const auto& store = static_cast<const StoreSlotInst&>(instruction);
                if (!eligible[store.slot.index])
                    continue;
                definitions[store.slot.index].push_back(store.value());
                removed[frame.block].push_back(i);
            } else if (instruction.type() == Instruction::Type::LoadSlot) {
                const auto& load = static_cast<const LoadSlotInst&>(instruction);
                if (!eligible[load.slot.index])
                    continue;
                version->replace_all_uses(load.result, definitions[load.slot.index].back());
                removed[frame.block].push_back(i);
            }
        }
        for (const auto successor : cfg.successors(frame.block)) {
            for (std::size_t slot = 0; slot < slot_count; ++slot) {
                if (!phis[successor][slot])
                    continue;
                const auto& values = definitions[slot];
                phis[successor][slot]->add_incoming(
                    version->blocks()[frame.block].get(),
                    values.empty() ? std::nullopt : std::optional<ValueId>{values.back()});
            }
        }
        frame.exit = true;
        const auto block = frame.block;
        work.push_back(std::move(frame));
        const auto& children = dominators.children(block);
        for (auto it = children.rbegin(); it != children.rend(); ++it)
            work.push_back({*it, false, {}});
    }
    for (std::size_t b = 0; b < cfg.size(); ++b) {
        for (std::size_t slot = 0; slot < slot_count; ++slot) {
            if (!phis[b][slot])
                continue;
            // 不可达前驱也必须显式记录，但不会参与可达区域的定义分析。
            for (const auto predecessor : cfg.predecessors(b))
                if (!dominators.reachable(predecessor))
                    phis[b][slot]->add_incoming(version->blocks()[predecessor].get(), std::nullopt);
        }
        for (auto it = removed[b].rbegin(); it != removed[b].rend(); ++it)
            version->blocks()[b]->erase_instruction(*it);
    }
    version->compact_values();
    function.set_stage_version(IRStage::SSA, std::move(version));
    function.set_current_stage(IRStage::SSA);
    // 新版本的块身份与旧版本不同，旧 CFG 和支配分析均不能继续使用。
    return PreservedAnalyses::none();
}

} // baltam 命名空间
