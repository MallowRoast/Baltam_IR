#pragma once

#include "ir/ir_inst.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <filesystem>
#include <map>
#include <optional>
#include <string_view>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace baltam {

class IRModule;
class IRStageVersion;
class IRUnit;

enum class IRStage : std::uint8_t { NonSSA, SSA, OptimizedSSA };

struct SlotInfo {
    InternedString name;
};

// 槽位编号只在所属代码单元内有效，表项不重复保存编号。
class SlotTable final {
public:
    [[nodiscard]] SlotId add_slot(InternedString name);
    [[nodiscard]] const SlotInfo& find(SlotId id) const;
    [[nodiscard]] std::size_t size() const noexcept { return slots_.size(); }

private:
    std::vector<SlotInfo> slots_;
};

struct ValueInfo {
    Instruction* def = nullptr; // 同一阶段版本的基本块拥有该指令。
    std::uint32_t index = 0; // 定义指令的第几个结果，从 0 开始。
    // 仅记录是否可能承载逗号分隔列表，不表示运行时元素数量。
    enum class Form : std::uint8_t { Single, MaybePack } form = Form::Single;
    bool is_constant = false;
};

// 值编号只在所属代码单元的当前阶段版本内有效。
class ValueTable final {
public:
    [[nodiscard]] const ValueInfo& find(ValueId id) const;
    [[nodiscard]] std::size_t size() const noexcept { return values_.size(); }

private:
    friend class IRStageVersion;
    [[nodiscard]] ValueId add_value(ValueInfo info);
    std::vector<ValueInfo> values_;
};

// 当前固定基本块身份与指令所有权；控制流关系另行设计。
class BasicBlock final {
public:
    [[nodiscard]] IRUnit* parent() const noexcept { return parent_; }
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
        added.parent_ = this;
        return added;
    }

private:
    friend class IRStageVersion;

    explicit BasicBlock(IRUnit& parent) : parent_(&parent) {}

    IRUnit* parent_;
    std::vector<std::unique_ptr<Instruction>> instructions_;
};

// 基本块和值表共同存活，供执行者持有正在使用的阶段版本。
class IRStageVersion final {
public:
    IRStageVersion() = default;
    [[nodiscard]] BasicBlock* entry_block() const noexcept { return entry_block_; }
    [[nodiscard]] const std::vector<std::unique_ptr<BasicBlock>>&
    blocks() const noexcept { return blocks_; }
    [[nodiscard]] ValueTable& values() noexcept { return values_; }
    [[nodiscard]] const ValueTable& values() const noexcept { return values_; }
    [[nodiscard]] ValueId add_value(ValueInfo info);
    BasicBlock& add_block(IRUnit& parent) {
        auto block = std::unique_ptr<BasicBlock>(new BasicBlock(parent));
        blocks_.push_back(std::move(block));
        if (!entry_block_) {
            entry_block_ = blocks_.back().get();
        }
        return *blocks_.back();
    }

private:
    BasicBlock* entry_block_ = nullptr;
    std::vector<std::unique_ptr<BasicBlock>> blocks_;
    ValueTable values_;
};

// 每个代码单元拥有一张跨阶段共用的槽位表。
class IRUnit {
public:
    virtual ~IRUnit() = default;
    [[nodiscard]] IRModule* parent() const noexcept { return parent_; }
    void set_parent(IRModule* parent) noexcept { parent_ = parent; }
    [[nodiscard]] SlotTable& slot_table() noexcept { return slot_table_; }
    [[nodiscard]] const SlotTable& slot_table() const noexcept {
        return slot_table_;
    }

protected:
    IRUnit() = default;

private:
    IRModule* parent_ = nullptr;
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
    BasicBlock& add_block() { return version_->add_block(*this); }

private:
    std::shared_ptr<IRStageVersion> version_ =
        std::make_shared<IRStageVersion>();
};

class FunctionUnit final : public IRUnit {
public:
    using IRStage = baltam::IRStage;
    FunctionUnit(std::string name, std::vector<std::string> parameters,
                 std::vector<std::string> returns);

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] IRStage current_stage() const noexcept { return current_stage_; }
    void set_current_stage(IRStage stage) noexcept { current_stage_ = stage; }
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
    FunctionUnit& append_nested_function(std::unique_ptr<FunctionUnit> function);
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
    [[nodiscard]] std::shared_ptr<IRStageVersion> current_version();
    [[nodiscard]] std::shared_ptr<const IRStageVersion> current_version() const;

private:
    std::string name_;
    IRStage current_stage_ = IRStage::NonSSA;
    std::vector<SlotId> parameter_slots_;
    std::vector<SlotId> return_slots_;
    std::shared_ptr<IRStageVersion> non_ssa_version_ =
        std::make_shared<IRStageVersion>();
    std::shared_ptr<IRStageVersion> ssa_version_ =
        std::make_shared<IRStageVersion>();
    std::shared_ptr<IRStageVersion> optimized_ssa_version_ =
        std::make_shared<IRStageVersion>();
    std::vector<std::unique_ptr<FunctionUnit>> nested_functions_;
};

enum class SourceType : std::uint8_t { File, Command, Memory };
enum class ModuleType : std::uint8_t { Script, FunctionFile, ClassFile, Command };

// 一个模块对应一次源码输入生成的 IR；所有权树发布后不再修改。
class IRModule final {
public:
    IRModule(SourceType source_type, ModuleType module_type,
             std::optional<std::filesystem::path> source_path);

    IRModule(const IRModule&) = delete;
    IRModule& operator=(const IRModule&) = delete;
    IRModule(IRModule&&) = delete;
    IRModule& operator=(IRModule&&) = delete;

    [[nodiscard]] SourceType source_type() const noexcept { return source_type_; }
    [[nodiscard]] ModuleType module_type() const noexcept { return module_type_; }
    [[nodiscard]] const std::optional<std::filesystem::path>& source_path() const noexcept {
        return path_;
    }
    [[nodiscard]] const CommandUnit* command_unit() const noexcept {
        return command_unit_.get();
    }
    [[nodiscard]] const FunctionUnit* entry_function() const noexcept {
        return module_type_ == ModuleType::FunctionFile && !functions_.empty()
                   ? functions_.front().get() : nullptr;
    }
    [[nodiscard]] const std::vector<std::unique_ptr<FunctionUnit>>&
    functions() const noexcept {
        return functions_;
    }
    // 只查找模块直接拥有的函数；不遍历嵌套函数，也不执行 M 语言的可见性检查。
    [[nodiscard]] const FunctionUnit* find_function(std::string_view key) const;

    void set_command_unit(std::unique_ptr<CommandUnit>&& unit);
    // 按传入顺序保存函数；函数文件的主函数由调用处放在首位。
    void set_functions(std::vector<std::pair<std::string, std::unique_ptr<FunctionUnit>>>&& functions);
private:
    SourceType source_type_;
    ModuleType module_type_;
    std::optional<std::filesystem::path> path_;

    std::unique_ptr<CommandUnit> command_unit_;
    std::vector<std::unique_ptr<FunctionUnit>> functions_;
    std::map<std::string, FunctionUnit*, std::less<>> function_map;
};

} // baltam 命名空间
