#include "Transform/simplifycfg.h"
#include "Transform/phi_simplify.h"
#include "ir/ir_constant.h"
#include "ba_obj/ba_obj.h"

#include <unordered_map>

namespace baltam {

namespace {
// 条件转换失败时保留 branch，让异常仍在原运行时位置发生。
bool fold_branches(IRStageVersion& version) {
    bool changed = false;
    for (const auto& block : version.blocks()) {
        const auto* end = block->terminator();
        if (!end || end->type() != Instruction::Type::Branch)
            continue;
        const auto& branch = static_cast<const BranchInst&>(*end);
        auto condition = try_fold_logical_scalar(version, branch.condition());
        if (!condition) {
            const auto* definition = version.values().find(branch.condition()).definition();
            if (definition && definition->type() == Instruction::Type::Intrinsic) {
                const auto& intrinsic = static_cast<const IntrinsicInst&>(*definition);
                if (intrinsic.id == IntrinsicId::LogicalScalar && intrinsic.operand_count() == 1)
                    condition = try_fold_logical_scalar(version, intrinsic.operands().front());
            }
        }
        if (!condition)
            continue;
        auto* target = condition->as_bool() ? branch.true_target : branch.false_target;
        block->erase_instruction(block->instructions().size() - 1);
        block->add_instruction<GotoInst>(target);
        changed = true;
    }
    return changed;
}

bool deduplicate_constants(IRStageVersion& version) {
    const CFGAnalysis cfg(version);
    const DominatorAnalysis dom(cfg);
    // 沿支配树进入/退出作用域，祖先常量可供后代复用，兄弟分支互不泄漏。
    struct Frame {
        std::size_t block;
        bool leaving = false;
        std::vector<std::string> introduced;
    };
    std::vector<Frame> pending;
    for (std::size_t b = 0; b < cfg.size(); ++b)
        if (!dom.reachable(b))
            pending.push_back({b});
    if (cfg.entry())
        pending.push_back({cfg.index(cfg.entry())});
    std::unordered_map<std::string, ValueId> constants;
    bool changed = false;
    while (!pending.empty()) {
        auto frame = std::move(pending.back());
        pending.pop_back();
        if (frame.leaving) {
            for (const auto& key : frame.introduced)
                constants.erase(key);
            continue;
        }
        auto& block = *version.blocks()[frame.block];
        for (std::size_t i = 0; i < block.instructions().size();) {
            const auto& instruction = *block.instructions()[i];
            if (instruction.type() != Instruction::Type::Const) {
                ++i;
                continue;
            }
            const auto& constant = static_cast<const ConstInst&>(instruction);
            auto key = constant_key(constant);
            const auto inserted = constants.emplace(key, constant.result);
            if (inserted.second) {
                frame.introduced.push_back(std::move(key));
                ++i;
                continue;
            }
            version.replace_all_uses(constant.result, inserted.first->second);
            block.erase_instruction(i);
            changed = true;
        }
        const auto b = frame.block;
        frame.leaving = true;
        pending.push_back(std::move(frame));
        const auto& children = dom.children(b);
        for (auto it = children.rbegin(); it != children.rend(); ++it)
            pending.push_back({*it});
    }
    return changed;
}

bool simplify_cfg(IRStageVersion& version) {
    bool changed = false;
    bool iteration_changed;
    do {
        iteration_changed = fold_branches(version);
        iteration_changed |= version.remove_unreachable_blocks();
        iteration_changed |= simplify_trivial_phis(version);
        bool merged;
        do {
            merged = false;
            for (const auto& block : version.blocks()) {
                if (version.merge_single_successor(*block)) {
                    iteration_changed = merged = true;
                    break;
                }
            }
        } while (merged);
        iteration_changed |= deduplicate_constants(version);
        changed |= iteration_changed;
    } while (iteration_changed);
    return changed;
}
} // 匿名命名空间

PreservedAnalyses SimplifyCFGPass::run(FunctionUnit& function, FunctionAnalysisManager&) {
    if (function.current_stage() != IRStage::NonSSA)
        return PreservedAnalyses::all();
    // 已发布的 SSA 可能引用 Non-SSA 指令作为回退基线。
    if (!function.stage_version(IRStage::SSA)->blocks().empty() ||
        !function.stage_version(IRStage::OptimizedSSA)->blocks().empty())
        return PreservedAnalyses::all();
    auto version = function.current_version()->clone(function);
    if (!simplify_cfg(*version))
        return PreservedAnalyses::all();
    version->compact_values();
    function.set_stage_version(IRStage::NonSSA, std::move(version));
    return PreservedAnalyses::none();
}

void SimplifyCFGPass::run(CommandUnit& command) {
    auto version = command.stage_version()->clone(command);
    if (!simplify_cfg(*version))
        return;
    version->compact_values();
    command.set_stage_version(std::move(version));
}

} // baltam 命名空间
