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
    };

    /**
     * @brief 多态删除所需的虚析构函数。
     */
    virtual ~CodeUnit() = default;

    /**
     * @brief 当前代码单元所属的父文件单元。
     *
     * 该字段是非拥有指针，由外层 builder 或 lowering 过程负责维护。
     */
    MFileUnit* parent = nullptr;
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
     * @brief 查找指定隐藏角色对应的 slot。
     *
     * 该接口依赖一个 schema 前提：在同一个 `CodeUnit` 中，除 `None` 外的同一
     * `HiddenRole` 至多出现一个对应 slot。这个唯一性约束应由 verifier 保证。
     *
     * @param role 待查找的隐藏角色。
     * @return 找到时返回对应 slot 指针，否则返回 `nullptr`。
     */
    [[nodiscard]] Slot* find_hidden_slot(SlotAttrs::HiddenRole role) noexcept {
        return slot_table.find_hidden_slot(role);
    }

    /**
     * @brief 查找指定隐藏角色对应的 slot。
     *
     * 该接口依赖一个 schema 前提：在同一个 `CodeUnit` 中，除 `None` 外的同一
     * `HiddenRole` 至多出现一个对应 slot。这个唯一性约束应由 verifier 保证。
     *
     * @param role 待查找的隐藏角色。
     * @return 找到时返回对应 slot 指针，否则返回 `nullptr`。
     */
    [[nodiscard]] const Slot* find_hidden_slot(SlotAttrs::HiddenRole role) const noexcept {
        return slot_table.find_hidden_slot(role);
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
 * 第一版 `ScriptUnit` 不新增独立字段，只在 `CodeUnit` 之上显式固定脚本语义约束：
 * `type()` 固定返回 `Script`。
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
};

/**
 * @brief `CodeUnit(type=function)` 的薄特化。
 *
 * 第一版 `FunctionUnit` 在 `CodeUnit` 之上增加函数专属接口描述，并显式固定函数语义约束：
 * - `type()` 固定返回 `Function`
 * - 参数与返回值接口由 `param_slots` / `return_slots` 描述
 * - `nargin`、`nargout`、`varargin`、`varargout` 通过 `hidden slot` 表达
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

    /**
     * @brief 按声明顺序保存函数参数对应的 slot。
     *
     * 参数名字、源码位置等信息统一由对应 `Slot` 提供，不再重复保存一份描述结构。
     */
    std::vector<SlotId> param_slots;

    /**
     * @brief 按声明顺序保存函数返回值对应的 slot。
     *
     * 返回值名字、源码位置等信息统一由对应 `Slot` 提供，不再重复保存一份描述结构。
     */
    std::vector<SlotId> return_slots;
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

    AnonymousFunctionId id = InvalidAnonymousFunctionId;
    std::vector<SlotId> param_slots;
    std::vector<SlotId> capture_slots;
};

/**
 * @brief 匿名函数体全局表。
 *
 * 第一阶段把“全局”限定在单个 IR 文件单元 / build session 中，由该表拥有所有匿名函数
 * 体。普通 IR 指令通过 `AnonymousFunctionId` 间接引用表内单元。
 */
struct AnonymousFunctionTable {
    std::vector<std::unique_ptr<AnonymousFunctionUnit>> functions;

    [[nodiscard]] bool empty() const noexcept {
        return functions.empty();
    }

    [[nodiscard]] AnonymousFunctionUnit* find(AnonymousFunctionId id) noexcept {
        return const_cast<AnonymousFunctionUnit*>(std::as_const(*this).find(id));
    }

    [[nodiscard]] const AnonymousFunctionUnit* find(AnonymousFunctionId id) const noexcept {
        if (!id.is_valid()) {
            return nullptr;
        }

        for (const auto& function : functions) {
            if (function != nullptr && function->id == id) {
                return function.get();
            }
        }
        return nullptr;
    }
};

/**
 * @brief 文件级 IR 单元。
 *
 * `MFileUnit` 对应一个 `.m` 文件，直接拥有该文件中的全部 `CodeUnit`，并通过
 * `entry_unit` 指向入口代码单元。文件是脚本文件还是函数文件，不再额外缓存一份
 * `file_type` 状态，而是从入口代码单元的实际类型推导。
 */
struct MFileUnit {
    NormalizedPath path;
    std::vector<std::unique_ptr<CodeUnit>> code_units;
    CodeUnit* entry_unit = nullptr;
    std::unordered_map<InternedString, FunctionUnit*> local_function_map;
    AnonymousFunctionTable anonymous_functions;

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
