#pragma once

#include "ir/ir_base_types.h"

#include <cstdint>
#include <variant>
#include <vector>

namespace baltam {

struct BasicBlock;
struct FunctionUnit;

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
 * @brief 第一版 IR 操作数集合。
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
    Opaque,  ///< IR 层无法精确描述的副作用。
};

/**
 * @brief IR 指令基类。
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
        LoadWorkspace,
        StoreWorkspace,
        Apply,
        Call,
        Copy,
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

    BasicBlock* parent = nullptr;
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
    Type type_;
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
     *
     * `load_slot` 的输入操作数是 `SlotId`，表示一个在 IR 构建阶段就已经静态绑定好的
     * frame 槽位句柄。它读取的是当前 `CodeUnit` 的 slot 空间，而不是按名字查询
     * workspace。
     */
    LoadSlotInst() noexcept : Instruction(Instruction::LoadSlot) {
        effect = Frame;
    }

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
    StoreSlotInst() noexcept : Instruction(Instruction::StoreSlot) {
        effect = Frame;
    }

    SlotId slot_id = InvalidSlotId;
    Operand value;
};

/**
 * @brief `load_workspace` 指令。
 *
 * `load_workspace` 与 `load_slot` 的核心区别在于输入不是静态 `SlotId`，而是：
 * - 一个指向工作区句柄 slot 的 `workspace_handle_slot`
 * - 一个需要在 workspace 中查询的 `symbol`
 *
 * 因此它表达的是“按名字访问 workspace”，而不是“读取已经静态绑定好的 frame 槽位”。
 */
class LoadWorkspaceInst final : public Instruction {
public:
    /**
     * @brief 构造 `load_workspace` 指令。
     */
    LoadWorkspaceInst() noexcept : Instruction(Instruction::LoadWorkspace) {
        effect = Env;
    }

    ValueId result = InvalidValueId;
    SlotId workspace_handle_slot = InvalidSlotId;
    InternedString symbol;
};

/**
 * @brief `store_workspace` 指令。
 *
 * 该指令与 `store_slot` 的区别同样在于写回目标并非静态 slot，而是当前 workspace 中
 * 名为 `symbol` 的动态名字。
 */
class StoreWorkspaceInst final : public Instruction {
public:
    /**
     * @brief 构造 `store_workspace` 指令。
     */
    StoreWorkspaceInst() noexcept : Instruction(Instruction::StoreWorkspace) {
        effect = Env;
    }

    SlotId workspace_handle_slot = InvalidSlotId;
    InternedString symbol;
    Operand value;
};

/**
 * @brief 通用圆括号应用指令。
 *
 * `apply` 保留源码层 `A(...)` 的歧义：这里暂时只知道发生了一次圆括号应用，但尚未收敛
 * 成“函数调用”还是“圆括号取值”。
 */
class ApplyInst final : public Instruction {
public:
    /**
     * @brief 构造 `apply` 指令。
     */
    ApplyInst() noexcept : Instruction(Instruction::Apply) {
        effect = Opaque;
    }

    std::vector<ValueId> results;
    Operand callee_or_base;
    std::vector<Operand> arguments;
};

/**
 * @brief 函数调用指令。
 *
 * `call` 只表达已经确认是调用的语义，不再承载 `A(...)` 尚未消歧时的通用圆括号应用。
 * 其中：
 * - `Direct` 表示 callee 作为已知可调用名出现，通常由 `InternedString` 承载
 * - `Indirect` 表示通过值发起调用，例如函数句柄，通常由 `ValueId` 或 `SlotId` 承载
 */
class CallInst final : public Instruction {
public:
    enum CalleeKind : std::uint8_t {
        Direct,
        Local,
        Indirect,
    };

    /**
     * @brief 构造 `call` 指令。
     */
    CallInst() noexcept : Instruction(Instruction::Call) {
        effect = Opaque;
    }

    std::vector<ValueId> results;
    CalleeKind callee_kind = Direct;
    Operand callee;
    FunctionUnit* local_target = nullptr;
    std::vector<Operand> arguments;
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
