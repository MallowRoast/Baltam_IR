#include "analysis/def_use.h"

#include <unordered_set>

#include "analysis/cfg_analysis.h"
#include "analysis/name_analysis_utils.h"

namespace baltam {
namespace analysis {

namespace {

const std::vector<const NonSSANode*>& empty_node_list() {
    static const std::vector<const NonSSANode*> empty;
    return empty;
}

const std::vector<const BasicBlock*>& empty_block_list() {
    static const std::vector<const BasicBlock*> empty;
    return empty;
}

}  // namespace

const std::vector<const NonSSANode*>& DefUse::Result::definitions_of(
    const std::string& name) const {
    if (name.empty()) {
        return empty_node_list();
    }

    auto it = defs_of_name.find(name);
    if (it == defs_of_name.end()) {
        return empty_node_list();
    }
    return it->second;
}

const std::vector<const NonSSANode*>& DefUse::Result::uses_of(const std::string& name) const {
    if (name.empty()) {
        return empty_node_list();
    }

    auto it = uses_of_name.find(name);
    if (it == uses_of_name.end()) {
        return empty_node_list();
    }
    return it->second;
}

const std::vector<const BasicBlock*>& DefUse::Result::definition_blocks_of(
    const std::string& name) const {
    if (name.empty()) {
        return empty_block_list();
    }

    auto it = def_blocks_of_name.find(name);
    if (it == def_blocks_of_name.end()) {
        return empty_block_list();
    }
    return it->second;
}

DefUse::Result DefUse::run(Function& function, FunctionAnalysisManager& analysis_manager) const {
    const CFGAnalysis::Result& cfg = analysis_manager.get<CFGAnalysis>(function);

    Result result;
    std::unordered_map<std::string, std::unordered_set<const BasicBlock*>> seen_def_blocks;

    for (const BasicBlock* block : cfg.reverse_postorder) {
        detail::for_each_block_node(*block, [&](const NonSSANode& node) {
            detail::for_each_node_use(node, [&](const std::string& name) {
                result.uses_of_name[name].push_back(&node);
            });

            detail::for_each_node_def(node, [&](const std::string& name) {
                result.defs_of_name[name].push_back(&node);

                std::unordered_set<const BasicBlock*>& seen_blocks = seen_def_blocks[name];
                if (seen_blocks.insert(block).second) {
                    result.def_blocks_of_name[name].push_back(block);
                }
            });
        });
    }

    return result;
}

}  // namespace analysis
}  // namespace baltam
