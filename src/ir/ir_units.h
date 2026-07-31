#pragma once

#include "ir/ir_cfg.h"

#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace baltam {

struct MFileUnit;
struct AnonymousFunctionUnit;
struct CodeUnit;
struct CommandUnit;
struct FunctionUnit;

/**
 * @brief 可 lowering 的 IR 代码单元。
 *
 * 第一版 `CodeUnit` 承载稳定的顶层元数据、slot 表和 block 列表。
 * `CodeUnit` 通过 `std::unique_ptr` 统一拥有全部 `BasicBlock`，这样块对象地址稳定，
 * 可以让 CFG 边和跳转指令直接保存 `BasicBlock*`。
 */
struct CodeUnit {
    /**
     * @brief `CodeUnit` 的单元类型。
     */
    enum Type : std::uint8_t {
        Script,
        Function,
        AnonymousFunction,
        Command,
    };

    /**
     * @brief 多态删除所需的虚析构函数。
     */
    virtual ~CodeUnit() = default;

    InternedString name;
    SlotTable slot_table;
    ValueTable value_table;
    BasicBlock* entry_block = nullptr;
    std::vector<std::unique_ptr<BasicBlock>> basic_blocks;
    SourceSpan source_span;

    /**
     * @brief 获取当前代码单元的实际类型。
     *
     * 具体类型由派生类决定，不再在基类中重复保存一个 `type` 字段。
     *
     * @return 当前代码单元的实际类型。
     */
    [[nodiscard]] virtual Type type() const noexcept = 0;

    /**
     * @brief 判断当前单元是否为脚本单元。
     *
     * @return `type() == Script` 时返回 true。
     */
    [[nodiscard]] bool is_script() const noexcept {
        return type() == Script;
    }

    /**
     * @brief 判断当前单元是否为函数单元。
     *
     * @return `type() == Function` 时返回 true。
     */
    [[nodiscard]] bool is_function() const noexcept {
        return type() == Function;
    }

    /**
     * @brief 判断当前单元是否为匿名函数体单元。
     *
     * @return `type() == AnonymousFunction` 时返回 true。
     */
    [[nodiscard]] bool is_anonymous_function() const noexcept {
        return type() == AnonymousFunction;
    }

    /**
     * @brief 判断当前单元是否为命令行 / REPL 输入单元。
     *
     * @return `type() == Command` 时返回 true。
     */
    [[nodiscard]] bool is_command() const noexcept {
        return type() == Command;
    }

    /**
     * @brief 为当前 unit 创建一个基本块。
     */
    [[nodiscard]] BasicBlock* create_block(std::string_view label, SourceSpan source_span) {
        auto block = std::make_unique<BasicBlock>();
        block->parent = this;
        block->label = InternedString(label);
        block->source_span = source_span;

        BasicBlock* block_ptr = block.get();
        basic_blocks.push_back(std::move(block));
        return block_ptr;
    }

    /**
     * @brief 设置当前 unit 的入口基本块。
     *
     * @return `block == nullptr` 或其属于当前 unit 时返回 true。
     */
    [[nodiscard]] bool set_entry_block(BasicBlock* block) noexcept {
        if (block != nullptr && block->parent != this) {
            return false;
        }

        entry_block = block;
        return true;
    }
};

/**
 * @brief `CodeUnit(type=script)` 的薄特化。
 *
 * 脚本单元来源于 `.m` 文件，因此显式记录所属 `MFileUnit`。
 */
struct ScriptUnit : CodeUnit {
    /**
     * @brief 构造一个脚本单元。
     */
    ScriptUnit() noexcept = default;

    /**
     * @brief 获取当前脚本单元的类型。
     *
     * @return 固定返回 `CodeUnit::Script`。
     */
    [[nodiscard]] Type type() const noexcept override {
        return CodeUnit::Script;
    }

    MFileUnit* file = nullptr;
};

/**
 * @brief `CodeUnit(type=function)` 的薄特化。
 *
 * 第一版 `FunctionUnit` 在 `CodeUnit` 之上增加函数专属接口描述，并显式固定函数语义约束：
 * - `type()` 固定返回 `Function`
 * - 参数与返回值接口由 `param_slots` / `return_slots` 描述
 * - `nargin`、`nargout`、`varargin`、`varargout` 通过对应 `SlotTag` 表达
 */
struct FunctionUnit : CodeUnit {
    /**
     * @brief 构造一个函数单元。
     */
    FunctionUnit() noexcept = default;

    /**
     * @brief 获取当前函数单元的类型。
     *
     * @return 固定返回 `CodeUnit::Function`。
     */
    [[nodiscard]] Type type() const noexcept override {
        return CodeUnit::Function;
    }

    MFileUnit* file = nullptr;

    /**
     * @brief 按声明顺序保存函数参数对应的 slot。
     *
     * 参数名字、源码位置等信息统一由对应 `Slot` 提供，不再重复保存一份描述结构。
     */
    std::vector<Slot> param_slots;

    /**
     * @brief 按声明顺序保存函数返回值对应的 slot。
     *
     * 返回值名字、源码位置等信息统一由对应 `Slot` 提供，不再重复保存一份描述结构。
     */
    std::vector<Slot> return_slots;
};

/**
 * @brief 命令行 / REPL 输入对应的代码单元。
 *
 * `CommandUnit` 不是 `.m` 文件的一部分。当前 runtime 只保留一个当前命令单元，
 * 由 `InterpreterContext::command` 拥有。命令中的普通用户名字按 base workspace
 * 语义建模，后续 lowering 应使用 `SlotTag::BaseVar` 表达静态可见的 base workspace
 * binding。
 */
struct CommandUnit : CodeUnit {
    /**
     * @brief 构造一个命令代码单元。
     */
    CommandUnit() noexcept = default;

    /**
     * @brief 获取当前命令单元的类型。
     *
     * @return 固定返回 `CodeUnit::Command`。
     */
    [[nodiscard]] Type type() const noexcept override {
        return CodeUnit::Command;
    }
};

/**
 * @brief 匿名函数体代码单元。
 *
 * 匿名函数没有 Matlab 名字空间里的函数名，也没有显式返回参数。其参数 slot 按源码
 * `@(args)` 顺序记录，捕获 slot 按 closure capture layout 顺序记录，body 直接通过
 * `ReturnInst` 返回表达式 lowering 后的 `ValueId`。
 */
struct AnonymousFunctionUnit : CodeUnit {
    /**
     * @brief 构造一个匿名函数体单元。
     */
    AnonymousFunctionUnit() noexcept = default;

    /**
     * @brief 获取当前匿名函数体单元的类型。
     *
     * @return 固定返回 `CodeUnit::AnonymousFunction`。
     */
    [[nodiscard]] Type type() const noexcept override {
        return CodeUnit::AnonymousFunction;
    }

    CodeUnit* lexical_parent = nullptr;
    std::vector<Slot> param_slots;
    std::vector<Slot> capture_slots;
};

/**
 * @brief 文件级 IR 单元。
 *
 * `MFileUnit` 对应一个 `.m` 文件，直接拥有该文件中的脚本 / 具名函数 `CodeUnit`，并通过
 * `entry_unit` 指向入口代码单元。匿名函数体由 `InterpreterContext::anonymous_functions`
 * 持有；匿名函数构造指令通过 `std::shared_ptr<AnonymousFunctionUnit>` 直接引用目标。
 *
 * 文件是脚本文件还是函数文件，不再额外缓存一份 `file_type` 状态，而是从入口代码单元的
 * 实际类型推导。
 */
struct MFileUnit {
    NormalizedPath path;
    std::vector<std::unique_ptr<CodeUnit>> code_units;
    CodeUnit* entry_unit = nullptr;
    std::unordered_map<InternedString, FunctionUnit*> local_function_map;

    /**
     * @brief 获取文件去掉扩展名后的 stem。
     *
     * @return 当前文件路径对应的 stem。
     */
    [[nodiscard]] std::filesystem::path file_stem() const {
        return path.stem();
    }

    /**
     * @brief 判断当前文件是否为脚本文件。
     *
     * @return 入口代码单元存在且其类型为 `CodeUnit::Script` 时返回 true。
     */
    [[nodiscard]] bool is_script_file() const noexcept {
        return entry_unit != nullptr && entry_unit->is_script();
    }

    /**
     * @brief 判断当前文件是否为函数文件。
     *
     * @return 入口代码单元存在且其类型为 `CodeUnit::Function` 时返回 true。
     */
    [[nodiscard]] bool is_function_file() const noexcept {
        return entry_unit != nullptr && entry_unit->is_function();
    }

    [[nodiscard]] FunctionUnit* find_local_function(std::string_view name) noexcept {
        const auto it = local_function_map.find(InternedString(name));
        return it != local_function_map.end() ? it->second : nullptr;
    }

    [[nodiscard]] const FunctionUnit* find_local_function(std::string_view name) const noexcept {
        const auto it = local_function_map.find(InternedString(name));
        return it != local_function_map.end() ? it->second : nullptr;
    }
};

} // namespace baltam
