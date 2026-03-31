#ifndef BALTAM_IR_IR_H
#define BALTAM_IR_IR_H

#include <complex>
#include <cstdint>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace baltam {

using ValueId = std::uint32_t;
/**
 * @brief 保留的无效 ValueId 哨兵值。
 *
 * 所有真实 `ValueId` 都从 1 开始分配，因此默认构造出来的 `ValueRef`
 * 或 `InstValue` 只要仍然等于该值，就表示“尚未绑定到任何真实 IR 值”。
 */
inline constexpr ValueId InvalidValueId = 0;

/**
 * @brief 附着在 IR 指令上的源码位置信息。
 */
struct SourceLocation {
    std::string filename;
    int begin_line = 0;
    int begin_column = 0;
    int end_line = 0;
    int end_column = 0;
};

/**
 * @brief 对某个 IR 结果值的轻量引用。
 *
 * 第一阶段先只用稳定的 `ValueId` 标识一个结果值；后续若引入 block
 * argument、tuple-like 结果或更多调试字段，可继续扩展该结构。
 */
struct ValueRef {
    ValueId id = InvalidValueId;

    /**
     * @brief 判断该引用是否已经绑定到一个合法结果值。
     */
    bool is_valid() const;
};

/**
 * @brief 一条指令定义出的单个结果值的元数据。
 *
 * `debug_name` 用于打印出更接近源码的名字，例如 `%x.3`；`source_location`
 * 允许在 SSA/value-based IR 阶段为单个结果保留更细粒度的调试信息。
 */
struct InstValue {
    ValueId id = InvalidValueId;
    std::string debug_name;
    std::optional<SourceLocation> source_location;

    /**
     * @brief 判断该值定义是否已经分配了合法的 ValueId。
     */
    bool is_valid() const;
};

class BasicBlock;
class Function;
class Module;

/**
 * @brief 所有 IR 指令的基类。
 *
 * 一条具体指令至多属于一个 BasicBlock。指令对象的内存由 Function
 * 统一持有，BasicBlock 中只保存非 owning 指针。
 */
class Instruction {
public:
    /**
     * @brief 所有 IR 指令的动态类型标签。
     */
    enum Type {
        Text,
        Binding,
        Number,
        Undef,
        UnaryOp,
        BinOp,
        Phi,
        Asgn,
        Call,
        CondJump,
        Jump,
        Return,
    };

    virtual ~Instruction() = default;

    /**
     * @brief 返回当前指令的具体类型。
     */
    Type type() const;

    /**
     * @brief 返回所属 BasicBlock；若尚未挂接则返回 nullptr。
     */
    BasicBlock* parent() const;

    /**
     * @brief 返回可选的源码位置信息。
     */
    const std::optional<SourceLocation>& source_location() const;

    /**
     * @brief 返回当前指令定义出的结果值列表。
     *
     * 之所以是一个列表而不是单个 `InstValue`，是因为当前 IR 允许
     * 一条指令定义 0 个、1 个或多个结果值。
     *
     * 典型场景是 MATLAB-like 多返回值调用，例如：
     *
     * `[a, b] = foo(x)`
     *
     * 在迁移后的 IR 里更自然的形态是“一条 call 指令定义两个结果值”，
     * 而不是拆成两条独立指令。当前第一阶段迁移期间，大量现有指令可能
     * 仍为空；后续随着 IR 向 value-based / SSA 演进，产值指令将逐步
     * 在这里挂接结果定义。
     */
    const std::vector<InstValue>& value_defs() const;

    /**
     * @brief 返回当前指令定义的结果个数。
     */
    std::size_t value_count() const;

    /**
     * @brief 返回第 `index` 个结果定义对应的 ValueRef；越界时返回非法引用。
     */
    ValueRef value_ref(std::size_t index = 0) const;

    /**
     * @brief 判断当前指令是否定义了至少一个结果值。
     */
    bool has_values() const;

protected:
    /**
     * @brief 用动态类型标签构造一条指令。
     */
    explicit Instruction(Type type, std::optional<SourceLocation> location = std::nullopt);

private:
    friend class BasicBlock;
    friend class Function;

    /**
     * @brief 将指令挂接到某个 BasicBlock。
     */
    void set_parent(BasicBlock* block);

    /**
     * @brief 返回第 `index` 个结果定义；仅供 `Instruction` 内部实现使用。
     */
    const InstValue* value_def(std::size_t index) const;

    /**
     * @brief 由 Function 在构建/迁移阶段批量替换该指令的结果定义。
     */
    void set_value_defs(std::vector<InstValue> values);

    Type type_;
    BasicBlock* parent_ = nullptr;
    std::optional<SourceLocation> source_location_;
    std::vector<InstValue> value_defs_;
};

/**
 * @brief 自由文本形式的临时占位指令。
 */
class TextInstruction final : public Instruction {
public:
    explicit TextInstruction(std::string text, std::optional<SourceLocation> location = std::nullopt);

    /**
     * @brief 返回原始文本内容。
     */
    const std::string& text() const;

private:
    std::string text_;
};

/**
 * @brief 对应运行时名字绑定读取的 IR 节点，例如函数入参或动态变量读取。
 */
class BindingInstruction final : public Instruction {
public:
    explicit BindingInstruction(std::string name,
                                std::optional<SourceLocation> location = std::nullopt);

    /**
     * @brief 返回被引用的符号名。
     */
    const std::string& name() const;

private:
    std::string name_;
};

/**
 * @brief 对应数值字面量的 IR 节点。
 *
 * 当前使用一个较小的 tagged union 来表示原型阶段需要支持的几类数值。
 */
class NumberInstruction final : public Instruction {
public:
    using NumberValue = std::variant<bool, std::int64_t, std::uint64_t, double, std::complex<double>>;

    explicit NumberInstruction(bool value, std::optional<SourceLocation> location = std::nullopt);
    explicit NumberInstruction(std::int64_t value, std::optional<SourceLocation> location = std::nullopt);
    explicit NumberInstruction(std::uint64_t value, std::optional<SourceLocation> location = std::nullopt);
    explicit NumberInstruction(double value, std::optional<SourceLocation> location = std::nullopt);
    explicit NumberInstruction(std::complex<double> value,
                               std::optional<SourceLocation> location = std::nullopt);

    /**
     * @brief 返回字面量载荷。
     */
    const NumberValue& value() const;

private:
    NumberValue value_;
};

/**
 * @brief 显式表示“当前路径上尚未定义具体值”的 SSA 占位指令。
 */
class UndefInstruction final : public Instruction {
public:
    explicit UndefInstruction(std::optional<SourceLocation> location = std::nullopt);
};

/**
 * @brief 统一的单目运算指令。
 */
class UnaryOpInstruction final : public Instruction {
public:
    /**
     * @brief 具体的单目运算种类。
     */
    enum Type {
        Logic_Not,
        UMinus,
    };

    explicit UnaryOpInstruction(Type op, ValueRef operand_ref,
                                std::optional<SourceLocation> location = std::nullopt);

    /**
     * @brief 返回单目运算种类。
     */
    Type op() const;

    /**
     * @brief 返回该操作数对应的 ValueRef。
     */
    ValueRef operand_ref() const;

private:
    Type op_ = Type::UMinus;
    ValueRef operand_ref_;
};

/**
 * @brief 统一的二元运算指令。
 *
 * 该节点对应 `node_add`、`node_greater_than`、`node_multiply`
 * 等二元 AST 节点。
 */
class BinOpInstruction final : public Instruction {
public:
    /**
     * @brief 具体的二元运算种类。
     */
    enum Type {
        Add,
        Subtract,
        Eq,
        Gt,
        Lt,
        Ne,
        Or,
        MPower,
        Multiply,
    };

    BinOpInstruction(Type op, ValueRef lhs_ref, ValueRef rhs_ref,
                     std::optional<SourceLocation> location = std::nullopt);

    /**
     * @brief 返回二元运算种类。
     */
    Type op() const;

    /**
     * @brief 返回左操作数的 ValueRef。
     */
    ValueRef lhs_ref() const;

    /**
     * @brief 返回右操作数的 ValueRef。
     */
    ValueRef rhs_ref() const;

private:
    Type op_ = Type::Add;
    ValueRef lhs_ref_;
    ValueRef rhs_ref_;
};

/**
 * @brief 赋值指令，例如 `a = expr`。
 */
class AssignInstruction final : public Instruction {
public:
    AssignInstruction(std::string name, ValueRef value_ref,
                      std::optional<SourceLocation> location = std::nullopt);

    /**
     * @brief 返回赋值目标变量名。
     */
    const std::string& name() const;

    /**
     * @brief 返回右值对应的 ValueRef。
     */
    ValueRef value_ref() const;

private:
    std::string name_;
    ValueRef value_ref_;
};

/**
 * @brief CFG 合流点上的值合并指令。
 */
class PhiInstruction final : public Instruction {
public:
    /**
     * @brief Phi 节点的一条入边。
     */
    struct Incoming {
        BasicBlock* predecessor = nullptr;
        ValueRef value_ref;
    };

    explicit PhiInstruction(std::vector<Incoming> incomings = {},
                            std::optional<SourceLocation> location = std::nullopt);

    /**
     * @brief 返回全部 incoming。
     */
    const std::vector<Incoming>& incomings() const;

    /**
     * @brief 返回 incoming 个数。
     */
    std::size_t incoming_count() const;

    /**
     * @brief 返回第 `index` 条 incoming；越界时返回 nullptr。
     */
    const Incoming* incoming(std::size_t index) const;

    /**
     * @brief 追加一条 incoming。
     */
    void append_incoming(Incoming incoming);

private:
    std::vector<Incoming> incomings_;
};

/**
 * @brief 带显式输入输出参数的函数调用指令。
 */
class CallInstruction final : public Instruction {
public:
    CallInstruction(std::string name, std::size_t output_count,
                    std::vector<ValueRef> in_arg_refs,
                    std::optional<SourceLocation> location = std::nullopt);
    CallInstruction(ValueRef callee_ref, std::size_t output_count, std::vector<ValueRef> in_arg_refs,
                    std::optional<SourceLocation> location = std::nullopt);

    /**
     * @brief 返回被调用函数名。
     */
    const std::string& name() const;

    /**
     * @brief 返回间接调用目标对应的 ValueRef；直接调用时为非法引用。
     */
    ValueRef callee_ref() const;

    /**
     * @brief 返回该调用是否通过运行时值决定目标。
     */
    bool is_indirect() const;

    /**
     * @brief 返回源码层显式请求的输出参数个数。
     */
    std::size_t output_count() const;

    /**
     * @brief 返回输入参数个数，优先以 ValueRef 列表为准。
     */
    std::size_t input_count() const;

    /**
     * @brief 返回第 `index` 个输入参数的 ValueRef；越界时返回非法引用。
     */
    ValueRef input_ref(std::size_t index) const;

    /**
     * @brief 返回输入参数对应的 ValueRef 列表。
     */
    const std::vector<ValueRef>& in_arg_refs() const;

private:
    std::string name_;
    ValueRef callee_ref_;
    std::size_t output_count_ = 0;
    std::vector<ValueRef> in_arg_refs_;
};

/**
 * @brief 条件分支终结指令。
 */
class CondJumpInstruction final : public Instruction {
public:
    CondJumpInstruction(ValueRef cond_ref, BasicBlock* true_block, BasicBlock* false_block,
                        std::optional<SourceLocation> location = std::nullopt);

    /**
     * @brief 返回条件为真时的目标块。
     */
    BasicBlock* true_block() const;

    /**
     * @brief 返回条件为假时的目标块。
     */
    BasicBlock* false_block() const;

    /**
     * @brief 返回条件值对应的 ValueRef。
     */
    ValueRef cond_ref() const;

private:
    BasicBlock* true_block_ = nullptr;
    BasicBlock* false_block_ = nullptr;
    ValueRef cond_ref_;
};

/**
 * @brief 无条件跳转终结指令。
 */
class JumpInstruction final : public Instruction {
public:
    explicit JumpInstruction(BasicBlock* target,
                             std::optional<SourceLocation> location = std::nullopt);

    /**
     * @brief 返回跳转目标块。
     */
    BasicBlock* target() const;

private:
    BasicBlock* target_ = nullptr;
};

/**
 * @brief 函数返回终结指令。
 */
class ReturnInstruction final : public Instruction {
public:
    explicit ReturnInstruction(std::vector<ValueRef> value_refs = {},
                               std::optional<SourceLocation> location = std::nullopt);

    /**
     * @brief 返回显式返回值个数，优先以 ValueRef 列表为准。
     */
    std::size_t return_value_count() const;

    /**
     * @brief 返回第 `index` 个返回值的 ValueRef；越界时返回非法引用。
     */
    ValueRef return_value_ref(std::size_t index) const;

    /**
     * @brief 返回显式返回值对应的 ValueRef 列表。
     */
    const std::vector<ValueRef>& value_refs() const;

private:
    std::vector<ValueRef> value_refs_;
};

/**
 * @brief 含有线性指令序列和一个终结指令的基本块。
 *
 * BasicBlock 的内存由 Function 持有；前驱和后继列表仅保存 CFG 的
 * 非 owning 链接。
 */
class BasicBlock {
public:
    explicit BasicBlock(std::string name);

    /**
     * @brief 判断给定块列表中是否包含指定基本块。
     */
    static bool contains_block(const std::vector<BasicBlock*>& blocks, const BasicBlock* target);

    /**
     * @brief 返回所属 Function；若尚未挂接则返回 nullptr。
     */
    Function* parent() const;

    /**
     * @brief 返回基本块名称。
     */
    const std::string& name() const;

    /**
     * @brief 返回非终结指令序列。
     */
    const std::vector<Instruction*>& instructions() const;

    /**
     * @brief 返回前驱基本块列表。
     */
    const std::vector<BasicBlock*>& predecessors() const;

    /**
     * @brief 返回后继基本块列表。
     */
    const std::vector<BasicBlock*>& successors() const;

    /**
     * @brief 返回该块的终结指令。
     */
    Instruction* terminal() const;

    /**
     * @brief 向块体追加一条普通指令。
     */
    void append_instruction(Instruction* instruction);

    /**
     * @brief 增加一个 CFG 后继，并同步更新对方的前驱列表。
     */
    void add_successor(BasicBlock* successor);

    /**
     * @brief 设置该块的终结指令。
     */
    void set_terminal(Instruction* instruction);

private:
    friend class Function;

    /**
     * @brief 将该块挂接到某个 Function。
     */
    void set_parent(Function* function);

    Function* parent_ = nullptr;
    std::string name_;
    std::vector<Instruction*> instructions_;
    std::vector<BasicBlock*> predecessors_;
    std::vector<BasicBlock*> successors_;
    Instruction* terminal_ = nullptr;
};

/**
 * @brief 函数级 IR 容器。
 *
 * Function 同时拥有 BasicBlock 和 Instruction 的内存；BasicBlock 内部
 * 只保存对指令的非 owning 指针。
 */
class Function {
public:
    enum Type {
        Script,
        PrimaryFunction,
        LocalFunction,
    };

    Function(std::string name, Type type);

    /**
     * @brief 返回函数名。
     */
    const std::string& name() const;

    /**
     * @brief 返回所属 Module；若尚未挂接则返回 nullptr。
     */
    Module* parent() const;

    /**
     * @brief 返回函数类型。
     */
    Type type() const;

    /**
     * @brief 返回输入参数名列表。
     */
    const std::vector<std::string>& input_names() const;

    /**
     * @brief 返回输入参数在 SSA IR 中对应的值定义。
     */
    const std::vector<InstValue>& input_values() const;

    /**
     * @brief 返回输出参数名列表。
     */
    const std::vector<std::string>& output_names() const;

    /**
     * @brief 返回函数入口基本块。
     */
    BasicBlock* entry_block() const;

    /**
     * @brief 按插入顺序返回本函数拥有的基本块。
     */
    const std::vector<std::unique_ptr<BasicBlock>>& blocks() const;

    /**
     * @brief 创建并接管一个新的 BasicBlock。
     */
    BasicBlock* create_block(std::string name);

    /**
     * @brief 将某个块标记为函数入口。
     */
    void set_entry_block(BasicBlock* block);

    /**
     * @brief 设置输入参数名列表。
     */
    void set_input_names(std::vector<std::string> names);

    /**
     * @brief 返回第 `index` 个输入参数的 ValueRef；越界时返回非法引用。
     */
    ValueRef input_ref(std::size_t index) const;

    /**
     * @brief 设置输出参数名列表。
     */
    void set_output_names(std::vector<std::string> names);

    /**
     * @brief 分配一个新的结果值定义。
     *
     * 该接口为向 value-based / SSA IR 迁移预留；当前阶段可以逐步让
     * 产值指令通过它获得稳定的 `ValueId`。
     */
    InstValue create_value(std::string debug_name = {},
                           std::optional<SourceLocation> location = std::nullopt);

    /**
     * @brief 为一条已有指令挂接一组值定义。
     */
    void attach_value_defs(Instruction& instruction, std::vector<InstValue> values);

    /**
     * @brief 为一条已有指令挂接一个单值定义并返回该值。
     */
    InstValue attach_single_value(Instruction& instruction, std::string debug_name = {},
                                  std::optional<SourceLocation> location = std::nullopt);

    /**
     * @brief 按 ValueId 查找本函数内对应的值定义元数据。
     */
    const InstValue* find_value(ValueId id) const;

    /**
     * @brief 按 ValueId 查找定义该值的指令；未找到时返回 nullptr。
     */
    Instruction* find_value_owner(ValueId id);

    /**
     * @brief 按 ValueId 查找定义该值的指令；未找到时返回 nullptr。
     */
    const Instruction* find_value_owner(ValueId id) const;

    /**
     * @brief 创建并接管一个新的指令对象。
     */
    template <typename T, typename... Args>
    T* create_instruction(Args&&... args) {
        auto instruction = std::make_unique<T>(std::forward<Args>(args)...);
        T* raw = instruction.get();
        instruction_storage_.push_back(std::move(instruction));
        return raw;
    }

private:
    friend class Module;

    /**
     * @brief 将该函数挂接到某个 Module。
     */
    void set_parent(Module* module);

    Module* parent_ = nullptr;
    std::string name_;
    Type type_ = PrimaryFunction;
    std::vector<std::string> input_names_;
    std::vector<InstValue> input_values_;
    std::vector<std::string> output_names_;
    std::vector<std::unique_ptr<BasicBlock>> block_storage_;
    std::vector<std::unique_ptr<Instruction>> instruction_storage_;
    BasicBlock* entry_block_ = nullptr;
    ValueId next_value_id_ = 1;
};

/**
 * @brief 源文件级别的顶层 IR 容器。
 */
class Module {
public:
    enum Type {
        M_Script,
        M_Function,
    };

    Module(std::string name, std::string source_path, Type type);

    /**
     * @brief 返回模块名。
     */
    const std::string& name() const;

    /**
     * @brief 返回模块类型。
     */
    Type type() const;

    /**
     * @brief 返回构建该模块所对应的源文件路径。
     */
    const std::string& source_path() const;

    /**
     * @brief 返回该模块的入口函数。
     */
    Function* entry_function() const;

    /**
     * @brief 按插入顺序返回本模块拥有的函数。
     */
    const std::vector<std::unique_ptr<Function>>& functions() const;

    /**
     * @brief 创建并接管一个新的 Function。
     */
    Function* create_function(std::string name, Function::Type type);

    /**
     * @brief 设置该模块的入口函数。
     */
    void set_entry_function(Function* function);

private:
    std::string name_;
    std::string source_path_;
    Type type_ = M_Function;
    std::vector<std::unique_ptr<Function>> function_storage_;
    Function* entry_function_ = nullptr;
};

/**
 * @brief 输出当前 IR 的文本表示。
 */
void print_ir(std::ostream& os, const Module& module);

}  // namespace baltam

#endif
