#pragma once

#include "ir/ir_value.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace baltam {

struct ba_obj;
class BasicBlock;
class FunctionUnit;
class AnonymousFunctionUnit;
using const_ba_obj_ptr = std::shared_ptr<const ba_obj>;

// 动态分派保留到运行时；其余类别由后续解析或优化阶段确定。
enum class DispatchType : std::uint8_t { Dynamic, MFunction, Builtin, Internal, Plugin};

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

// 基本块通过该基类独占拥有不同种类的指令。
class Instruction {
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
        CreateNamedFunctionHandle,
        CreateAnonymousFunctionHandle,
        LoadSlot,
        StoreSlot,
    };

    virtual ~Instruction() = default;

    // 预留各类指令的文本表示入口，具体格式由后续逐类实现。
    [[nodiscard]] virtual std::string to_string() const;

    [[nodiscard]] Type type() const noexcept { return type_; }
    [[nodiscard]] bool is_terminator() const noexcept {
        return type_ == Type::Goto || type_ == Type::Branch ||
               type_ == Type::Return;
    }

protected:
    explicit Instruction(Type type) noexcept : type_(type) {}

private:
    Type type_;
};

// 将运行前确定的对象物化为一个 IR 值；non-SSA 和 SSA 使用同一结构。
class ConstInst final : public Instruction {
public:
    ConstInst(const_ba_obj_ptr value, ValueId result)
        : Instruction(Type::Const), value(std::move(value)), result(result) {}

    const_ba_obj_ptr value;
    ValueId result;
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
    StoreSlotInst(SlotId slot, ValueId value)
        : Instruction(Type::StoreSlot), slot(slot), value(value) {}

    SlotId slot;
    ValueId value;
};

// 非空的水平或竖直并置；输入是否包含非常量值由构建调用处提供。
class ConcatInst final : public Instruction {
public:
    enum class Axis : std::uint8_t { Horizontal, Vertical };

    ConcatInst(std::vector<ValueId> items, ValueId result, Axis axis,
               bool is_dynamic)
        : Instruction(Type::Concat), items(std::move(items)), result(result),
          axis(axis), is_dynamic(is_dynamic) {}

    // 空 [] 生成常量，普通单值 [x] 透传 x；实际并置至少有一项输入。
    // 输入值即使是空数组，也不能删除对应的项。
    std::vector<ValueId> items;
    ValueId result;
    Axis axis;
    // 输入中是否包含非常量值；不能据此判断是否需要运行时分派。
    bool is_dynamic;
};

// 按源码顺序保存非空元胞构造的元素及二维形状。
class CellConstructInst final : public Instruction {
public:
    CellConstructInst(std::vector<ValueId> items, std::size_t rows,
                      std::size_t cols, ValueId result)
        : Instruction(Type::CellConstruct), items(std::move(items)),
          rows(rows), cols(cols), result(result) {
        // 空 {} 已由 ConstInst 表示；非空输入与形状一致性隐含行列非零。
        if ((cols != 0 && rows > std::numeric_limits<std::size_t>::max() / cols) ||
            this->items.size() != rows * cols) {
            throw std::invalid_argument("元胞构造指令的元素数量与形状不一致");
        }
    }

    std::vector<ValueId> items;
    std::size_t rows;
    std::size_t cols;
    ValueId result;
};

// 生成序列的冒号表达式；缺省步长与显式给出步长保持区分。
class ColonInst final : public Instruction {
public:
    ColonInst(ValueId result, ValueId start, std::optional<ValueId> step,
              ValueId stop)
        : Instruction(Type::Colon), result(result), start(start),
          step(step), stop(stop) {}

    ValueId result;
    ValueId start;
    std::optional<ValueId> step;
    ValueId stop;
    bool needs_runtime_dispatch = true;
};

// 索引中的单独冒号只产生占位值，不生成序列。
class MagicColonInst final : public Instruction {
public:
    explicit MagicColonInst(ValueId result)
        : Instruction(Type::MagicColon), result(result) {}

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

    MagicEndInst(std::vector<MagicEndContext> candidate_contexts,
                 ValueId result)
        : Instruction(Type::MagicEnd),
          candidate_contexts(std::move(candidate_contexts)), result(result) {
        for (const auto& context : this->candidate_contexts) {
            if (context.dim == 0 || context.nindices == 0 ||
                context.dim > context.nindices) {
                throw std::invalid_argument("索引 end 指令的维度或索引数量无效");
            }
            if (const auto* name =
                           std::get_if<InternedString>(&context.callee_or_base)) {
                if (name->empty()) {
                    throw std::invalid_argument("索引 end 指令的候选名字不能为空");
                }
            }
        }
    }

    std::vector<MagicEndContext> candidate_contexts;
    ValueId result;
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
    explicit GotoInst(BasicBlock* target)
        : Instruction(Type::Goto), target(target) {}

    BasicBlock* target;
};

// 根据已求值的条件选择两个后继基本块之一。
class BranchInst final : public Instruction {
public:
    BranchInst(ValueId condition, BasicBlock* true_target,
               BasicBlock* false_target)
        : Instruction(Type::Branch), true_target(true_target),
          false_target(false_target), condition(condition) {}

    BasicBlock* true_target;
    BasicBlock* false_target;
    ValueId condition;
};

// 结束当前代码单元，按顺序返回已有的值；脚本可使用空列表。
class ReturnInst final : public Instruction {
public:
    explicit ReturnInst(std::vector<ValueId> values)
        : Instruction(Type::Return), values(std::move(values)) {
    }

    std::vector<ValueId> values;
};

// 保留 A(...) 的名字歧义，后续再确定调用或索引。
class ApplyInst final : public Instruction {
public:
    ApplyInst(std::vector<ValueId> results, Operand callee_or_base,
              std::vector<ValueId> arguments)
        : Instruction(Type::Apply), results(std::move(results)),
          callee_or_base(std::move(callee_or_base)),
          arguments(std::move(arguments)) {}

    std::vector<ValueId> results;
    Operand callee_or_base;
    std::vector<ValueId> arguments;
};

// base 已经求值，圆括号语义仍可能是句柄调用或索引。
class ValueApplyInst final : public Instruction {
public:
    ValueApplyInst(std::vector<ValueId> results,
                   std::vector<ValueId> arguments, ValueId base)
        : Instruction(Type::ValueApply), results(std::move(results)),
          arguments(std::move(arguments)), base(base) {}

    std::vector<ValueId> results;
    std::vector<ValueId> arguments;
    ValueId base;
};

// 已确定按函数名调用；函数句柄调用由 ValueApplyInst 按运行时值处理。
// 跨模块 M 函数可能随源码和查找环境变化，IR 不保存其静态目标。
class CallInst final : public Instruction {
public:
    CallInst(std::vector<ValueId> results, InternedString callee,
             std::vector<ValueId> arguments,
             DispatchType dispatch_type = DispatchType::Dynamic,
             FunctionUnit* m_function_target = nullptr)
        : Instruction(Type::Call), results(std::move(results)),
          callee(std::move(callee)), arguments(std::move(arguments)),
          m_function_target(m_function_target),
          dispatch_type(dispatch_type) {}

    std::vector<ValueId> results;
    InternedString callee;
    std::vector<ValueId> arguments;
    // 仅指向同一 IRModule 内词法绑定的 M 函数；由构建流程保证归属。
    // 局部函数与嵌套函数的区别由函数的词法关系表示，不增加分派类型。
    FunctionUnit* m_function_target;
    // 外部 M 函数与可替换的插件函数保留 Dynamic，由运行时解析。
    DispatchType dispatch_type;
};

// 已确定为圆括号取值，保留多个输出及索引实参的源码顺序。
class ParenReferenceInst final : public Instruction {
public:
    ParenReferenceInst(std::vector<ValueId> results,
                       std::vector<ValueId> indices, ValueId base)
        : Instruction(Type::ParenReference), results(std::move(results)),
          indices(std::move(indices)), base(base) {}

    std::vector<ValueId> results;
    std::vector<ValueId> indices;
    ValueId base;
};

// 更新圆括号选中的元素，并定义新的 base 值。
class ParenAssignInst final : public Instruction {
public:
    ParenAssignInst(std::vector<ValueId> indices, ValueId result,
                    ValueId base, ValueId value)
        : Instruction(Type::ParenAssign), indices(std::move(indices)),
          result(result), base(base), value(value) {}

    std::vector<ValueId> indices;
    ValueId result;
    ValueId base;
    ValueId value;
};

// A(I) = [] 的删除语义不携带右值，结果是更新后的 base。
class ParenDeleteInst final : public Instruction {
public:
    ParenDeleteInst(std::vector<ValueId> indices, ValueId result,
                    ValueId base)
        : Instruction(Type::ParenDelete), indices(std::move(indices)),
          result(result), base(base) {}

    std::vector<ValueId> indices;
    ValueId result;
    ValueId base;
};

// 花括号访问可产生多个元胞内容值。
class BraceReferenceInst final : public Instruction {
public:
    BraceReferenceInst(std::vector<ValueId> results,
                       std::vector<ValueId> indices, ValueId base)
        : Instruction(Type::BraceReference), results(std::move(results)),
          indices(std::move(indices)), base(base) {}

    std::vector<ValueId> results;
    std::vector<ValueId> indices;
    ValueId base;
};

// 花括号赋值允许多个内容值；右侧为空数组仍属于赋值语义。
class BraceAssignInst final : public Instruction {
public:
    BraceAssignInst(std::vector<ValueId> indices, std::vector<ValueId> values,
                    ValueId result, ValueId base)
        : Instruction(Type::BraceAssign), indices(std::move(indices)),
          values(std::move(values)), result(result), base(base) {}

    std::vector<ValueId> indices;
    std::vector<ValueId> values;
    ValueId result;
    ValueId base;
};

// 保留点号访问左侧名字尚未绑定时的歧义；member 可为静态名字或动态求值结果。
class DotAccessInst final : public Instruction {
public:
    DotAccessInst(std::vector<ValueId> results, Operand base_or_name,
                  Operand member)
        : Instruction(Type::DotAccess), results(std::move(results)),
          base_or_name(std::move(base_or_name)), member(std::move(member)) {}

    std::vector<ValueId> results;
    Operand base_or_name;
    Operand member;
};

// 已确定按值读取成员，具体实现仍由对象协议分派。
class DotReferenceInst final : public Instruction {
public:
    DotReferenceInst(std::vector<ValueId> results, Operand member,
                     ValueId base)
        : Instruction(Type::DotReference), results(std::move(results)),
          member(std::move(member)), base(base) {}

    std::vector<ValueId> results;
    Operand member;
    ValueId base;
};

// 设置静态或动态成员，并定义更新后的 base 值。
class DotAssignInst final : public Instruction {
public:
    DotAssignInst(Operand member, ValueId result, ValueId base,
                  ValueId value)
        : Instruction(Type::DotAssign), member(std::move(member)),
          result(result), base(base), value(value) {}

    Operand member;
    ValueId result;
    ValueId base;
    ValueId value;
};

// 一元运算保留正负号、逻辑非及两种转置的源码区别。
class UnaryInst final : public Instruction {
public:
    UnaryInst(ValueId result, ValueId operand, UnaryOp op)
        : Instruction(Type::Unary), result(result), operand(operand), op(op) {
        if (op > UnaryOp::Ctranspose) {
            throw std::invalid_argument("一元运算的操作类型无效");
        }
    }

    ValueId result;
    ValueId operand;
    UnaryOp op;
    bool needs_runtime_dispatch = true;
};

// 二元逻辑运算的两侧都已求值；短路逻辑运算由控制流表示。
class BinaryInst final : public Instruction {
public:
    BinaryInst(ValueId result, ValueId lhs, ValueId rhs, BinaryOp op)
        : Instruction(Type::Binary), result(result), lhs(lhs), rhs(rhs), op(op) {
        if (op > BinaryOp::Ne) {
            throw std::invalid_argument("二元运算的操作类型无效");
        }
    }

    ValueId result;
    ValueId lhs;
    ValueId rhs;
    BinaryOp op;
    bool needs_runtime_dispatch = true;
};

// @name 只构造句柄值；运行时解析模式在构造点查询函数目标。
class CreateNamedFunctionHandleInst final : public Instruction {
public:
    enum class ResolutionMode : std::uint8_t { Runtime, Static };

    CreateNamedFunctionHandleInst(
        InternedString name, ValueId result,
        ResolutionMode resolution_mode = ResolutionMode::Runtime,
        DispatchType bound_dispatch_type = DispatchType::Dynamic,
        FunctionUnit* m_function_target = nullptr)
        : Instruction(Type::CreateNamedFunctionHandle), name(std::move(name)),
          m_function_target(m_function_target), result(result),
          resolution_mode(resolution_mode),
          bound_dispatch_type(bound_dispatch_type) {
        if (this->name.empty()) {
            throw std::invalid_argument("具名函数句柄的函数名不能为空");
        }
        if (resolution_mode == ResolutionMode::Runtime) {
            if (bound_dispatch_type != DispatchType::Dynamic ||
                m_function_target != nullptr) {
                throw std::invalid_argument("运行时解析的具名函数句柄不能预先绑定目标");
            }
        } else if (resolution_mode == ResolutionMode::Static) {
            if (!((bound_dispatch_type == DispatchType::Builtin &&
                   m_function_target == nullptr) ||
                  (bound_dispatch_type == DispatchType::MFunction &&
                   m_function_target != nullptr))) {
                throw std::invalid_argument("静态解析的具名函数句柄目标无效");
            }
        } else {
            throw std::invalid_argument("具名函数句柄的解析模式无效");
        }
    }

    InternedString name;
    FunctionUnit* m_function_target;
    ValueId result;
    ResolutionMode resolution_mode;
    DispatchType bound_dispatch_type;
};

// 构造匿名函数句柄时按值捕获；source_slot 只定位外层来源。
class CreateAnonymousFunctionHandleInst final : public Instruction {
public:
    struct CaptureValue {
        InternedString name;
        Slot source_slot;
        ValueId captured_value;
    };

    CreateAnonymousFunctionHandleInst(
        std::shared_ptr<AnonymousFunctionUnit> target,
        std::vector<CaptureValue> captures, ValueId result);

    std::shared_ptr<AnonymousFunctionUnit> target;
    std::vector<CaptureValue> captures;
    ValueId result;
};

} // baltam 命名空间
