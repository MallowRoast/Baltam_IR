#pragma once

#include <cstddef>
#include <iosfwd>
#include <string>
#include <string_view>

namespace baltam {

struct IRModule;
struct MFileUnit;

/**
 * @brief IR 文本打印选项。
 */
struct IRPrintOptions {
    /**
     * @brief 外部直接提供的源码文本。
     *
     * 当该字段非空时，打印器优先用它解析 `SourceSpan` 对应的源码片段。
     */
    std::string_view source_text;

    /**
     * @brief 当 `source_text` 为空时，是否尝试从 `MFileUnit::path` 读取源码。
     */
    bool load_source_from_path = true;

    /**
     * @brief 是否在语句级 IR 指令行尾追加源码注释。
     */
    bool print_source_comments = true;

    /**
     * @brief 是否在源码注释前缀中打印源码行号。
     */
    bool print_source_line_numbers = true;

    /**
     * @brief 是否打印 slot 表。
     */
    bool print_slot_table = true;

    /**
     * @brief 是否在结果值指令后打印 `ValueTable` 中记录的类型事实。
     */
    bool print_type_facts = true;

    /**
     * @brief 是否打印文件级头注释。
     */
    bool print_file_header = true;

    /**
     * @brief 源码注释起始列的最小值。
     *
     * 打印器会在不早于该列的位置对齐所有源码注释。
     */
    std::size_t min_comment_column = 56;
};

/**
 * @brief 将整个 `IRModule` 格式化为接近 LLVM IR 的文本表示。
 */
[[nodiscard]] std::string format_ir(
    const IRModule& module,
    const IRPrintOptions& options = {});

/**
 * @brief 将整个 `MFileUnit` 格式化为接近 LLVM IR 的文本表示。
 */
[[nodiscard]] std::string format_ir(
    const MFileUnit& mfile,
    const IRPrintOptions& options = {});

/**
 * @brief 把整个 `IRModule` 打印到输出流。
 */
void print_ir(
    std::ostream& os,
    const IRModule& module,
    const IRPrintOptions& options = {});

/**
 * @brief 把整个 `MFileUnit` 打印到输出流。
 */
void print_ir(
    std::ostream& os,
    const MFileUnit& mfile,
    const IRPrintOptions& options = {});

} // namespace baltam
