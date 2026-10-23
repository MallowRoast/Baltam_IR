#pragma once

#include "ir/ir_inst.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace baltam {

class IRModule;
class IRStageVersion;

enum class IRStage : std::uint8_t { NonSSA, SSA, OptimizedSSA };

enum class SlotValueType : std::uint8_t {
    Unknown,
    Int64Scalar,
    LogicalScalar,
};

struct SlotInfo {
    InternedString name;
    SlotTag tag = SlotTag::Local;
    SlotValueType fixed_type = SlotValueType::Unknown;
};

// 槽位编号只在所属代码单元内有效，表项不重复保存编号。
class SlotTable final {
public:
    [[nodiscard]] SlotId add_slot(InternedString name, SlotTag tag,
                                  SlotValueType fixed_type = SlotValueType::Unknown);
    [[nodiscard]] const SlotInfo* find(SlotId id) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return slots_.size(); }

private:
    std::vector<SlotInfo> slots_;
};

struct ValueInfo {
    Instruction* definition = nullptr; // 同一阶段版本的基本块拥有该指令。
    std::uint32_t result_index = 0;
    // 仅记录是否可能承载逗号分隔列表，不表示运行时元素数量。
    enum class Form : std::uint8_t { Single, MaybePack } form = Form::Single;
    // 索引冒号是固定占位符，不能作为普通对象常量参与折叠。
    enum class ConstantKind : std::uint8_t {
        Unknown,
        Object,
        IndexColon,
    } constant_kind = ConstantKind::Unknown;
};

// 值编号只在所属代码单元的当前阶段版本内有效。
class ValueTable final {
public:
    [[nodiscard]] ValueId reserve_results(std::size_t count = 1);
    void bind(ValueId id, Instruction& definition, std::uint32_t result_index = 0,
              ValueInfo::Form form = ValueInfo::Form::Single);
    void discard_last(ValueId first, std::size_t count = 1);
    [[nodiscard]] const ValueInfo* find(ValueId id) const noexcept;
    [[nodiscard]] bool all_defined() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return values_.size(); }

private:
    std::vector<ValueInfo> values_;
};

// 当前固定基本块身份与指令所有权；控制流关系另行设计。
class BasicBlock final {
public:
    [[nodiscard]] std::size_t index() const noexcept { return index_; }
    [[nodiscard]] const std::vector<std::unique_ptr<Instruction>>&
    instructions() const noexcept { return instructions_; }
    [[nodiscard]] const Instruction* terminator() const noexcept {
        return !instructions_.empty() && instructions_.back()->is_terminator()
                   ? instructions_.back().get() : nullptr;
    }

    template <typename Inst, typename... Args>
    Inst& add_instruction(Args&&... args) {
        static_assert(std::is_base_of_v<Instruction, Inst>,
                      "指令类型必须继承 Instruction");
        if (terminator()) {
            throw std::logic_error("终结指令之后不能添加指令");
        }
        auto instruction = std::make_unique<Inst>(std::forward<Args>(args)...);
        Inst& added = *instruction;
        instructions_.push_back(std::move(instruction));
        return added;
    }

private:
    friend class IRStageVersion;

    explicit BasicBlock(std::size_t index) : index_(index) {}

    std::size_t index_;
    std::vector<std::unique_ptr<Instruction>> instructions_;
};

// 基本块和值表共同存活，供执行者持有正在使用的阶段版本。
class IRStageVersion final {
public:
    explicit IRStageVersion(IRStage stage) : stage_(stage) {}

    [[nodiscard]] IRStage stage() const noexcept { return stage_; }
    [[nodiscard]] const std::vector<std::unique_ptr<BasicBlock>>&
    blocks() const noexcept { return blocks_; }
    [[nodiscard]] ValueTable& values() noexcept { return values_; }
    [[nodiscard]] const ValueTable& values() const noexcept { return values_; }
    BasicBlock& add_block() {
        auto block = std::unique_ptr<BasicBlock>(new BasicBlock(blocks_.size()));
        blocks_.push_back(std::move(block));
        return *blocks_.back();
    }

private:
    std::vector<std::unique_ptr<BasicBlock>> blocks_;
    ValueTable values_;
    IRStage stage_;
};

// 每个代码单元拥有一张跨阶段共用的槽位表。
class IRUnit {
public:
    virtual ~IRUnit() = default;
    [[nodiscard]] SlotTable& slot_table() noexcept { return slot_table_; }
    [[nodiscard]] const SlotTable& slot_table() const noexcept {
        return slot_table_;
    }

protected:
    IRUnit() = default;

private:
    SlotTable slot_table_;
};

class CommandUnit final : public IRUnit {
public:
    CommandUnit() = default;

    [[nodiscard]] const std::vector<std::unique_ptr<BasicBlock>>&
    blocks() const noexcept { return version_->blocks(); }
    [[nodiscard]] std::shared_ptr<IRStageVersion>
    stage_version() noexcept { return version_; }
    [[nodiscard]] std::shared_ptr<const IRStageVersion>
    stage_version() const noexcept { return version_; }
    BasicBlock& add_block() { return version_->add_block(); }

private:
    std::shared_ptr<IRStageVersion> version_ =
        std::make_shared<IRStageVersion>(IRStage::NonSSA);
};

class FunctionUnit final : public IRUnit {
public:
    FunctionUnit(std::string name, std::vector<std::string> parameters,
                 std::vector<std::string> returns);

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] const std::vector<SlotId>& parameter_slots() const noexcept {
        return parameter_slots_;
    }
    [[nodiscard]] const std::vector<SlotId>& return_slots() const noexcept {
        return return_slots_;
    }

    [[nodiscard]] const std::vector<std::unique_ptr<FunctionUnit>>&
    nested_functions() const noexcept {
        return nested_functions_;
    }
    [[nodiscard]] const std::vector<std::unique_ptr<BasicBlock>>&
    non_ssa_blocks() const noexcept { return non_ssa_version_->blocks(); }
    [[nodiscard]] const std::vector<std::unique_ptr<BasicBlock>>&
    ssa_blocks() const noexcept { return ssa_version_->blocks(); }
    [[nodiscard]] const std::vector<std::unique_ptr<BasicBlock>>&
    optimized_ssa_blocks() const noexcept { return optimized_ssa_version_->blocks(); }
    [[nodiscard]] std::shared_ptr<IRStageVersion>
    stage_version(IRStage stage);
    [[nodiscard]] std::shared_ptr<const IRStageVersion>
    stage_version(IRStage stage) const;
    void replace_stage_version(IRStage stage,
                               std::shared_ptr<IRStageVersion> version);
    FunctionUnit& add_nested_function(std::string name,
                                      std::vector<std::string> parameters,
                                      std::vector<std::string> returns);
    BasicBlock& add_block(IRStage stage);

private:
    std::string name_;
    std::vector<SlotId> parameter_slots_;
    std::vector<SlotId> return_slots_;
    std::shared_ptr<IRStageVersion> non_ssa_version_ =
        std::make_shared<IRStageVersion>(IRStage::NonSSA);
    std::shared_ptr<IRStageVersion> ssa_version_ =
        std::make_shared<IRStageVersion>(IRStage::SSA);
    std::shared_ptr<IRStageVersion> optimized_ssa_version_ =
        std::make_shared<IRStageVersion>(IRStage::OptimizedSSA);
    std::vector<std::unique_ptr<FunctionUnit>> nested_functions_;
};

// 匿名函数体是独立代码单元；参数与捕获槽位均属于该函数体。
class AnonymousFunctionUnit final : public IRUnit {
public:
    AnonymousFunctionUnit(IRUnit* lexical_parent,
                          std::vector<Slot> param_slots,
                          std::vector<Slot> capture_slots)
        : lexical_parent_(lexical_parent),
          param_slots_(std::move(param_slots)),
          capture_slots_(std::move(capture_slots)) {
        if (!lexical_parent_) {
            throw std::invalid_argument("匿名函数体的词法父单元不能为空");
        }
        for (Slot slot : capture_slots_) {
            if (slot.tag != SlotTag::Capture) {
                throw std::invalid_argument("匿名函数体的捕获槽位无效");
            }
        }
    }

    [[nodiscard]] IRUnit* lexical_parent() const noexcept {
        return lexical_parent_;
    }
    [[nodiscard]] const std::vector<Slot>& param_slots() const noexcept {
        return param_slots_;
    }
    [[nodiscard]] const std::vector<Slot>& capture_slots() const noexcept {
        return capture_slots_;
    }
    [[nodiscard]] const std::vector<std::unique_ptr<BasicBlock>>&
    non_ssa_blocks() const noexcept { return non_ssa_version_->blocks(); }
    [[nodiscard]] const std::vector<std::unique_ptr<BasicBlock>>&
    ssa_blocks() const noexcept { return ssa_version_->blocks(); }
    [[nodiscard]] const std::vector<std::unique_ptr<BasicBlock>>&
    optimized_ssa_blocks() const noexcept { return optimized_ssa_version_->blocks(); }
    [[nodiscard]] std::shared_ptr<IRStageVersion>
    stage_version(IRStage stage);
    [[nodiscard]] std::shared_ptr<const IRStageVersion>
    stage_version(IRStage stage) const;
    void replace_stage_version(IRStage stage,
                               std::shared_ptr<IRStageVersion> version);

    BasicBlock& add_block(IRStage stage);

private:
    IRUnit* lexical_parent_;
    std::vector<Slot> param_slots_;
    std::vector<Slot> capture_slots_;
    std::shared_ptr<IRStageVersion> non_ssa_version_ =
        std::make_shared<IRStageVersion>(IRStage::NonSSA);
    std::shared_ptr<IRStageVersion> ssa_version_ =
        std::make_shared<IRStageVersion>(IRStage::SSA);
    std::shared_ptr<IRStageVersion> optimized_ssa_version_ =
        std::make_shared<IRStageVersion>(IRStage::OptimizedSSA);
};

} // baltam 命名空间
