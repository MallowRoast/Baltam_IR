#pragma once

#include "ir/ir_value.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

namespace baltam {

struct ba_obj;
class BasicBlock;
class FunctionUnit;
using const_ba_obj_ptr = std::shared_ptr<const ba_obj>;

// 动态分派保留到运行时；其余类别由后续解析或优化阶段确定。
enum class DispatchType : std::uint8_t { Dynamic, MFunction, Builtin, Internal, Plugin };

// 保留一元运算在源码中的具体操作种类。
enum class UnaryOp : std::uint8_t {
    Uplus,
    Uminus,
    LogicalNot,
    Transpose,
    Ctranspose,
};

// 矩阵、逐元素、比较和双侧求值逻辑运算使用不同的操作种类。
enum class BinaryOp : std::uint8_t {
    Add,
    Sub,
    Mul,
    Rdiv,
    Ldiv,
    Pow,
    ElemMul,
    ElemRdiv,
    ElemLdiv,
    ElemPow,
    And,
    Or,
    Lt,
    Le,
    Gt,
    Ge,
    Eq,
    Ne,
};

// 内部语义操作先保留语义身份，由后续阶段选择具体实现。
enum class IntrinsicId : std::uint8_t {
    SwitchMatch,
    ForeachInit,
    ForeachCount,
    ForeachGet,
    IntGt,
    IntAdd,
    LogicalScalar,
};

// 仅描述指令是否可能改写已有 slot；不表示是否会抛错或产生其他副作用。
enum class Effect : std::uint8_t { NoSlotWrite, MayWriteSlot };

// 基本块通过该基类独占拥有不同种类的指令。
class Instruction : public User {
public:
    enum class Type : std::uint8_t {
        Const,
        Concat,
        CellConstruct,
        Colon,
        MagicColon,
        MagicEnd,
        GlobalDecl,
        PersistentDecl,
        Goto,
        Branch,
        Return,
        Apply,
        ValueApply,
        Call,
        Intrinsic,
        ParenReference,
        BraceReference,
        DotAccess,
        DotReference,
        ParenAssign,
        ParenDelete,
        BraceAssign,
        DotAssign,
        Unary,
        Binary,
        LoadSlot,
        StoreSlot,
        Phi,
    };

    virtual ~Instruction() = default;

    // 复制指令内容；块、值和静态目标引用由阶段克隆流程处理。
    [[nodiscard]] std::unique_ptr<Instruction> clone() const;

    [[nodiscard]] Type type() const noexcept { return type_; }

    [[nodiscard]] Effect effect() const noexcept;
    [[nodiscard]] BasicBlock* parent() const noexcept { return parent_; }

    [[nodiscard]] bool is_terminator() const noexcept {
        return type_ == Type::Goto || type_ == Type::Branch || type_ == Type::Return;
    }

protected:
    explicit Instruction(Type type) noexcept : type_(type) {}

    Instruction(const Instruction& other) : User(other), type_(other.type_) {}

private:
    friend class BasicBlock;

    Type type_;
    BasicBlock* parent_ = nullptr;
};

// 将运行前确定的对象物化为一个 IR 值；non-SSA 和 SSA 使用同一结构。
class ConstInst final : public Instruction {
public:
    ConstInst(const_ba_obj_ptr value, ValueId result)
        : Instruction(Type::Const), value(std::move(value)), result(result) {}

    const_ba_obj_ptr value;
    ValueId result;
};

// 根据进入当前基本块的前驱选择输入值；空值表示该路径上的变量未定义。
class PhiInst final : public Instruction {
public:
    struct Incoming {
        BasicBlock* predecessor;
        std::optional<ValueId> value;
    };

    explicit PhiInst(ValueId result, const std::vector<Incoming>& incomings = {})
        : Instruction(Type::Phi), result(result) {
        for (const auto& incoming : incomings)
            add_incoming(incoming.predecessor, incoming.value);
    }

    [[nodiscard]] const std::vector<BasicBlock*>& predecessors() const noexcept {
        return predecessors_;
    }

    [[nodiscard]] Incoming incoming(std::size_t index) const {
        return {predecessors_.at(index), User::operand(index).value()};
    }

    void add_incoming(BasicBlock* predecessor, std::optional<ValueId> value) {
        predecessors_.push_back(predecessor);
        try {
            append_operand(value);
        } catch (...) {
            predecessors_.pop_back();
            throw;
        }
    }

    void set_incoming_value(std::size_t index, std::optional<ValueId> value) {
        set_operand(index, value);
    }

    void set_predecessor(std::size_t index, BasicBlock* predecessor) {
        predecessors_.at(index) = predecessor;
    }

    void erase_incoming(std::size_t index) {
        erase_operand(index);
        predecessors_.erase(predecessors_.begin() + index);
    }

    ValueId result;

private:
    std::vector<BasicBlock*> predecessors_;
};

// 从已绑定的槽位读取一个值；槽位类别从所属代码单元的槽位表查询。
class LoadSlotInst final : public Instruction {
public:
    LoadSlotInst(ValueId result, SlotId slot)
        : Instruction(Type::LoadSlot), result(result), slot(slot) {}

    ValueId result;
    SlotId slot;
};

// 将已求值的结果写入已绑定的槽位；不产生新的 ValueId。
class StoreSlotInst final : public Instruction {
public:
    StoreSlotInst(SlotId slot, ValueId value) : Instruction(Type::StoreSlot), slot(slot) {
        append_operand(value);
    }

    [[nodiscard]] ValueId value() const { return input(0); }

    void set_value(ValueId value) { User::set_operand(0, value); }

    SlotId slot;
};

// 非空的水平或竖直并置；输入是否包含非常量值由构建调用处提供。
class ConcatInst final : public Instruction {
public:
    enum class Axis : std::uint8_t { Horizontal, Vertical };

    ConcatInst(std::vector<ValueId> items, ValueId result, Axis axis, bool is_dynamic)
        : Instruction(Type::Concat), result(result), axis(axis), is_dynamic(is_dynamic) {
        append_values(items);
    }

    [[nodiscard]] std::vector<ValueId> items() const { return input_values(0); }

    ValueId result;
    Axis axis;
    bool is_dynamic;
};

// 按源码顺序保存非空元胞构造的元素及二维形状。
class CellConstructInst final : public Instruction {
public:
    CellConstructInst(std::vector<ValueId> items, std::size_t rows, std::size_t cols,
                      ValueId result)
        : Instruction(Type::CellConstruct), rows(rows), cols(cols), result(result) {
        if ((cols != 0 && rows > std::numeric_limits<std::size_t>::max() / cols) ||
            items.size() != rows * cols)
            throw std::invalid_argument("元胞构造指令的元素数量与形状不一致");
        append_values(items);
    }

    [[nodiscard]] std::vector<ValueId> items() const { return input_values(0); }

    std::size_t rows;
    std::size_t cols;
    ValueId result;
};

// 生成序列的冒号表达式；缺省步长与显式给出步长保持区分。
class ColonInst final : public Instruction {
public:
    ColonInst(ValueId result, ValueId start, std::optional<ValueId> step, ValueId stop)
        : Instruction(Type::Colon), result(result) {
        append_operand(start);
        append_operand(step);
        append_operand(stop);
    }

    [[nodiscard]] ValueId start() const { return input(0); }

    void set_start(ValueId value) { User::set_operand(0, value); }

    [[nodiscard]] std::optional<ValueId> step() const { return User::operand(1).value(); }

    void set_step(std::optional<ValueId> value) { set_operand(1, value); }

    [[nodiscard]] ValueId stop() const { return input(2); }

    void set_stop(ValueId value) { set_operand(2, value); }

    ValueId result;
    bool needs_runtime_dispatch = true;
};

// 索引中的单独冒号只产生占位值，不生成序列。
class MagicColonInst final : public Instruction {
public:
    explicit MagicColonInst(ValueId result) : Instruction(Type::MagicColon), result(result) {}

    ValueId result;
};

// 保留可能归属的索引上下文，按内层到外层依次尝试。
class MagicEndInst final : public Instruction {
public:
    struct MagicEndContext {
        Operand callee_or_base;
        std::uint32_t dim;
        std::uint32_t nindices;
    };

    MagicEndInst(const std::vector<MagicEndContext>& candidate_contexts, ValueId result)
        : Instruction(Type::MagicEnd), result(result) {
        for (const auto& context : candidate_contexts) {
            if (context.dim == 0 || context.nindices == 0 || context.dim > context.nindices)
                throw std::invalid_argument("索引 end 指令的维度或索引数量无效");
            if (const auto* name = std::get_if<InternedString>(&context.callee_or_base))
                if (name->empty())
                    throw std::invalid_argument("索引 end 指令的候选名字不能为空");
            contexts_.push_back(
                {append_named_operand(context.callee_or_base), context.dim, context.nindices});
        }
    }

    [[nodiscard]] std::vector<MagicEndContext> candidate_contexts() const {
        std::vector<MagicEndContext> result;
        for (const auto& context : contexts_)
            result.push_back(
                {named_operand(context.callee_or_base), context.dim, context.nindices});
        return result;
    }

    ValueId result;

private:
    struct Context {
        NamedOperand callee_or_base;
        std::uint32_t dim;
        std::uint32_t nindices;
    };
    std::vector<Context> contexts_;
};

// 将当前代码单元中的名字绑定到全局存储；槽位类别由构建调用处查询槽位表。
class GlobalDeclInst final : public Instruction {
public:
    explicit GlobalDeclInst(std::vector<SlotId> slots)
        : Instruction(Type::GlobalDecl), slots(std::move(slots)) {}

    std::vector<SlotId> slots;
};

// 将当前函数中的名字绑定到该函数的持久存储；槽位类别由构建调用处查询槽位表。
class PersistentDeclInst final : public Instruction {
public:
    explicit PersistentDeclInst(std::vector<SlotId> slots)
        : Instruction(Type::PersistentDecl), slots(std::move(slots)) {}

    std::vector<SlotId> slots;
};

// 无条件跳转到目标基本块；目标归属由后续 CFG 检查确定。
class GotoInst final : public Instruction {
public:
    explicit GotoInst(BasicBlock* target) : Instruction(Type::Goto), target(target) {}

    BasicBlock* target;
};

// 根据已求值的条件选择两个后继基本块之一。
class BranchInst final : public Instruction {
public:
    BranchInst(ValueId condition, BasicBlock* true_target, BasicBlock* false_target)
        : Instruction(Type::Branch), true_target(true_target), false_target(false_target) {
        append_operand(condition);
    }

    [[nodiscard]] ValueId condition() const { return input(0); }

    void set_condition(ValueId value) { User::set_operand(0, value); }

    BasicBlock* true_target;
    BasicBlock* false_target;
};

// 结束当前代码单元；函数返回值由返回槽位提供。
class ReturnInst final : public Instruction {
public:
    ReturnInst() : Instruction(Type::Return) {}
};

// 保留 A(...) 的名字歧义，后续再确定调用或索引。
class ApplyInst final : public Instruction {
public:
    ApplyInst(std::vector<ValueId> results, Operand callee_or_base, std::vector<ValueId> arguments)
        : Instruction(Type::Apply), results(std::move(results)) {
        callee_or_base_ = append_named_operand(callee_or_base);
        arguments_offset_ = operand_count();
        append_values(arguments);
    }

    [[nodiscard]] Operand callee_or_base() const { return named_operand(callee_or_base_); }

    [[nodiscard]] std::vector<ValueId> arguments() const { return input_values(arguments_offset_); }

    std::vector<ValueId> results;

private:
    NamedOperand callee_or_base_;
    std::size_t arguments_offset_;
};

// base 已经求值，圆括号语义仍可能是句柄调用或索引。
class ValueApplyInst final : public Instruction {
public:
    ValueApplyInst(std::vector<ValueId> results, std::vector<ValueId> arguments, ValueId base)
        : Instruction(Type::ValueApply), results(std::move(results)) {
        append_operand(base);
        append_values(arguments);
    }

    [[nodiscard]] ValueId base() const { return input(0); }

    void set_base(ValueId value) { User::set_operand(0, value); }

    [[nodiscard]] std::vector<ValueId> arguments() const { return input_values(1); }

    std::vector<ValueId> results;
};

// 已确定按函数名调用；函数句柄调用由 ValueApplyInst 按运行时值处理。
// 跨模块 M 函数可能随源码和查找环境变化，IR 不保存其静态目标。
class CallInst final : public Instruction {
public:
    CallInst(std::vector<ValueId> results, InternedString callee, std::vector<ValueId> arguments,
             DispatchType dispatch_type = DispatchType::Dynamic,
             FunctionUnit* m_function_target = nullptr)
        : Instruction(Type::Call), results(std::move(results)), callee(std::move(callee)),
          m_function_target(m_function_target), dispatch_type(dispatch_type) {
        append_values(arguments);
    }

    [[nodiscard]] std::vector<ValueId> arguments() const { return input_values(0); }

    std::vector<ValueId> results;
    InternedString callee;
    // 静态目标仅引用同模块内词法绑定的函数。
    FunctionUnit* m_function_target;
    DispatchType dispatch_type;
};

// 不绑定运行时函数或机器指令；内部操作不得隐式改写 slot。
class IntrinsicInst final : public Instruction {
public:
    IntrinsicInst(ValueId result, IntrinsicId id, std::vector<ValueId> operands)
        : Instruction(Type::Intrinsic), result(result), id(id) {
        append_values(operands);
    }

    [[nodiscard]] std::vector<ValueId> operands() const { return input_values(0); }

    ValueId result;
    IntrinsicId id;
};

// 已确定为圆括号取值，保留多个输出及索引实参的源码顺序。
class ParenReferenceInst final : public Instruction {
public:
    ParenReferenceInst(ValueId result, std::vector<ValueId> indices, ValueId base)
        : Instruction(Type::ParenReference), result(result) {
        append_operand(base);
        append_values(indices);
    }

    [[nodiscard]] ValueId base() const { return input(0); }

    void set_base(ValueId value) { User::set_operand(0, value); }

    [[nodiscard]] std::vector<ValueId> indices() const { return input_values(1); }

    ValueId result;
};

// 更新圆括号选中的元素，并定义新的 base 值。
class ParenAssignInst final : public Instruction {
public:
    ParenAssignInst(std::vector<ValueId> indices, ValueId result, ValueId base, ValueId value)
        : Instruction(Type::ParenAssign), result(result) {
        append_operand(base);
        append_operand(value);
        append_values(indices);
    }

    [[nodiscard]] ValueId base() const { return input(0); }

    void set_base(ValueId value) { User::set_operand(0, value); }

    [[nodiscard]] ValueId value() const { return input(1); }

    void set_value(ValueId value) { set_operand(1, value); }

    [[nodiscard]] std::vector<ValueId> indices() const { return input_values(2); }

    ValueId result;
};

// A(I) = [] 的删除语义不携带右值，结果是更新后的 base。
class ParenDeleteInst final : public Instruction {
public:
    ParenDeleteInst(std::vector<ValueId> indices, ValueId result, ValueId base)
        : Instruction(Type::ParenDelete), result(result) {
        append_operand(base);
        append_values(indices);
    }

    [[nodiscard]] ValueId base() const { return input(0); }

    void set_base(ValueId value) { User::set_operand(0, value); }

    [[nodiscard]] std::vector<ValueId> indices() const { return input_values(1); }

    ValueId result;
};

// 花括号访问的单个 IR 结果可承载逗号分隔列表。
class BraceReferenceInst final : public Instruction {
public:
    BraceReferenceInst(ValueId result, std::vector<ValueId> indices, ValueId base)
        : Instruction(Type::BraceReference), result(result) {
        append_operand(base);
        append_values(indices);
    }

    [[nodiscard]] ValueId base() const { return input(0); }

    void set_base(ValueId value) { User::set_operand(0, value); }

    [[nodiscard]] std::vector<ValueId> indices() const { return input_values(1); }

    ValueId result;
};

// 花括号赋值的单个 IR 输入值可承载逗号分隔列表。
class BraceAssignInst final : public Instruction {
public:
    BraceAssignInst(std::vector<ValueId> indices, ValueId value, ValueId result, ValueId base)
        : Instruction(Type::BraceAssign), result(result) {
        append_operand(base);
        append_operand(value);
        append_values(indices);
    }

    [[nodiscard]] ValueId base() const { return input(0); }

    void set_base(ValueId value) { User::set_operand(0, value); }

    [[nodiscard]] ValueId value() const { return input(1); }

    void set_value(ValueId value) { set_operand(1, value); }

    [[nodiscard]] std::vector<ValueId> indices() const { return input_values(2); }

    ValueId result;
};

// 保留点号访问左侧名字尚未绑定时的歧义；member 可为静态名字或动态求值结果。
class DotAccessInst final : public Instruction {
public:
    DotAccessInst(ValueId result, Operand base_or_name, Operand member)
        : Instruction(Type::DotAccess), result(result) {
        base_or_name_ = append_named_operand(base_or_name);
        member_ = append_named_operand(member);
    }

    [[nodiscard]] Operand base_or_name() const { return named_operand(base_or_name_); }

    [[nodiscard]] Operand member() const { return named_operand(member_); }

    ValueId result;

private:
    NamedOperand base_or_name_;
    NamedOperand member_;
};

// 已确定按值读取成员，具体实现仍由对象协议分派。
class DotReferenceInst final : public Instruction {
public:
    DotReferenceInst(ValueId result, Operand member, ValueId base)
        : Instruction(Type::DotReference), result(result) {
        append_operand(base);
        member_ = append_named_operand(member);
    }

    [[nodiscard]] ValueId base() const { return input(0); }

    void set_base(ValueId value) { User::set_operand(0, value); }

    [[nodiscard]] Operand member() const { return named_operand(member_); }

    ValueId result;

private:
    NamedOperand member_;
};

// 设置静态或动态成员，并定义更新后的 base 值。
class DotAssignInst final : public Instruction {
public:
    DotAssignInst(Operand member, ValueId result, ValueId base, ValueId value)
        : Instruction(Type::DotAssign), result(result) {
        append_operand(base);
        append_operand(value);
        member_ = append_named_operand(member);
    }

    [[nodiscard]] ValueId base() const { return input(0); }

    void set_base(ValueId value) { User::set_operand(0, value); }

    [[nodiscard]] ValueId value() const { return input(1); }

    void set_value(ValueId value) { set_operand(1, value); }

    [[nodiscard]] Operand member() const { return named_operand(member_); }

    ValueId result;

private:
    NamedOperand member_;
};

// 一元运算保留正负号、逻辑非及两种转置的源码区别。
class UnaryInst final : public Instruction {
public:
    UnaryInst(ValueId result, ValueId operand, UnaryOp op)
        : Instruction(Type::Unary), result(result), op(op) {
        append_operand(operand);
    }

    using User::operand;
    using User::set_operand;

    [[nodiscard]] ValueId operand() const { return input(0); }

    void set_operand(ValueId value) { User::set_operand(0, value); }

    ValueId result;
    UnaryOp op;
    bool needs_runtime_dispatch = true;
};

// 二元逻辑运算的两侧都已求值；短路逻辑运算由控制流表示。
class BinaryInst final : public Instruction {
public:
    BinaryInst(ValueId result, ValueId lhs, ValueId rhs, BinaryOp op)
        : Instruction(Type::Binary), result(result), op(op) {
        append_operand(lhs);
        append_operand(rhs);
    }

    [[nodiscard]] ValueId lhs() const { return input(0); }

    void set_lhs(ValueId value) { User::set_operand(0, value); }

    [[nodiscard]] ValueId rhs() const { return input(1); }

    void set_rhs(ValueId value) { set_operand(1, value); }

    ValueId result;
    BinaryOp op;
    bool needs_runtime_dispatch = true;
};

} // baltam 命名空间
