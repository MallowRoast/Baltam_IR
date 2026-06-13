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
 * @brief slot 表项。
 */
struct SlotInfo {
    Slot slot;
    InternedString name;
    SourceSpan source_span;
    SlotValueType value_type = SlotValueType::Unknown;
};

/**
 * @brief `CodeUnit` 的 slot 表。
 */
struct SlotTable {
    std::vector<SlotInfo> slots;

    [[nodiscard]] bool empty() const noexcept {
        return slots.empty();
    }

    [[nodiscard]] SlotInfo* find_slot(Slot slot) noexcept {
        return const_cast<SlotInfo*>(std::as_const(*this).find_slot(slot));
    }

    [[nodiscard]] const SlotInfo* find_slot(Slot slot) const noexcept {
        if (!slot.is_valid()) {
            return nullptr;
        }
        for (const SlotInfo& info : slots) {
            if (info.slot == slot) {
                return &info;
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
