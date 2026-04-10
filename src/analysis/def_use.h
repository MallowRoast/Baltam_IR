#ifndef BALTAM_IR_ANALYSIS_DEF_USE_H
#define BALTAM_IR_ANALYSIS_DEF_USE_H

#include <string>
#include <unordered_map>
#include <vector>

#include "analysis/analysis_manager.h"

namespace baltam {
namespace analysis {

/**
 * @brief 统计名字级 definition / use / def-block 的 analysis。
 *
 * 当前分析域是 non-SSA IR 中出现的名字文本，结果只覆盖入口可达子图。
 */
class DefUse {
public:
    /**
     * @brief DefUse 的结果对象。
     *
     * - `defs_of_name` 记录每个名字对应的定义节点列表
     * - `uses_of_name` 记录每个名字对应的使用节点列表
     * - `def_blocks_of_name` 记录每个名字在哪些可达块里出现过定义
     *
     * 其中节点列表按 RPO 块顺序和块内程序顺序稳定排列，block 列表按 RPO 顺序
     * 去重后保存。
     */
    struct Result {
        std::unordered_map<std::string, std::vector<const NonSSANode*>> defs_of_name;
        std::unordered_map<std::string, std::vector<const NonSSANode*>> uses_of_name;
        std::unordered_map<std::string, std::vector<const BasicBlock*>> def_blocks_of_name;

        /**
         * @brief 返回某个名字的定义节点列表。
         */
        const std::vector<const NonSSANode*>& definitions_of(const std::string& name) const;

        /**
         * @brief 返回某个名字的使用节点列表。
         */
        const std::vector<const NonSSANode*>& uses_of(const std::string& name) const;

        /**
         * @brief 返回某个名字的定义块列表。
         */
        const std::vector<const BasicBlock*>& definition_blocks_of(const std::string& name) const;
    };

    /**
     * @brief 在一个函数上运行 DefUse analysis。
     */
    Result run(Function& function, FunctionAnalysisManager& analysis_manager) const;
};

}  // namespace analysis
}  // namespace baltam

#endif
