#pragma once

#include "ir/ir_inst.h"
#include "ir/ir_type.h"

#include <memory>
#include <utility>
#include <vector>

namespace baltam {

struct CodeUnit;

/**
 * @brief 第一版值类型事实。
 */
struct TypeFact {
    bool is_unknown = true;
    bool is_scalar = false;
    TypeSet types = TypeSet::any();
};

/**
 * @brief slot 级语义属性。
 */
struct SlotAttrs {
    /**
     * @brief `hidden slot` 的角色类型。
     *
     * 这类 slot 不对应用户源码中的普通局部变量，而是承载调用约定或执行环境
     * 所必需的运行时状态。这样设计的目的主要有三点：
     * 1. 让 frame 布局保持稳定，不因可变参数个数或环境对象细节而动态改变。
     * 2. 把用户可见变量与运行时辅助状态分开，降低后续优化和验证的歧义。
     * 3. 让 bytecode、解释器和 JIT 都能通过统一的 slot 机制访问这些隐藏状态。
     */
    enum HiddenRole : std::uint8_t {
        None,             ///< 不是特殊隐藏角色。
        Nargin,           ///< 当前调用点实际输入参数个数。
        Nargout,          ///< 当前调用点期望输出参数个数。
        Varargin,         ///< 多余输入参数的聚合容器，而不是动态数量的多个 slot。
        Varargout,        ///< 额外输出参数的聚合容器，而不是动态数量的多个 slot。
        WorkspaceHandle,  ///< 工作区句柄，用于间接访问 script workspace。
    };

    /**
     * @brief 构造一个清零后的 slot 属性集合。
     */
    SlotAttrs() noexcept : is_user_visible(0), is_mutable(0), hidden_role(None) {}

    std::uint8_t is_user_visible : 1;
    std::uint8_t is_mutable : 1;
    std::uint8_t hidden_role : 3;
};

/**
 * @brief frame 中的变量槽位定义。
 */
struct Slot {
    /**
     * @brief slot 的类别。
     */
    enum Type : std::uint8_t {
        Arg,
        Local,
        Ret,
        Hidden,
    };

    SlotId slot_id = InvalidSlotId;
    Type type = Local;
    InternedString name;
    SourceSpan source_span;
    SlotAttrs attrs;

    /**
     * @brief 判断当前 slot 是否为参数 slot。
     *
     * @return `type == Arg` 时返回 true。
     */
    [[nodiscard]] bool is_arg() const noexcept {
        return type == Arg;
    }

    /**
     * @brief 判断当前 slot 是否为局部 slot。
     *
     * @return `type == Local` 时返回 true。
     */
    [[nodiscard]] bool is_local() const noexcept {
        return type == Local;
    }

    /**
     * @brief 判断当前 slot 是否为返回值 slot。
     *
     * @return `type == Ret` 时返回 true。
     */
    [[nodiscard]] bool is_ret() const noexcept {
        return type == Ret;
    }

    /**
     * @brief 判断当前 slot 是否为隐藏 slot。
     *
     * @return `type == Hidden` 时返回 true。
     */
    [[nodiscard]] bool is_hidden() const noexcept {
        return type == Hidden;
    }
};

/**
 * @brief `CodeUnit` 的 slot 表。
 *
 * 当前版本直接使用单一 `slots` 容器保存全部 slot 定义，slot 的分类由
 * `Slot::type` 与 `SlotAttrs::hidden_role` 决定，不再额外维护并行分类索引。
 */
struct SlotTable {
    std::vector<Slot> slots;

    /**
     * @brief 判断 slot 表是否为空。
     *
     * @return `slots.empty()` 的结果。
     */
    [[nodiscard]] bool empty() const noexcept {
        return slots.empty();
    }

    /**
     * @brief 按 `slot_id` 查找 slot。
     *
     * @param slot_id 待查找的 slot ID。
     * @return 找到时返回对应 slot 指针，否则返回 `nullptr`。
     */
    [[nodiscard]] Slot* find_slot(SlotId slot_id) noexcept {
        return const_cast<Slot*>(std::as_const(*this).find_slot(slot_id));
    }

    /**
     * @brief 按 `slot_id` 查找 slot。
     *
     * @param slot_id 待查找的 slot ID。
     * @return 找到时返回对应 slot 指针，否则返回 `nullptr`。
     */
    [[nodiscard]] const Slot* find_slot(SlotId slot_id) const noexcept {
        for (const Slot& slot : slots) {
            if (slot.slot_id == slot_id) {
                return &slot;
            }
        }
        return nullptr;
    }

    /**
     * @brief 查找指定隐藏角色对应的 slot。
     *
     * @param role 待查找的隐藏角色。
     * @return 找到时返回对应 slot 指针，否则返回 `nullptr`。
     */
    [[nodiscard]] Slot* find_hidden_slot(SlotAttrs::HiddenRole role) noexcept {
        return const_cast<Slot*>(std::as_const(*this).find_hidden_slot(role));
    }

    /**
     * @brief 查找指定隐藏角色对应的 slot。
     *
     * @param role 待查找的隐藏角色。
     * @return 找到时返回对应 slot 指针，否则返回 `nullptr`。
     */
    [[nodiscard]] const Slot* find_hidden_slot(SlotAttrs::HiddenRole role) const noexcept {
        for (const Slot& slot : slots) {
            if (slot.is_hidden() && slot.attrs.hidden_role == role) {
                return &slot;
            }
        }
        return nullptr;
    }

};

/**
 * @brief `ValueId` 对应的 side metadata。
 */
struct ValueInfo {
    ValueId value_id = InvalidValueId;
    std::size_t result_index = 0;
    TypeFact type_fact;
    Instruction* def = nullptr;
};

/**
 * @brief `CodeUnit` 的值表。
 */
struct ValueTable {
    std::vector<ValueInfo> values;

    /**
     * @brief 判断值表是否为空。
     */
    [[nodiscard]] bool empty() const noexcept {
        return values.empty();
    }

    /**
     * @brief 按 `ValueId` 查找值信息。
     */
    [[nodiscard]] ValueInfo* find(ValueId value_id) noexcept {
        return const_cast<ValueInfo*>(std::as_const(*this).find(value_id));
    }

    /**
     * @brief 按 `ValueId` 查找值信息。
     */
    [[nodiscard]] const ValueInfo* find(ValueId value_id) const noexcept {
        if (!value_id.is_valid() || value_id.value() >= values.size()) {
            return nullptr;
        }
        return &values[value_id.value()];
    }
};

/**
 * @brief IR 基本块。
 *
 * `BasicBlock` 直接拥有一组指令对象，采用
 * `std::vector<std::unique_ptr<Instruction>>` 保存，以避免继承层次下的对象切片。
 * CFG 边则显式保存在 `predecessors` 与 `successors` 中，使用非拥有裸指针引用
 * 同一个 `CodeUnit` 持有的其他 block。
 *
 * 结构约束如下：
 * - 终结类指令与普通指令统一建模。
 * - 如果存在终结类指令，则它必须是最后一条指令。
 * - 第一版 verifier 还应进一步保证块内除最后一条外不存在其他终结类指令。
 */
struct BasicBlock {
    CodeUnit* parent = nullptr;
    InternedString label;
    SourceSpan source_span;
    std::vector<std::unique_ptr<Instruction>> instructions;
    std::vector<BasicBlock*> predecessors;
    std::vector<BasicBlock*> successors;

    /**
     * @brief 判断当前 block 是否包含指令。
     *
     * @return `instructions.empty()` 的结果取反。
     */
    [[nodiscard]] bool has_instructions() const noexcept {
        return !instructions.empty();
    }

    /**
     * @brief 判断当前 block 是否有前驱块。
     *
     * @return `predecessors.empty()` 的结果取反。
     */
    [[nodiscard]] bool has_predecessors() const noexcept {
        return !predecessors.empty();
    }

    /**
     * @brief 判断当前 block 是否有后继块。
     *
     * @return `successors.empty()` 的结果取反。
     */
    [[nodiscard]] bool has_successors() const noexcept {
        return !successors.empty();
    }

    /**
     * @brief 获取当前 block 的终结指令。
     *
     * 若最后一条指令不是终结类指令，则返回 `nullptr`。
     *
     * @return 当前 block 的终结指令指针，或 `nullptr`。
     */
    [[nodiscard]] Instruction* terminator() noexcept {
        return const_cast<Instruction*>(std::as_const(*this).terminator());
    }

    /**
     * @brief 获取当前 block 的终结指令。
     *
     * 若最后一条指令不是终结类指令，则返回 `nullptr`。
     *
     * @return 当前 block 的终结指令指针，或 `nullptr`。
     */
    [[nodiscard]] const Instruction* terminator() const noexcept {
        if (instructions.empty()) {
            return nullptr;
        }

        const Instruction* last = instructions.back().get();
        return last != nullptr && last->is_terminator() ? last : nullptr;
    }

    /**
     * @brief 判断当前 block 是否以终结指令结束。
     *
     * @return 最后一条指令存在且为终结类指令时返回 true。
     */
    [[nodiscard]] bool has_terminator() const noexcept {
        return terminator() != nullptr;
    }
};

} // namespace baltam
