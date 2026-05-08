#pragma once

#include <iosfwd>
#include <string>

namespace baltam {

struct CodeUnit;
struct MFileUnit;

/**
 * @brief CFG Graphviz DOT 打印选项。
 */
struct CFGDotOptions {
    // TODO: 后续如果需要对齐 LLVM 的 dot-cfg / dot-cfg-only，可以增加节点详细程度选项，
    // 在只打印 CFG 结构和把 block 内 IR 指令展开到节点之间切换。
    /**
     * @brief 是否把非终结指令数量写入 block label，便于快速判断块内是否有实体代码。
     */
    bool print_instruction_counts = true;

    /**
     * @brief 是否在用户级 break / continue 跳转边上标出源码语句。
     */
    bool label_break_continue_edges = true;
};

/**
 * @brief 将单个 CodeUnit 的 CFG 格式化为 Graphviz DOT。
 */
[[nodiscard]] std::string format_cfg_dot(
    const CodeUnit& unit,
    const CFGDotOptions& options = {});

/**
 * @brief 将整个 MFileUnit 的 CFG 格式化为 Graphviz DOT。
 */
[[nodiscard]] std::string format_cfg_dot(
    const MFileUnit& mfile,
    const CFGDotOptions& options = {});

/**
 * @brief 把单个 CodeUnit 的 CFG 打印为 Graphviz DOT。
 */
void print_cfg_dot(
    std::ostream& os,
    const CodeUnit& unit,
    const CFGDotOptions& options = {});

/**
 * @brief 把整个 MFileUnit 的 CFG 打印为 Graphviz DOT。
 */
void print_cfg_dot(
    std::ostream& os,
    const MFileUnit& mfile,
    const CFGDotOptions& options = {});

} // namespace baltam
