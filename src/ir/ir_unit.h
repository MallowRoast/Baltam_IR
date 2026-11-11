#pragma once

#include "ir/ir_inst.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace baltam {

class IRModule;
class IRStageVersion;
class IRUnit;
class FunctionUnit;

enum class IRStage : std::uint8_t { NonSSA, SSA, OptimizedSSA };

struct SharedSlotRef {
    FunctionUnit* function; // 最终拥有变量的词法外层函数，非拥有引用。
    SlotId slot;            // 目标函数 SlotTable 中的编号。
};

struct SlotInfo {
    InternedString name;
    bool is_internal = false; // 编译器临时存储，不属于用户工作区。
    std::optional<ba_type> type;
    std::optional<std::vector<std::optional<std::size_t>>> shape;
    // 声明标记不代表当前执行已经建立对应绑定。
    bool has_global_decl = false;
    bool has_persistent_decl = false;
    bool shared_by_nested = false;              // 当前函数拥有，且被嵌套函数共享。
    std::optional<SharedSlotRef> shared_target; // 引用外层拥有的共享槽位。
};

// 槽位编号只在所属代码单元内有效，表项不重复保存编号。
class SlotTable final {
public:
    [[nodiscard]] SlotId add_slot(InternedString name, bool is_internal = false);
    [[nodiscard]] const SlotInfo& find(SlotId id) const;
    [[nodiscard]] SlotInfo& find(SlotId id);
    [[nodiscard]] std::size_t size() const noexcept { return slots_.size(); }

private:
    std::vector<SlotInfo> slots_;
};

// 每个阶段独占拥有稳定的值对象，允许先预留引用、再补齐定义。
class ValueTable final {
public:
    explicit ValueTable(IRStageVersion& version) : version_(&version) {}
    ValueTable(const ValueTable&) = delete;
    ValueTable& operator=(const ValueTable&) = delete;
    ValueTable(ValueTable&&) = delete;
    ValueTable& operator=(ValueTable&&) = delete;

    [[nodiscard]] Value& reserve(ValueId id);
    [[nodiscard]] const Value& find(ValueId id) const;
    [[nodiscard]] Value& find(ValueId id);
    void define_parameter(ValueId id, std::uint32_t index);

    void define(ValueId id, Instruction& definition, std::uint32_t result_index = 0,
                Value::Form form = Value::Form::Single, bool constant = false);
    // 包含构建中的预留记录和删除后留下的空位。
    [[nodiscard]] std::size_t size() const noexcept { return values_.size(); }

private:
    friend class IRStageVersion;
    IRStageVersion* version_;
    std::vector<std::unique_ptr<Value>> values_;
};

// 当前固定基本块身份与指令所有权；控制流关系另行设计。
class BasicBlock final {
public:
    [[nodiscard]] IRUnit* parent() const noexcept { return parent_; }

    [[nodiscard]] const std::string& label() const noexcept { return label_; }
    // 仅供阅读的来源角色，不参与控制流语义。
    [[nodiscard]] const std::string& description() const noexcept { return description_; }
    void set_description(std::string description) { description_ = std::move(description); }

    [[nodiscard]] const std::vector<std::unique_ptr<Instruction>>& instructions() const noexcept {
        return instructions_;
    }

    [[nodiscard]] const Instruction* terminator() const noexcept {
        return !instructions_.empty() && instructions_.back()->is_terminator()
                   ? instructions_.back().get()
                   : nullptr;
    }

    template <typename Inst, typename... Args> Inst& add_instruction(Args&&... args) {
        return insert_instruction<Inst>(instructions_.size(), std::forward<Args>(args)...);
    }

    // 插入后仍由调用方通过 add_value 登记结果，既有指令地址保持稳定。
    template <typename Inst, typename... Args>
    Inst& insert_instruction(std::size_t position, Args&&... args) {
        static_assert(std::is_base_of_v<Instruction, Inst>, "指令类型必须继承 Instruction");
        std::unique_ptr<Instruction> instruction =
            std::make_unique<Inst>(std::forward<Args>(args)...);
        return static_cast<Inst&>(insert_instruction(position, std::move(instruction)));
    }

    // 删除前要求所有已登记结果不再被使用；保留值编号，清除对应值表项。
    void erase_instruction(std::size_t position);

private:
    friend class IRStageVersion;
    friend class ValueTable;

    Instruction& insert_instruction(std::size_t position, std::unique_ptr<Instruction> instruction);

    BasicBlock(IRUnit& parent, IRStageVersion& version, std::string label)
        : parent_(&parent), version_(&version), label_(std::move(label)) {}

    IRUnit* parent_;
    IRStageVersion* version_;
    std::string label_;
    std::string description_;
    std::vector<std::unique_ptr<Instruction>> instructions_;
};

// 基本块和值表共同存活，供执行者持有正在使用的阶段版本。
class IRStageVersion final {
public:
    IRStageVersion() : values_(*this) {}

    ~IRStageVersion();
    IRStageVersion(const IRStageVersion&) = delete;
    IRStageVersion& operator=(const IRStageVersion&) = delete;
    IRStageVersion(IRStageVersion&&) = delete;
    IRStageVersion& operator=(IRStageVersion&&) = delete;
    [[nodiscard]] BasicBlock* entry_block() const noexcept { return entry_block_; }

    [[nodiscard]] const std::vector<std::unique_ptr<BasicBlock>>& blocks() const noexcept {
        return blocks_;
    }

    [[nodiscard]] ValueTable& values() noexcept { return values_; }

    [[nodiscard]] const ValueTable& values() const noexcept { return values_; }

    [[nodiscard]] ValueId add_value(Instruction& definition, std::uint32_t result_index = 0,
                                    Value::Form form = Value::Form::Single, bool constant = false);

    [[nodiscard]] ValueId add_parameter(std::uint32_t index);

    [[nodiscard]] std::vector<ValueId> parameters() const;

    [[nodiscard]] std::shared_ptr<IRStageVersion> clone(IRUnit& owner) const;

    // 仅用于与基线结构和编号相同的初始克隆；变换后调用 compact_values 归一来源。
    void initialize_deopt(const IRStageVersion* baseline);

    [[nodiscard]] const IRStageVersion* deopt_baseline() const noexcept { return deopt_baseline_; }

    [[nodiscard]] std::optional<ValueId> deopt_source(ValueId baseline_value) const;

    [[nodiscard]] std::size_t return_count() const noexcept { return return_count_; }

    void set_return_count(std::size_t count) noexcept { return_count_ = count; }

    // 删除留下的空位统一压缩，同时更新结果编号、输入和值表。
    void compact_values();

    // 仅重排拥有顺序，不移动基本块对象；入口必须仍位于首位。
    void reorder_blocks(const std::vector<BasicBlock*>& order);

    // 删除入口不可达块，并按实际前驱边修复保留块的 PHI。
    bool remove_unreachable_blocks();

    // 合并 goto 的唯一前驱后继；无法安全消除的 PHI 保守保留整个块。
    bool merge_single_successor(BasicBlock& block);

    // 调用方保证替换不破坏类型、支配关系及阶段语义。
    std::size_t replace_all_uses(ValueId from, ValueId to);

    BasicBlock& add_block(IRUnit& parent, std::string label = {}) {
        auto block = std::unique_ptr<BasicBlock>(new BasicBlock(parent, *this, std::move(label)));
        blocks_.push_back(std::move(block));
        if (!entry_block_) {
            entry_block_ = blocks_.back().get();
        }
        return *blocks_.back();
    }

private:
    friend class BasicBlock;

    void erase_instruction(BasicBlock& block, std::size_t position);

    BasicBlock* entry_block_ = nullptr;
    std::vector<std::unique_ptr<BasicBlock>> blocks_;
    ValueTable values_;
    std::size_t return_count_ = 0;
    // 非拥有引用；所属函数须保证基线在 SSA 使用期间存活且不被替换。
    const IRStageVersion* deopt_baseline_ = nullptr;
    std::vector<std::optional<ValueId>> baseline_to_ssa_;
    std::vector<std::optional<ValueId>> deopt_replacements_;
};

// 每个代码单元拥有一张跨阶段共用的槽位表。
class IRUnit {
public:
    virtual ~IRUnit() = default;
    [[nodiscard]] IRModule* parent() const noexcept { return parent_; }

    void set_parent(IRModule* parent) noexcept { parent_ = parent; }

    [[nodiscard]] SlotTable& slot_table() noexcept { return slot_table_; }

    [[nodiscard]] const SlotTable& slot_table() const noexcept { return slot_table_; }

protected:
    IRUnit() = default;

private:
    IRModule* parent_ = nullptr;
    SlotTable slot_table_;
};

class CommandUnit final : public IRUnit {
public:
    CommandUnit() = default;

    [[nodiscard]] const std::vector<std::unique_ptr<BasicBlock>>& blocks() const noexcept {
        return version_->blocks();
    }

    [[nodiscard]] std::shared_ptr<IRStageVersion> stage_version() noexcept { return version_; }

    [[nodiscard]] std::shared_ptr<const IRStageVersion> stage_version() const noexcept {
        return version_;
    }
    BasicBlock& add_block() { return version_->add_block(*this); }
    void set_stage_version(std::shared_ptr<IRStageVersion> version) {
        if (!version)
            throw std::invalid_argument("阶段版本不能为空");
        for (const auto& block : version->blocks())
            if (block->parent() != this)
                throw std::invalid_argument("阶段版本不属于当前命令单元");
        version_ = std::move(version);
    }

private:
    std::shared_ptr<IRStageVersion> version_ = std::make_shared<IRStageVersion>();
};

enum class FunctionType : std::uint8_t { Main, Local, Method, Nested };

class FunctionUnit final : public IRUnit {
public:
    using IRStage = baltam::IRStage;
    FunctionUnit(std::string name, std::vector<std::string> parameters,
                 std::vector<std::string> returns);

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] FunctionType function_type() const noexcept;
    void set_function_type(FunctionType type) noexcept { function_type_ = type; }

    [[nodiscard]] IRStage current_stage() const noexcept { return current_stage_; }

    void set_current_stage(IRStage stage) noexcept { current_stage_ = stage; }
    // 仅发布属于当前函数的完整阶段版本。
    void set_stage_version(IRStage stage, std::shared_ptr<IRStageVersion> version);

    [[nodiscard]] const std::vector<SlotId>& parameter_slots() const noexcept {
        return parameter_slots_;
    }

    [[nodiscard]] const std::vector<SlotId>& return_slots() const noexcept { return return_slots_; }

    [[nodiscard]] const std::vector<std::unique_ptr<FunctionUnit>>&
    nested_functions() const noexcept {
        return nested_functions_;
    }
    FunctionUnit& append_nested_function(std::unique_ptr<FunctionUnit> function);

    // 在函数树建立后，由词法绑定分析按从外到内的顺序登记共享引用。
    void bind_shared_slot(SlotId local, FunctionUnit& owner, SlotId target);

    [[nodiscard]] FunctionUnit* lexical_parent() const noexcept { return lexical_parent_; }

    [[nodiscard]] std::shared_ptr<IRStageVersion> stage_version(IRStage stage);
    [[nodiscard]] std::shared_ptr<const IRStageVersion> stage_version(IRStage stage) const;
    [[nodiscard]] std::shared_ptr<IRStageVersion> current_version();
    [[nodiscard]] std::shared_ptr<const IRStageVersion> current_version() const;

private:
    std::string name_;
    std::optional<FunctionType> function_type_;
    FunctionUnit* lexical_parent_ = nullptr;
    IRStage current_stage_ = IRStage::NonSSA;
    std::vector<SlotId> parameter_slots_;
    std::vector<SlotId> return_slots_;
    std::shared_ptr<IRStageVersion> non_ssa_version_ = std::make_shared<IRStageVersion>();
    std::shared_ptr<IRStageVersion> ssa_version_ = std::make_shared<IRStageVersion>();
    std::shared_ptr<IRStageVersion> optimized_ssa_version_ = std::make_shared<IRStageVersion>();
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

    [[nodiscard]] CommandUnit* command_unit() noexcept { return command_unit_.get(); }

    [[nodiscard]] const CommandUnit* command_unit() const noexcept { return command_unit_.get(); }

    [[nodiscard]] const FunctionUnit* entry_function() const noexcept {
        return module_type_ == ModuleType::FunctionFile && !functions_.empty()
                   ? functions_.front().get()
                   : nullptr;
    }

    [[nodiscard]] const std::vector<std::unique_ptr<FunctionUnit>>& functions() const noexcept {
        return functions_;
    }
    // 只查找模块直接拥有的函数；不遍历嵌套函数，也不执行 M 语言的可见性检查。
    [[nodiscard]] const FunctionUnit* find_function(std::string_view key) const;

    void set_command_unit(std::unique_ptr<CommandUnit>&& unit);
    // 按传入顺序保存函数；函数文件的主函数由调用处放在首位。
    void
    set_functions(std::vector<std::pair<std::string, std::unique_ptr<FunctionUnit>>>&& functions);

private:
    SourceType source_type_;
    ModuleType module_type_;
    std::optional<std::filesystem::path> path_;

    std::unique_ptr<CommandUnit> command_unit_;
    std::vector<std::unique_ptr<FunctionUnit>> functions_;
    std::map<std::string, FunctionUnit*, std::less<>> function_map;
};

} // baltam 命名空间
