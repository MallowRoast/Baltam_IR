#pragma once

#include "hir/hir_base_types.h"

#include <cstdint>
#include <variant>
#include <vector>

namespace baltam {

struct BasicBlock;

/**
 * @brief 逻辑常量。
 */
struct LogicalConstant {
    bool value = false;
};

/**
 * @brief `int64` 常量。
 */
struct Int64Constant {
    std::int64_t value = 0;
};

/**
 * @brief `uint64` 常量。
 */
struct UInt64Constant {
    std::uint64_t value = 0;
};

/**
 * @brief `float64` 常量。
 */
struct Float64Constant {
    double value = 0.0;
};

/**
 * @brief `complex128` 常量。
 */
struct Complex128Constant {
    double re = 0.0;
    double im = 0.0;
};

/**
 * @brief 字符字面量常量。
 */
struct CharLiteralConstant {
    InternedString value;
};

/**
 * @brief 字符串字面量常量。
 */
struct StringLiteralConstant {
    InternedString value;
};

/**
 * @brief 空 double 矩阵常量。
 */
struct EmptyDoubleMatrixConstant {};

/**
 * @brief 第一版立即数常量集合。
 */
using Constant = std::variant<
    LogicalConstant,
    Int64Constant,
    UInt64Constant,
    Float64Constant,
    Complex128Constant,
    CharLiteralConstant,
    StringLiteralConstant,
    EmptyDoubleMatrixConstant>;

/**
 * @brief 一元操作类型。
 */
enum UnaryOp : std::uint8_t {
    Uplus,       ///< 一元正号。
    Uminus,      ///< 一元负号。
    LogicalNot,  ///< 逻辑非。
    Transpose,   ///< 普通转置 `.'`
    Ctranspose,  ///< 共轭转置 `'`
};

/**
 * @brief 二元操作类型。
 *
 * 第一版把比较运算也收进二元操作集合，避免再额外拆一层 `CompareOp`。
 */
enum BinaryOp : std::uint8_t {
    Add,        ///< `+`
    Sub,        ///< `-`
    Mul,        ///< `*`
    Rdiv,       ///< `/`
    Ldiv,       ///< `\`
    Pow,        ///< `^`
    ElemMul,    ///< `.*`
    ElemRdiv,   ///< `./`
    ElemLdiv,   ///< `.\`
    ElemPow,    ///< `.^`
    And,        ///< `&`
    Or,         ///< `|`
    Lt,         ///< `<`
    Le,         ///< `<=`
    Gt,         ///< `>`
    Ge,         ///< `>=`
    Eq,         ///< `==`
    Ne,         ///< `~=`
};

/**
 * @brief 第一版 HIR 操作数集合。
 *
 * 操作数保持为轻量值类型，因为它们本质上是某条指令上的输入边，而不是独立 IR 节点。
 * 当前版本直接用强类型 ID 或名字文本承载引用，不再额外包一层单字段 wrapper。
 *
 * 当前版本不再让操作数直接承载立即数常量；字面量应先通过 `ConstInst` 物化为
 * `ValueId`，再由其他指令引用。
 */
using Operand = std::variant<
    ValueId,
    SlotId,
    InternedString>;

/**
 * @brief 指令属性。
 *
 * - `may_throw` 表示该指令在执行时可能触发错误并离开当前正常路径。
 * - `is_synthetic` 表示该指令不是直接对应源码表面语句，而是 lowering 或规范化时引入的。
 */
struct InstAttrs {
    /**
     * @brief 构造一个清零后的指令属性集合。
     */
    InstAttrs() noexcept : may_throw(0), is_synthetic(0) {}

    std::uint8_t may_throw : 1;
    std::uint8_t is_synthetic : 1;
};

/**
 * @brief 粗粒度副作用类别。
 */
enum EffectClass : std::uint8_t {
    Pure,    ///< 不读写外部可观察状态。
    Frame,   ///< 只读写当前 frame slot。
    Heap,    ///< 读写普通堆对象。
    Env,     ///< 读写 workspace、resolver、path 等动态环境。
    Opaque,  ///< HIR 层无法精确描述的副作用。
};

/**
 * @brief HIR 指令基类。
 *
 * 当前阶段改为继承层次，是因为具体指令的结果个数、字段形状和控制流角色都已经开始
 * 明显分化，继续用统一的 payload 容器表示会让结构约束越来越别扭。
 */
class Instruction {
public:
    /**
     * @brief 指令类型枚举。
     */
    enum Type : std::uint8_t {
        Const,
        LoadSlot,
        StoreSlot,
        Copy,
        Undef,
        Unary,
        Binary,
        Goto,
        Branch,
        Return,
    };

    /**
     * @brief 多态删除所需的虚析构函数。
     */
    virtual ~Instruction() = default;

    /**
     * @brief 获取当前指令的类型。
     *
     * @return 当前指令的 `Type`。
     */
    [[nodiscard]] Type type() const noexcept {
        return type_;
    }

    /**
     * @brief 判断当前指令是否为终结类指令。
     *
     * @return 当前类型为 `Goto`、`Branch` 或 `Return` 时返回 true。
     */
    [[nodiscard]] bool is_terminator() const noexcept {
        return type_ == Goto || type_ == Branch || type_ == Return;
    }

    EffectClass effect = Pure;
    SourceSpan source_span;
    InstAttrs attrs;

protected:
    /**
     * @brief 构造指定类型的指令基类。
     *
     * @param type 当前指令的类型。
     */
    explicit Instruction(Type type) noexcept : type_(type) {}

private:
    Type type_ = Undef;
};

/**
 * @brief `const` 指令。
 *
 * 当前版本要求所有需要进入数据流的字面量常量都先通过 `const` 指令物化为 `ValueId`。
 */
class ConstInst final : public Instruction {
public:
    /**
     * @brief 构造 `const` 指令。
     */
    ConstInst() noexcept : Instruction(Instruction::Const) {}

    ValueId result = InvalidValueId;
    Constant value;
};

/**
 * @brief `load_slot` 指令。
 */
class LoadSlotInst final : public Instruction {
public:
    /**
     * @brief 构造 `load_slot` 指令。
     */
    LoadSlotInst() noexcept : Instruction(Instruction::LoadSlot) {}

    ValueId result = InvalidValueId;
    SlotId slot_id = InvalidSlotId;
};

/**
 * @brief `store_slot` 指令。
 */
class StoreSlotInst final : public Instruction {
public:
    /**
     * @brief 构造 `store_slot` 指令。
     */
    StoreSlotInst() noexcept : Instruction(Instruction::StoreSlot) {}

    SlotId slot_id = InvalidSlotId;
    Operand value;
};

/**
 * @brief `copy` 指令。
 */
class CopyInst final : public Instruction {
public:
    /**
     * @brief 构造 `copy` 指令。
     */
    CopyInst() noexcept : Instruction(Instruction::Copy) {}

    ValueId result = InvalidValueId;
    Operand value;
};

/**
 * @brief `undef` 指令。
 */
class UndefInst final : public Instruction {
public:
    /**
     * @brief 构造 `undef` 指令。
     */
    UndefInst() noexcept : Instruction(Instruction::Undef) {}

    ValueId result = InvalidValueId;
};

/**
 * @brief 一元运算指令。
 */
class UnaryInst final : public Instruction {
public:
    /**
     * @brief 构造一元运算指令。
     */
    UnaryInst() noexcept : Instruction(Instruction::Unary) {}

    ValueId result = InvalidValueId;
    UnaryOp op = Uplus;
    Operand operand;
};

/**
 * @brief 二元运算指令。
 */
class BinaryInst final : public Instruction {
public:
    /**
     * @brief 构造二元运算指令。
     */
    BinaryInst() noexcept : Instruction(Instruction::Binary) {}

    ValueId result = InvalidValueId;
    BinaryOp op = Add;
    Operand lhs;
    Operand rhs;
};

/**
 * @brief 无条件跳转指令。
 *
 * 该指令属于终结类指令，只允许出现在基本块末尾。
 */
class GotoInst final : public Instruction {
public:
    /**
     * @brief 构造无条件跳转指令。
     */
    GotoInst() noexcept : Instruction(Instruction::Goto) {}

    BasicBlock* target = nullptr;
};

/**
 * @brief 条件分支指令。
 *
 * 该指令属于终结类指令，只允许出现在基本块末尾。
 */
class BranchInst final : public Instruction {
public:
    /**
     * @brief 构造条件分支指令。
     */
    BranchInst() noexcept : Instruction(Instruction::Branch) {}

    Operand condition;
    BasicBlock* true_target = nullptr;
    BasicBlock* false_target = nullptr;
};

/**
 * @brief 返回指令。
 *
 * 该指令属于终结类指令，只允许出现在基本块末尾。
 */
class ReturnInst final : public Instruction {
public:
    /**
     * @brief 构造返回指令。
     */
    ReturnInst() noexcept : Instruction(Instruction::Return) {}

    std::vector<Operand> values;
};

} // namespace baltam
