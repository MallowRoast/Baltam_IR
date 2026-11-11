#include "Transform/load_store_elim.h"
#include "ir/ir_constant.h"
#include "ba_obj/ba_type.h"

#include <optional>
#include <deque>
#include <cassert>
#include <unordered_map>
#include <vector>

namespace baltam {
namespace {

// PHI 可以传递 Undefined；带来源的 load 属于 SSA 状态读取，不能按普通读取处理。
bool has_concrete_value(const IRStageVersion& version, ValueId value) {
    const auto& info = version.values().find(value);
    if (info.form() != Value::Form::Single)
        return false;
    const auto* definition = info.definition();
    if (!definition || definition->type() == Instruction::Type::Phi)
        return false;
    return definition->type() != Instruction::Type::LoadSlot ||
           !static_cast<const LoadSlotInst&>(*definition).has_source();
}

using KnownValues = std::vector<std::optional<ValueId>>;

void transfer_known(const Instruction& instruction, const IRStageVersion& version,
                    const std::vector<bool>& eligible, KnownValues& known) {
    if (instruction.type() == Instruction::Type::LoadSlot) {
        const auto& load = static_cast<const LoadSlotInst&>(instruction);
        if (eligible[load.slot.index])
            known[load.slot.index] = has_concrete_value(version, load.result)
                                         ? std::optional<ValueId>{load.result}
                                         : std::nullopt;
    } else if (instruction.type() == Instruction::Type::StoreSlot) {
        const auto& store = static_cast<const StoreSlotInst&>(instruction);
        if (eligible[store.slot.index])
            known[store.slot.index] = has_concrete_value(version, store.value())
                                          ? std::optional<ValueId>{store.value()}
                                          : std::nullopt;
        else
            known.assign(known.size(), std::nullopt);
    } else if (instruction.effect() == Effect::MayWriteSlot) {
        known.assign(known.size(), std::nullopt);
    }
}

// 不创建 PHI；只有全部可达前驱一致的值才能进入块，且定义必须严格支配该块。
std::vector<KnownValues> available_values(const IRStageVersion& version, const CFGAnalysis& cfg,
                                          const DominatorAnalysis& dom,
                                          const std::vector<bool>& eligible) {
    const auto count = eligible.size();
    // reached=false 是格底；已可达环境中的空值表示 Unknown，不与格底混用。
    struct State {
        bool reached = false;
        KnownValues values;
    };
    std::vector<KnownValues> incoming(cfg.size(), KnownValues(count));
    std::vector<State> outgoing(cfg.size(), State{false, KnownValues(count)});
    std::deque<std::size_t> worklist;
    std::vector<bool> queued(cfg.size());
    if (cfg.entry()) {
        const auto entry = cfg.index(cfg.entry());
        worklist.push_back(entry);
        queued[entry] = true;
    }
    while (!worklist.empty()) {
        const auto b = worklist.front();
        worklist.pop_front();
        queued[b] = false;
        State joined{cfg.block(b) == cfg.entry(), KnownValues(count)};
        if (!joined.reached)
            for (const auto p : cfg.predecessors(b)) {
                if (!outgoing[p].reached)
                    continue;
                if (!joined.reached) {
                    joined = outgoing[p];
                } else {
                    for (std::size_t s = 0; s < count; ++s)
                        if (joined.values[s] != outgoing[p].values[s])
                            joined.values[s].reset();
                }
            }
        if (!joined.reached)
            continue;
        for (auto& value : joined.values) {
            if (!value)
                continue;
            const auto* definition = version.values().find(*value).definition();
            if (definition && (definition->parent() == cfg.block(b) ||
                               !dom.dominates(cfg.index(definition->parent()), b)))
                value.reset();
        }
        incoming[b] = joined.values;
        auto result = joined.values;
        // 固定 IR 上求不动点：load 总是生成自己的结果，不在求解期间转发或删指令。
        for (const auto& instruction : cfg.block(b)->instructions())
            transfer_known(*instruction, version, eligible, result);
        if (outgoing[b].reached && outgoing[b].values == result)
            continue;
        // 固定 IR 的 OUT 只能从格底到 Known，再到 Unknown，不能反向变化。
        if (outgoing[b].reached)
            for (std::size_t s = 0; s < count; ++s)
                assert(!result[s] || (outgoing[b].values[s] && outgoing[b].values[s] == result[s]));
        outgoing[b] = {true, std::move(result)};
        for (const auto successor : cfg.successors(b))
            if (!queued[successor]) {
                worklist.push_back(successor);
                queued[successor] = true;
            }
    }
    return incoming;
}

bool safe_store_crossing(const Instruction& instruction, const IRStageVersion& version) {
    switch (instruction.type()) {
    case Instruction::Type::Const:
    case Instruction::Type::Phi:
    case Instruction::Type::Goto:
        return true;
    case Instruction::Type::Branch: {
        const auto value = static_cast<const BranchInst&>(instruction).condition();
        const auto& info = version.values().find(value);
        return has_concrete_value(version, value) && info.type() == ba_bool_mat && info.shape() &&
               *info.shape() == std::vector<std::optional<std::size_t>>{1, 1};
    }
    default:
        return false;
    }
}

// 必须在所有后继路径上、读取/异常/工作区观察之前被覆盖，旧 store 才可删除。
// 从空集合开始，纯循环不能凭回边自证最终会发生覆盖写入。
bool remove_overwritten_stores(IRStageVersion& version, const CFGAnalysis& cfg,
                               const DominatorAnalysis& dom, const std::vector<bool>& eligible) {
    const auto count = eligible.size();
    using Killed = std::vector<bool>;
    std::vector<Killed> incoming(cfg.size(), Killed(count)), outgoing(cfg.size(), Killed(count));
    const auto transfer = [&](const Instruction& instruction, Killed& state) {
        if (instruction.type() == Instruction::Type::StoreSlot) {
            const auto& store = static_cast<const StoreSlotInst&>(instruction);
            state.assign(count, false);
            if (eligible[store.slot.index] && has_concrete_value(version, store.value()))
                state[store.slot.index] = true;
        } else if (!safe_store_crossing(instruction, version)) {
            state.assign(count, false);
        }
    };
    bool changed;
    do {
        changed = false;
        for (std::size_t b = cfg.size(); b-- > 0;) {
            if (!dom.reachable(b))
                continue;
            Killed joined(count);
            bool first = true;
            for (const auto successor : cfg.successors(b)) {
                if (first) {
                    joined = incoming[successor];
                    first = false;
                } else {
                    for (std::size_t s = 0; s < count; ++s)
                        joined[s] = joined[s] && incoming[successor][s];
                }
            }
            auto result = joined;
            const auto& instructions = cfg.block(b)->instructions();
            for (std::size_t i = instructions.size(); i-- > 0;)
                transfer(*instructions[i], result);
            if (incoming[b] != result || outgoing[b] != joined) {
                incoming[b] = std::move(result);
                outgoing[b] = std::move(joined);
                changed = true;
            }
        }
    } while (changed);
    bool removed = false;
    for (std::size_t b = 0; b < cfg.size(); ++b) {
        if (!dom.reachable(b))
            continue;
        auto state = outgoing[b];
        auto& block = *version.blocks()[b];
        for (std::size_t i = block.instructions().size(); i-- > 0;) {
            const auto& instruction = *block.instructions()[i];
            bool dead = false;
            if (instruction.type() == Instruction::Type::StoreSlot) {
                const auto& store = static_cast<const StoreSlotInst&>(instruction);
                dead = eligible[store.slot.index] && state[store.slot.index] &&
                       has_concrete_value(version, store.value());
            }
            transfer(instruction, state);
            if (dead) {
                block.erase_instruction(i);
                removed = true;
            }
        }
    }
    return removed;
}

bool eliminate_load_stores(IRUnit& unit, IRStageVersion& version) {
    const auto count = unit.slot_table().size();
    std::vector<bool> eligible(count, true);
    for (std::size_t i = 0; i < count; ++i) {
        const auto& slot = unit.slot_table().find(SlotId{static_cast<std::uint32_t>(i)});
        eligible[i] = !slot.has_global_decl && !slot.has_persistent_decl &&
                      !slot.shared_by_nested && !slot.shared_target;
    }
    // 已有 SSA 来源的读取不按普通工作区读取处理。
    for (const auto& block : version.blocks())
        for (const auto& instruction : block->instructions()) {
            if (instruction->type() == Instruction::Type::LoadSlot) {
                const auto& load = static_cast<const LoadSlotInst&>(*instruction);
                if (load.has_source())
                    eligible[load.slot.index] = false;
            }
        }

    const CFGAnalysis cfg(version);
    const DominatorAnalysis dom(cfg);
    const auto incoming = available_values(version, cfg, dom, eligible);
    std::unordered_map<std::uint32_t, ValueId> replacements;
    const auto resolve = [&replacements](ValueId value) {
        auto found = replacements.find(value.index);
        while (found != replacements.end()) {
            value = found->second;
            found = replacements.find(value.index);
        }
        return value;
    };
    bool changed = false;
    for (std::size_t b = 0; b < version.blocks().size(); ++b) {
        const auto& block = version.blocks()[b];
        auto known = incoming[b];
        for (std::size_t i = 0; i < block->instructions().size();) {
            const auto& instruction = *block->instructions()[i];
            if (instruction.type() == Instruction::Type::LoadSlot) {
                const auto& load = static_cast<const LoadSlotInst&>(instruction);
                if (eligible[load.slot.index]) {
                    auto& value = known[load.slot.index];
                    if (value) {
                        *value = resolve(*value);
                        replacements.emplace(load.result.index, *value);
                        version.replace_all_uses(load.result, *value);
                        block->erase_instruction(i);
                        changed = true;
                        continue;
                    }
                    // 首次读取必须执行，保留未定义变量的报错位置。
                    if (has_concrete_value(version, load.result))
                        value = load.result;
                }
            } else if (instruction.type() == Instruction::Type::StoreSlot) {
                const auto& store = static_cast<const StoreSlotInst&>(instruction);
                if (eligible[store.slot.index]) {
                    auto& value = known[store.slot.index];
                    if (value)
                        *value = resolve(*value);
                    if (value && *value == store.value()) {
                        block->erase_instruction(i);
                        changed = true;
                        continue;
                    }
                    value = has_concrete_value(version, store.value())
                                ? std::optional<ValueId>{store.value()}
                                : std::nullopt;
                } else {
                    // 非局部存储可能别名到其他绑定，不沿用任何已知状态。
                    known.assign(count, std::nullopt);
                }
            } else if (instruction.effect() == Effect::MayWriteSlot) {
                // NoSlotWrite 仅承诺不改写槽位；不据此删除 store 或移动指令。
                known.assign(count, std::nullopt);
            }
            ++i;
        }
    }
    // 按指令顺序折叠转发后新暴露的逻辑转换和嵌套元胞构造。
    for (const auto& block : version.blocks())
        for (std::size_t i = 0; i < block->instructions().size(); ++i) {
            const auto& instruction = *block->instructions()[i];
            const_ba_obj_ptr value;
            ValueId old_result{0};
            if (instruction.type() == Instruction::Type::CellConstruct) {
                const auto& cell = static_cast<const CellConstructInst&>(instruction);
                value = make_cell_constant(version, cell.items(), cell.rows, cell.cols);
                old_result = cell.result;
            } else if (instruction.type() == Instruction::Type::Intrinsic) {
                const auto& intrinsic = static_cast<const IntrinsicInst&>(instruction);
                if (intrinsic.id == IntrinsicId::LogicalScalar && intrinsic.operand_count() == 1) {
                    value = try_fold_logical_scalar(version, intrinsic.operands().front());
                    old_result = intrinsic.result;
                }
            }
            if (!value)
                continue;
            auto& constant = block->insert_instruction<ConstInst>(i, std::move(value), ValueId{0});
            constant.result = version.add_value(constant, 0, Value::Form::Single, true);
            version.replace_all_uses(old_result, constant.result);
            block->erase_instruction(i + 1);
            changed = true;
        }
    changed |= remove_overwritten_stores(version, cfg, dom, eligible);
    // 不删除可能抛错的首次 load，只清理已无 use 的常量。
    changed |= remove_unused_constants(version);
    return changed;
}

} // 匿名命名空间

PreservedAnalyses LoadStoreElimPass::run(FunctionUnit& function, FunctionAnalysisManager&) {
    if (function.current_stage() != IRStage::NonSSA)
        return PreservedAnalyses::all();
    // 自动捕获分析尚未接入；嵌套函数可能隐式共享当前工作区。
    if (!function.nested_functions().empty() || function.lexical_parent())
        return PreservedAnalyses::all();
    if (!function.stage_version(IRStage::SSA)->blocks().empty() ||
        !function.stage_version(IRStage::OptimizedSSA)->blocks().empty())
        return PreservedAnalyses::all();

    auto version = function.current_version()->clone(function);
    bool changed = false;
    while (eliminate_load_stores(function, *version))
        changed = true;
    if (!changed)
        return PreservedAnalyses::all();
    version->compact_values();
    function.set_stage_version(IRStage::NonSSA, std::move(version));
    return PreservedAnalyses::all();
}

void LoadStoreElimPass::run(CommandUnit& command) {
    auto version = command.stage_version()->clone(command);
    bool changed = false;
    while (eliminate_load_stores(command, *version))
        changed = true;
    if (!changed)
        return;
    version->compact_values();
    command.set_stage_version(std::move(version));
}

} // baltam 命名空间
