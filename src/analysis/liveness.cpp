#include "analysis/liveness.h"

#include <algorithm>
#include <unordered_set>

#include "analysis/cfg_analysis.h"
#include "analysis/name_analysis_utils.h"

namespace baltam {
namespace analysis {

namespace {

using NameSet = std::unordered_set<std::string>;

struct BlockNameInfo {
    NameSet upward_uses;
    NameSet defs;
};

const std::vector<std::string>& empty_name_list() {
    static const std::vector<std::string> empty;
    return empty;
}

std::vector<std::string> to_sorted_name_list(const NameSet& names) {
    std::vector<std::string> result(names.begin(), names.end());
    std::sort(result.begin(), result.end());
    return result;
}

BlockNameInfo compute_block_name_info(const BasicBlock& block) {
    BlockNameInfo info;

    detail::for_each_block_node(block, [&](const NonSSANode& node) {
        detail::for_each_node_use(node, [&](const std::string& name) {
            if (info.defs.find(name) == info.defs.end()) {
                info.upward_uses.insert(name);
            }
        });

        detail::for_each_node_def(node, [&](const std::string& name) { info.defs.insert(name); });
    });

    return info;
}

}  // namespace

const std::vector<std::string>& Liveness::Result::live_in_of(const BasicBlock* block) const {
    if (block == nullptr) {
        return empty_name_list();
    }

    auto it = live_in.find(block);
    if (it == live_in.end()) {
        return empty_name_list();
    }
    return it->second;
}

const std::vector<std::string>& Liveness::Result::live_out_of(const BasicBlock* block) const {
    if (block == nullptr) {
        return empty_name_list();
    }

    auto it = live_out.find(block);
    if (it == live_out.end()) {
        return empty_name_list();
    }
    return it->second;
}

Liveness::Result Liveness::run(Function& function,
                               FunctionAnalysisManager& analysis_manager) const {
    const CFGAnalysis::Result& cfg = analysis_manager.get<CFGAnalysis>(function);

    Result result;
    std::unordered_map<const BasicBlock*, BlockNameInfo> block_infos;
    std::unordered_map<const BasicBlock*, NameSet> live_in_sets;
    std::unordered_map<const BasicBlock*, NameSet> live_out_sets;

    for (const BasicBlock* block : cfg.reverse_postorder) {
        block_infos.emplace(block, compute_block_name_info(*block));
        live_in_sets.emplace(block, NameSet{});
        live_out_sets.emplace(block, NameSet{});
        result.live_in.emplace(block, std::vector<std::string>{});
        result.live_out.emplace(block, std::vector<std::string>{});
    }

    bool changed = true;
    while (changed) {
        changed = false;

        for (const BasicBlock* block : cfg.postorder) {
            NameSet new_live_out;
            for (BasicBlock* successor : block->successors()) {
                if (!cfg.is_reachable(successor)) {
                    continue;
                }

                const NameSet& successor_live_in = live_in_sets[successor];
                new_live_out.insert(successor_live_in.begin(), successor_live_in.end());
            }

            NameSet new_live_in = block_infos[block].upward_uses;
            for (const std::string& name : new_live_out) {
                if (block_infos[block].defs.find(name) == block_infos[block].defs.end()) {
                    new_live_in.insert(name);
                }
            }

            if (new_live_out != live_out_sets[block]) {
                live_out_sets[block] = std::move(new_live_out);
                changed = true;
            }
            if (new_live_in != live_in_sets[block]) {
                live_in_sets[block] = std::move(new_live_in);
                changed = true;
            }
        }
    }

    for (const BasicBlock* block : cfg.reverse_postorder) {
        result.live_in[block] = to_sorted_name_list(live_in_sets[block]);
        result.live_out[block] = to_sorted_name_list(live_out_sets[block]);
    }

    return result;
}

}  // namespace analysis
}  // namespace baltam
