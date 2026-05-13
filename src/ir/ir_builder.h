#pragma once

#include "ir/ir_units.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace baltam {

/**
 * @brief IRBuilder 诊断信息。
 */
struct IRBuildDiagnostic {
    /**
     * @brief 诊断级别。
     */
    enum Severity : std::uint8_t {
        Log,
        Warning,
        Error,
    };

    Severity severity = Error;
    InternedString message;
    SourceSpan source_span;
};

/**
 * @brief IRBuilder 的最终返回结果。
 */
struct IRBuildResult {
    std::unique_ptr<MFileUnit> mfile;
    std::vector<IRBuildDiagnostic> diagnostics;
};

/**
 * @brief 每个 `CodeUnit` 对应一份 builder 状态。
 */
struct IRUnitBuildState {
    /**
     * @brief 当前 unit 内的 ID 分配器。
     *
     * 第一版 `SlotId` 与 `ValueId` 都按 unit 局部递增分配。
     */
    struct IRIdAllocator {
        SlotId::underlying_type next_slot = 0;
        ValueId::underlying_type next_value = 0;

        /**
         * @brief 分配一个新的 `SlotId`。
         */
        [[nodiscard]] SlotId allocate_slot() noexcept {
            return SlotId(next_slot++);
        }

        /**
         * @brief 分配一个新的 `ValueId`。
         */
        [[nodiscard]] ValueId allocate_value() noexcept {
            return ValueId(next_value++);
        }

        /**
         * @brief 重置分配器。
         */
        void reset() noexcept {
            next_slot = 0;
            next_value = 0;
        }
    };

    CodeUnit* unit = nullptr;
    BasicBlock* current_block = nullptr;
    IRIdAllocator ids;
    /**
     * @brief lowering 期名字到 slot 的绑定表。
     *
     * 该表不属于最终 IR，只记录当前 unit 在 lowering 过程中已经确定为变量语义的名字。
     * 函数 lowering 会根据它判断源码中的 `A(...)` 应保留为 `apply`，还是收敛为直接
     * `call`。
     */
    std::unordered_map<InternedString, SlotId> name_bindings;
};

/**
 * @brief 第一版原始 IR 构造器。
 *
 * 该构造器直接创建最终 IR 对象，并用 side table 维护构建期状态。
 * 它不负责 AST 遍历本身，只负责把上层 lowering 决策安全地落到 IR 上。
 */
class IRBuilder final {
public:
    IRBuilder() = default;

    IRBuilder(const IRBuilder&) = delete;
    IRBuilder& operator=(const IRBuilder&) = delete;

    IRBuilder(IRBuilder&&) = default;
    IRBuilder& operator=(IRBuilder&&) = default;

    /**
     * @brief 重置 builder 到初始状态。
     */
    void reset() noexcept;

    /**
     * @brief 开始构建一个文件级单元。
     */
    MFileUnit& begin_file(NormalizedPath path);

    /**
     * @brief 在当前文件下开始一个脚本单元。
     */
    ScriptUnit& begin_script_unit(std::string_view name, SourceSpan source_span);

    /**
     * @brief 在当前文件下开始一个函数单元。
     */
    FunctionUnit& begin_function_unit(std::string_view name, SourceSpan source_span);

    /**
     * @brief 在当前文件下开始一个匿名函数体单元。
     */
    AnonymousFunctionUnit& begin_anonymous_function_unit(SourceSpan source_span);

    /**
     * @brief 切换当前活动代码单元。
     */
    void set_current_unit(CodeUnit* unit);

    /**
     * @brief 设置当前指令插入点。
     */
    void set_insert_point(BasicBlock* block);

    /**
     * @brief 获取当前 unit。
     */
    [[nodiscard]] CodeUnit* current_unit() noexcept;

    /**
     * @brief 获取当前 unit。
     */
    [[nodiscard]] const CodeUnit* current_unit() const noexcept;

    /**
     * @brief 获取当前 block。
     */
    [[nodiscard]] BasicBlock* current_block() noexcept;

    /**
     * @brief 获取当前 block。
     */
    [[nodiscard]] const BasicBlock* current_block() const noexcept;

    /**
     * @brief 为当前 unit 创建一个 slot。
     */
    [[nodiscard]] SlotId create_slot(
        Slot::Type type,
        std::string_view name,
        SourceSpan source_span,
        SlotAttrs attrs = {});

    /**
     * @brief 为当前 unit 创建一个 hidden slot。
     */
    [[nodiscard]] SlotId create_hidden_slot(
        std::string_view name,
        SlotAttrs::HiddenRole role,
        SourceSpan source_span);

    /**
     * @brief 为当前 unit 分配一个新的 `ValueId`。
     */
    [[nodiscard]] ValueId create_value();

    /**
     * @brief 为当前文件分配一个匿名函数 ID。
     */
    [[nodiscard]] AnonymousFunctionId create_anonymous_function_id();

    /**
     * @brief 绑定一个名字到当前 unit 的名字表。
     */
    void bind_name(std::string_view name, SlotId slot_id);

    /**
     * @brief 在当前 unit 中查找名字绑定。
     */
    [[nodiscard]] SlotId* find_name(std::string_view name) noexcept;

    /**
     * @brief 在当前 unit 中查找名字绑定。
     */
    [[nodiscard]] const SlotId* find_name(std::string_view name) const noexcept;

    /**
     * @brief 向当前 block 追加一条指令。
     */
    void append_instruction(std::unique_ptr<Instruction> instruction);

    /**
     * @brief 结束构建并返回结果。
     */
    [[nodiscard]] IRBuildResult finish();

    /**
     * @brief 追加一条 builder 诊断。
     */
    void report(
        IRBuildDiagnostic::Severity severity,
        std::string_view message,
        SourceSpan source_span = SourceSpan::invalid());

    /**
     * @brief 获取当前累积的诊断列表。
     */
    [[nodiscard]] const std::vector<IRBuildDiagnostic>& diagnostics() const noexcept {
        return diagnostics_;
    }

private:
    template <typename UnitT>
    UnitT& begin_unit(
        std::string_view name,
        SourceSpan source_span,
        std::string_view missing_file_message);

    std::unique_ptr<MFileUnit> owned_file_;
    std::unordered_map<CodeUnit*, std::unique_ptr<IRUnitBuildState>> unit_states_;
    IRUnitBuildState* current_unit_state_ = nullptr;
    AnonymousFunctionId::underlying_type next_anonymous_function_ = 0;
    std::vector<IRBuildDiagnostic> diagnostics_;
};

} // namespace baltam
