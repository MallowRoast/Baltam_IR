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

/**
 * @brief 附着在 IR 指令上的源码位置信息。
 */
struct SourceSpan {
    std::string filename;
    int begin_line = 0;
    int begin_column = 0;
    int end_line = 0;
    int end_column = 0;
};

/**
 * @brief 所有 IR 指令的动态类型标签。
 */
enum class InstructionType {
    Text,
    Name,
    Number,
    BinOp,
    Asgn,
    Call,
    If,
    Jump,
    Return,
};

class BasicBlock;
class Function;

/**
 * @brief 所有 IR 指令的基类。
 *
 * 一条具体指令至多属于一个 BasicBlock。指令对象的内存由 Function
 * 统一持有，BasicBlock 中只保存非 owning 指针。
 */
class Instruction {
public:
    virtual ~Instruction() = default;

    /**
     * @brief 返回当前指令的具体类型。
     */
    InstructionType type() const;

    /**
     * @brief 返回所属 BasicBlock；若尚未挂接则返回 nullptr。
     */
    BasicBlock* parent() const;

    /**
     * @brief 返回可选的源码位置信息。
     */
    const std::optional<SourceSpan>& source_span() const;

protected:
    /**
     * @brief 用动态类型标签构造一条指令。
     */
    explicit Instruction(InstructionType type, std::optional<SourceSpan> span = std::nullopt);

private:
    friend class BasicBlock;

    /**
     * @brief 将指令挂接到某个 BasicBlock。
     */
    void set_parent(BasicBlock* block);

    InstructionType type_;
    BasicBlock* parent_ = nullptr;
    std::optional<SourceSpan> source_span_;
};

/**
 * @brief 自由文本形式的临时占位指令。
 */
class TextInstruction final : public Instruction {
public:
    explicit TextInstruction(std::string text, std::optional<SourceSpan> span = std::nullopt);

    /**
     * @brief 返回原始文本内容。
     */
    const std::string& text() const;

private:
    std::string text_;
};

/**
 * @brief 对应名字引用的 IR 节点，例如 `a` 或 `b`。
 */
class NameInstruction final : public Instruction {
public:
    explicit NameInstruction(std::string name, std::optional<SourceSpan> span = std::nullopt);

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
    using NumberValue = std::variant<bool, std::int64_t, double, std::complex<double>>;

    explicit NumberInstruction(bool value, std::optional<SourceSpan> span = std::nullopt);
    explicit NumberInstruction(std::int64_t value, std::optional<SourceSpan> span = std::nullopt);
    explicit NumberInstruction(double value, std::optional<SourceSpan> span = std::nullopt);
    explicit NumberInstruction(std::complex<double> value,
                               std::optional<SourceSpan> span = std::nullopt);

    /**
     * @brief 返回字面量载荷。
     */
    const NumberValue& value() const;

private:
    NumberValue value_;
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
    enum class Type {
        Add,
        Gt,
        Multiply,
    };

    BinOpInstruction(Type op, Instruction* lhs, Instruction* rhs,
                     std::optional<SourceSpan> span = std::nullopt);

    /**
     * @brief 返回二元运算种类。
     */
    Type op() const;

    /**
     * @brief 返回左操作数。
     */
    Instruction* lhs() const;

    /**
     * @brief 返回右操作数。
     */
    Instruction* rhs() const;

private:
    Type op_ = Type::Add;
    Instruction* lhs_ = nullptr;
    Instruction* rhs_ = nullptr;
};

/**
 * @brief 赋值指令，例如 `a = expr`。
 */
class AssignInstruction final : public Instruction {
public:
    AssignInstruction(std::string name, Instruction* value,
                      std::optional<SourceSpan> span = std::nullopt);

    /**
     * @brief 返回赋值目标变量名。
     */
    const std::string& name() const;

    /**
     * @brief 返回右侧被赋值的表达式。
     */
    Instruction* value() const;

private:
    std::string name_;
    Instruction* value_ = nullptr;
};

/**
 * @brief 带显式输入输出参数的函数调用指令。
 */
class CallInstruction final : public Instruction {
public:
    CallInstruction(std::string name, std::vector<Instruction*> out_args,
                    std::vector<Instruction*> in_args, std::optional<SourceSpan> span = std::nullopt);

    /**
     * @brief 返回被调用函数名。
     */
    const std::string& name() const;

    /**
     * @brief 返回输出参数列表。
     */
    const std::vector<Instruction*>& out_args() const;

    /**
     * @brief 返回输入参数列表。
     */
    const std::vector<Instruction*>& in_args() const;

private:
    std::string name_;
    std::vector<Instruction*> out_args_;
    std::vector<Instruction*> in_args_;
};

/**
 * @brief 条件分支终结指令。
 */
class IfInstruction final : public Instruction {
public:
    IfInstruction(Instruction* cond, BasicBlock* true_block, BasicBlock* false_block,
                  std::optional<SourceSpan> span = std::nullopt);

    /**
     * @brief 返回分支条件指令。
     */
    Instruction* cond() const;

    /**
     * @brief 返回条件为真时的目标块。
     */
    BasicBlock* true_block() const;

    /**
     * @brief 返回条件为假时的目标块。
     */
    BasicBlock* false_block() const;

private:
    Instruction* cond_ = nullptr;
    BasicBlock* true_block_ = nullptr;
    BasicBlock* false_block_ = nullptr;
};

/**
 * @brief 无条件跳转终结指令。
 */
class JumpInstruction final : public Instruction {
public:
    explicit JumpInstruction(BasicBlock* target, std::optional<SourceSpan> span = std::nullopt);

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
    explicit ReturnInstruction(std::vector<Instruction*> values = {},
                               std::optional<SourceSpan> span = std::nullopt);

    /**
     * @brief 返回返回值列表。
     */
    const std::vector<Instruction*>& values() const;

private:
    std::vector<Instruction*> values_;
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
    explicit Function(std::string name);

    /**
     * @brief 返回函数名。
     */
    const std::string& name() const;

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
    std::string name_;
    std::vector<std::unique_ptr<BasicBlock>> block_storage_;
    std::vector<std::unique_ptr<Instruction>> instruction_storage_;
    BasicBlock* entry_block_ = nullptr;
};

/**
 * @brief 源文件级别的顶层 IR 容器。
 */
class Module {
public:
    Module(std::string name, std::string source_path);

    /**
     * @brief 返回模块名。
     */
    const std::string& name() const;

    /**
     * @brief 返回构建该模块所对应的源文件路径。
     */
    const std::string& source_path() const;

    /**
     * @brief 按插入顺序返回本模块拥有的函数。
     */
    const std::vector<std::unique_ptr<Function>>& functions() const;

    /**
     * @brief 创建并接管一个新的 Function。
     */
    Function* create_function(std::string name);

private:
    std::string name_;
    std::string source_path_;
    std::vector<std::unique_ptr<Function>> function_storage_;
};

/**
 * @brief 为 `test/simple_demo.m` 构造一份手写的示例 IR。
 */
Module build_demo(const std::string& source_path);

/**
 * @brief 输出当前 IR 的文本表示。
 */
void print_ir(std::ostream& os, const Module& module);

}  // namespace baltam

#endif
