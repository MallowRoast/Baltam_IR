#pragma once

#include "bt_ast_interface/ast/ast_base.h"
#include "ir/ir_unit.h"

#include <string>
#include <unordered_map>

namespace baltam {

struct LoopControlContext {
    BasicBlock* break_target;
    BasicBlock* continue_target;
};

// AST lowering 时维护；不属于生成后的 IR。
class LoopControlStack final {
public:
    void push(BasicBlock& break_target, BasicBlock& continue_target) {
        contexts_.push_back({&break_target, &continue_target});
    }
    void pop() {
        if (contexts_.empty()) throw std::logic_error("循环上下文栈为空");
        contexts_.pop_back();
    }
    [[nodiscard]] const LoopControlContext& current() const {
        if (contexts_.empty()) throw std::logic_error("break/continue 必须位于循环内");
        return contexts_.back();
    }

private:
    std::vector<LoopControlContext> contexts_;
};

// 从脚本或函数 AST 构建 Non-SSA IR；不执行优化 pass。
class IRBuilder final {
public:
    explicit IRBuilder(FunctionUnit& function);
    explicit IRBuilder(CommandUnit& command);
    void build(const ast& root);
    // 按绝对路径复用有效模块；失效时重建并注册到文件 IR 缓存。
    [[nodiscard]] static std::shared_ptr<IRModule> build_file(
        const std::filesystem::path& path);

private:
    [[nodiscard]] ValueId create_const(const_ba_obj_ptr value);
    [[nodiscard]] ValueId create_load(SlotId slot);
    [[nodiscard]] ValueId create_concat(std::vector<ValueId> items, ConcatInst::Axis axis);
    [[nodiscard]] ValueId create_cell(std::vector<ValueId> items, std::size_t rows, std::size_t cols);
    [[nodiscard]] ValueId create_colon(ValueId start, std::optional<ValueId> step, ValueId stop);
    [[nodiscard]] ValueId create_unary(ValueId operand, UnaryOp op);
    [[nodiscard]] ValueId create_binary(ValueId lhs, ValueId rhs, BinaryOp op);
    [[nodiscard]] ValueId create_brace_reference(ValueId base, std::vector<ValueId> indices);
    [[nodiscard]] ValueId create_brace_assign(ValueId base, std::vector<ValueId> indices, ValueId value);
    [[nodiscard]] ValueId create_apply(InternedString name, std::vector<ValueId> arguments);
    CallInst& create_call(InternedString name, std::vector<ValueId> arguments);
    ValueApplyInst& create_value_apply(ValueId base, std::vector<ValueId> arguments);
    [[nodiscard]] ValueId create_paren_reference(ValueId base, std::vector<ValueId> indices);
    [[nodiscard]] ValueId create_paren_assign(ValueId base, std::vector<ValueId> indices, ValueId value);
    [[nodiscard]] ValueId create_paren_delete(ValueId base, std::vector<ValueId> indices);
    [[nodiscard]] ValueId create_dot_access(InternedString base_name, Operand member);
    [[nodiscard]] ValueId create_dot_reference(ValueId base, Operand member);
    [[nodiscard]] ValueId create_dot_assign(ValueId base, Operand member, ValueId value);
    GlobalDeclInst& create_global_decl(std::vector<SlotId> slots);
    PersistentDeclInst& create_persistent_decl(std::vector<SlotId> slots);
    BranchInst& create_branch(ValueId condition, BasicBlock& true_target, BasicBlock& false_target);
    GotoInst& create_goto(BasicBlock& target);
    [[nodiscard]] ValueId create_switch_match(ValueId value, ValueId candidate);
    [[nodiscard]] ValueId create_intrinsic(IntrinsicId id, std::vector<ValueId> operands);
    GotoInst& create_break(const LoopControlStack& loops);
    GotoInst& create_continue(const LoopControlStack& loops);
    StoreSlotInst& create_store(SlotId slot, ValueId value);
    ReturnInst& create_return(std::vector<ValueId> values);

    IRBuilder& set_insert_point(BasicBlock& block) noexcept { block_ = &block; return *this; }
    [[nodiscard]] ValueId add_result(
        Instruction& instruction, ValueId& result,
        ValueInfo::Form form = ValueInfo::Form::Single,
        bool is_constant = false);

    [[nodiscard]] BasicBlock& add_block();
    [[nodiscard]] SlotId slot(const std::string& name);
    [[nodiscard]] ValueId lower_value(const ast& node, BasicBlock& block);
    [[nodiscard]] std::vector<ValueId> lower_indices(
        const ast& node, BasicBlock& block);
    [[nodiscard]] Operand lower_member(
        const ast& node, BasicBlock& block);
    [[nodiscard]] BasicBlock* lower_statement(
        const ast& node, BasicBlock* current);
    [[nodiscard]] BasicBlock* lower_if(
        const ast& node, BasicBlock& current);
    [[nodiscard]] BasicBlock* lower_switch(
        const ast& node, BasicBlock& current);
    [[nodiscard]] BasicBlock* lower_for(
        const ast& node, BasicBlock& current);
    [[nodiscard]] BasicBlock* lower_while(
        const ast& node, BasicBlock& current);
    void lower_condition(const ast& node, BasicBlock& current,
                         BasicBlock& true_target, BasicBlock& false_target);
    void lower_short_condition(const ast& node, BasicBlock& current,
                               BasicBlock& true_target, BasicBlock& false_target);
    [[nodiscard]] BasicBlock* lower_short_assignment(
        const ast& node, BasicBlock& current, SlotId target);

    IRUnit& unit_;
    IRStageVersion& version_;
    BasicBlock* block_ = nullptr; // 当前指令插入位置。
    LoopControlStack loops_;
    std::unordered_map<std::string, SlotId> slots_;
};

} // namespace baltam
