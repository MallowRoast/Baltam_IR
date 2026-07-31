#pragma once

#include "ir/ir_base_types.h"

#include "ba_obj/ba_obj.h"
#include "core.h"

#include <cstdint>
#include <memory>
#include <variant>
#include <vector>

namespace baltam {

struct BasicBlock;
struct AnonymousFunctionUnit;
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
 * @brief 运行时对象常量。
 *
 * 该常量用于承载优化 pass 通过 builtin 计算出的结果。`value` 被视为 IR literal，
 * executor 取值时应返回副本，避免后续运行时写入修改 IR 中保存的常量对象。
 */
struct RuntimeObjectConstant {
    const_ba_obj_ptr value;
    bool folded = true;
};

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
    EmptyDoubleMatrixConstant,
    RuntimeObjectConstant>;

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
 * @brief 调用与运算的分派类型。
 *
 * - `Dynamic` 表示仍按 Matlab 运行时规则解析，运算符可能被用户类型重载。
 * - `Builtin` 表示已静态解析到 Matlab 内置实现。
 * - `Internal` 表示 IR/runtime 内部实现，不参与 Matlab 名字查找和运算符重载；
 *   printer 统一显示为 `internal.xxx` 目标或内部运算 mnemonic。
 * - `MFunction` 表示已静态解析到某个 M 函数实例。
 *
 * 运算符如果静态分派到 M 函数，不继续保留为 `UnaryInst` / `BinaryInst`，而应 lower
 * 成 `CallInst`，并通过 `dispatch_type = MFunction` 与 `m_function_target` 记录目标。
 */
enum DispatchType : std::uint8_t {
    Dynamic,
    Builtin,
    Internal,
    MFunction,
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
    Slot,
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
        GlobalDecl,
        PersistentDecl,
        CreateNamedFunctionHandle,
        CreateAnonymousFunctionHandle,
        Apply,
        ValueApply,
        MagicEnd,
        Call,
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
 * @brief `load` 指令。
 */
class LoadSlotInst final : public Instruction {
public:
    /**
     * @brief 构造 `load` 指令。
     *
     * `load` 的输入操作数是 `Slot`，表示一个在 IR 构建阶段就已经静态绑定好的
     * frame 槽位句柄。它读取的是当前 `CodeUnit` 的 slot 空间。
     */
    LoadSlotInst() noexcept : Instruction(Instruction::LoadSlot) {
        effect = Frame;
    }

    ValueId result = InvalidValueId;
    Slot slot = InvalidSlot;
};

/**
 * @brief `store` 指令。
 */
class StoreSlotInst final : public Instruction {
public:
    /**
     * @brief 构造 `store` 指令。
     */
    StoreSlotInst() noexcept : Instruction(Instruction::StoreSlot) {
        effect = Frame;
    }

    Slot slot = InvalidSlot;
    ValueId value = InvalidValueId;
};

/**
 * @brief `global` 声明指令。
 *
 * 该指令保留源码层的全局变量声明语义。声明本身不产生数据流结果，但会把当前
 * `CodeUnit` 中对应名字绑定到 `Global` slot；后续读写仍通过 `LoadSlotInst` /
 * `StoreSlotInst` 引用这些 slot。
 */
class GlobalDeclInst final : public Instruction {
public:
    /**
     * @brief 构造 `global` 声明指令。
     */
    GlobalDeclInst() noexcept : Instruction(Instruction::GlobalDecl) {
        effect = Env;
    }

    std::vector<Slot> slots;
};

/**
 * @brief `persistent` 声明指令。
 *
 * 该指令保留源码层的持久变量声明语义。声明本身不产生数据流结果，但会把当前
 * `CodeUnit` 中对应名字绑定到 `Persistent` slot。
 */
class PersistentDeclInst final : public Instruction {
public:
    /**
     * @brief 构造 `persistent` 声明指令。
     */
    PersistentDeclInst() noexcept : Instruction(Instruction::PersistentDecl) {
        effect = Env;
    }

    std::vector<Slot> slots;
};

/**
 * @brief 构造具名函数句柄指令。
 *
 * 该指令表达源码层 `@name`。它必须区分两类语义：
 * - `Runtime`：运行到这条指令时按当前函数搜索环境查询一次。若查到目标，
 *   runtime 句柄会冻结该目标；若查不到，runtime 句柄保留名字，后续每次调用再查询。
 * - `Static`：IR 构建或前置分析已经确定目标。运行时直接构造已绑定句柄，不再查询。
 */
class CreateNamedFunctionHandleInst final : public Instruction {
public:
    enum ResolutionMode : std::uint8_t {
        Runtime,
        Static,
    };

    /**
     * @brief 构造具名函数句柄指令。
     */
    CreateNamedFunctionHandleInst() noexcept
        : Instruction(Instruction::CreateNamedFunctionHandle) {
        effect = Env;
    }

    ValueId result = InvalidValueId;
    InternedString name;
    ResolutionMode resolution_mode = Runtime;
    DispatchType bound_dispatch_type = Dynamic;
    FunctionUnit* m_function_target = nullptr;
};

/**
 * @brief 构造匿名函数句柄指令。
 *
 * 该指令表达源码层 `@(args) expr` 的 closure 构造点。匿名函数体通过 shared owner
 * 直接引用，捕获值使用当前外层 `CodeUnit` 中已经定义好的 `ValueId` 表示。运行到该指令时，
 * runtime 会把这些 `ValueId` 当前对应的运行时值保存进新建 closure 的 capture environment。
 */
class CreateAnonymousFunctionHandleInst final : public Instruction {
public:
    struct CaptureValue {
        InternedString name;
        /**
         * @brief 捕获来源槽位。
         *
         * 在函数 / 匿名函数体中，`source_slot` 是被捕获变量自己的静态 slot，
         * lowering 通过 `load source_slot` 得到 `captured_value`。
         *
         * 在脚本中，`source_slot` 是脚本中该变量自己的 `ScriptVar` slot。运行时
         * 可以根据该 slot 的绑定状态决定直接访问已绑定存储，或退回动态 lookup。
         */
        Slot source_slot = InvalidSlot;
        ValueId captured_value = InvalidValueId;
    };

    /**
     * @brief 构造匿名函数句柄指令。
     */
    CreateAnonymousFunctionHandleInst() noexcept
        : Instruction(Instruction::CreateAnonymousFunctionHandle) {
        effect = Heap;
    }

    ValueId result = InvalidValueId;
    std::shared_ptr<AnonymousFunctionUnit> target;
    std::vector<CaptureValue> captures;
};

/**
 * @brief 通用圆括号应用指令。
 *
 * `apply` 保留源码层 `A(...)` 的名字歧义：这里暂时只知道发生了一次对名字的圆括号应用，
 * 但尚未收敛成“函数调用”还是“读取同名变量后继续分派”。
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
 * @brief 值圆括号应用指令。
 *
 * `value_apply` 表示 base 已经明确是运行时值，但该值上的圆括号应用还没有继续分派为
 * 函数句柄调用或圆括号取值。base 必须是 `ValueId`，从而让后续类型事实、SSA 提升和
 * 分派收敛都能沿普通数据流追踪。
 */
class ValueApplyInst final : public Instruction {
public:
    /**
     * @brief 构造 `value_apply` 指令。
     */
    ValueApplyInst() noexcept : Instruction(Instruction::ValueApply) {
        effect = Opaque;
    }

    std::vector<ValueId> results;
    ValueId base = InvalidValueId;
    std::vector<Operand> arguments;
};

/**
 * @brief 延迟解析的 magic `end`。
 *
 * 候选上下文按内层到外层排列。后续名字解析、专用 pass 或运行时分派选择第一层真正表示
 * 索引语义的上下文，并负责实现普通数组和类对象 `end` 方法语义；若没有候选能提供索引
 * 语义，则该 `end` 在运行时应报错。候选 Operand 的种类是语义的一部分：
 * `InternedString` 表示尚未解析的源码名字，`Slot` / `ValueId` 表示已经绑定到变量上下文。
 * 已绑定候选遇到 cleared / unbound slot 时应报错，不回退到普通函数名解析；用户自定义
 * `end.m` 不是合法候选。
 */
class MagicEndInst final : public Instruction {
public:
    /**
     * @brief `end` 可能归属的圆括号应用上下文。
     *
     * `A(fun(end))` 在脚本中无法仅靠 AST 判断 `end` 属于 `fun(end)` 还是外层
     * `A(...)`：如果未解析的 `fun` 运行时解析为索引，则使用内层；如果它解析为普通函数
     * 调用，`end` 应继续向外寻找可用的索引上下文。若 `fun` 已经是函数内变量 slot，则
     * 该候选不再按名字重新解析；cleared slot 是错误，而不是候选失败。
     */
    struct MagicEndContext {
        Operand callee_or_base;
        std::uint32_t dim = 0;
        std::uint32_t nindices = 0;
    };

    MagicEndInst() noexcept : Instruction(Instruction::MagicEnd) {
        effect = Opaque;
    }

    ValueId result = InvalidValueId;
    std::vector<MagicEndContext> candidate_contexts;
};

/**
 * @brief 函数调用指令。
 *
 * `call` 只表达已经确认是调用的语义，不再承载 `A(...)` 尚未消歧时的通用圆括号应用。
 * 其中：
 * - `Direct` 表示 callee 作为已知可调用名出现，通常由 `InternedString` 承载
 * - `Indirect` 表示通过值发起调用，例如函数句柄，通常由 `ValueId` 或 `SlotId` 承载
 *
 * 静态分派到 local M 函数也是 `dispatch_type = MFunction` 的一种，目标函数实例由
 * `m_function_target` 保存。这里不再单独保留 `Local` callee kind。
 * `dispatch_type = Internal` 的 direct call 使用普通 helper 名保存在 `callee` 中，
 * 打印时统一加上 `internal.` 前缀。
 */
class CallInst final : public Instruction {
public:
    enum CalleeKind : std::uint8_t {
        Direct,
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
    DispatchType dispatch_type = Dynamic;
    Operand callee;
    FunctionUnit* m_function_target = nullptr;
    std::vector<Operand> arguments;
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
    DispatchType dispatch_type = Dynamic;
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
    DispatchType dispatch_type = Dynamic;
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

    std::vector<ValueId> values;
};

} // namespace baltam
