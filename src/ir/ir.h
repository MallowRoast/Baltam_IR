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
 * @brief 附着在 IR 节点上的源码位置信息。
 */
struct SourceLocation {
    std::string filename;
    int begin_line = 0;
    int begin_column = 0;
    int end_line = 0;
    int end_column = 0;
};

/**
 * @brief non-SSA 阶段里对一个具名值的轻量描述。
 */
struct NamedValue {
    enum Type {
        UserVariable,
        Temporary,
    };

    std::string name;
    Type type = UserVariable;
};

class BasicBlock;
class Function;
class Module;

/**
 * @brief 分阶段 IR 体系中的公共节点基类。
 *
 * 当前实际只实现 non-SSA 阶段，但公共基类先固定 stage、parent 和
 * source location，避免后续接 SSA 时再重改所有节点接口。
 */
class IRNode {
public:
    enum Stage {
        NonSSA,
        UntypedSSA,
        TypedSSA,
    };

    virtual ~IRNode() = default;

    Stage stage() const;
    BasicBlock* parent() const;
    const std::optional<SourceLocation>& source_location() const;

protected:
    explicit IRNode(Stage stage, std::optional<SourceLocation> location = std::nullopt);

private:
    friend class BasicBlock;

    void set_parent(BasicBlock* block);

    Stage stage_ = NonSSA;
    BasicBlock* parent_ = nullptr;
    std::optional<SourceLocation> source_location_;
};

/**
 * @brief 所有 non-SSA IR 节点的公共基类。
 *
 * 这一层是线性语句 IR，而不是表达式树 IR。带结果的节点直接把结果写到
 * 某个 `NamedValue`，不会把 rhs 再嵌成子节点。
 */
class NonSSANode : public IRNode {
public:
    enum Type {
        Number,
        Text,
        Assign,
        UnaryOp,
        BinOp,
        Call,
        CondJump,
        Jump,
        Return,
    };

    Type type() const;

protected:
    explicit NonSSANode(Type type, std::optional<SourceLocation> location = std::nullopt);

private:
    Type type_ = Number;
};

/**
 * @brief SSA 节点的占位基类。
 *
 * 当前不实现 untyped SSA / typed SSA 节点，只保留层次，方便后续接上。
 */
class SSANode : public IRNode {
protected:
    explicit SSANode(Stage stage, std::optional<SourceLocation> location = std::nullopt);
};

/**
 * @brief 数值常量节点。
 */
class NumberNode final : public NonSSANode {
public:
    using NumberValue =
        std::variant<bool, std::int64_t, std::uint64_t, double, std::complex<double>>;

    NumberNode(NamedValue result, NumberValue value,
               std::optional<SourceLocation> location = std::nullopt);

    const NamedValue& result() const;
    const NumberValue& value() const;

private:
    NamedValue result_;
    NumberValue value_;
};

/**
 * @brief 文本常量节点。
 */
class TextNode final : public NonSSANode {
public:
    TextNode(NamedValue result, std::string text,
             std::optional<SourceLocation> location = std::nullopt);

    const NamedValue& result() const;
    const std::string& text() const;

private:
    NamedValue result_;
    std::string text_;
};

/**
 * @brief 名字到名字的赋值节点。
 */
class AssignNode final : public NonSSANode {
public:
    AssignNode(NamedValue dst, NamedValue src,
               std::optional<SourceLocation> location = std::nullopt);

    const NamedValue& dst() const;
    const NamedValue& src() const;

private:
    NamedValue dst_;
    NamedValue src_;
};

/**
 * @brief 单目运算节点。
 */
class UnaryOpNode final : public NonSSANode {
public:
    enum Op {
        Logic_Not,
        UMinus,
    };

    UnaryOpNode(Op op, NamedValue result, NamedValue operand,
                std::optional<SourceLocation> location = std::nullopt);

    Op op() const;
    const NamedValue& result() const;
    const NamedValue& operand() const;

private:
    Op op_ = UMinus;
    NamedValue result_;
    NamedValue operand_;
};

/**
 * @brief 二元运算节点。
 */
class BinOpNode final : public NonSSANode {
public:
    enum Op {
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

    BinOpNode(Op op, NamedValue result, NamedValue lhs, NamedValue rhs,
              std::optional<SourceLocation> location = std::nullopt);

    Op op() const;
    const NamedValue& result() const;
    const NamedValue& lhs() const;
    const NamedValue& rhs() const;

private:
    Op op_ = Add;
    NamedValue result_;
    NamedValue lhs_;
    NamedValue rhs_;
};

/**
 * @brief 函数调用节点。
 *
 * 直接调用使用函数名，间接调用使用保存 callee 的名字文本。
 */
class CallNode final : public NonSSANode {
public:
    enum CalleeType {
        Direct,
        Indirect,
    };

    CallNode(CalleeType callee_type, std::string callee, std::vector<NamedValue> outputs,
             std::vector<NamedValue> inputs,
             std::optional<SourceLocation> location = std::nullopt);

    CalleeType callee_type() const;
    const std::string& callee() const;
    const std::vector<NamedValue>& outputs() const;
    const std::vector<NamedValue>& inputs() const;

private:
    CalleeType callee_type_ = Direct;
    std::string callee_;
    std::vector<NamedValue> outputs_;
    std::vector<NamedValue> inputs_;
};

/**
 * @brief 条件跳转终结节点。
 */
class CondJumpNode final : public NonSSANode {
public:
    CondJumpNode(NamedValue cond, BasicBlock* true_block, BasicBlock* false_block,
                 std::optional<SourceLocation> location = std::nullopt);

    const NamedValue& cond() const;
    BasicBlock* true_block() const;
    BasicBlock* false_block() const;

private:
    NamedValue cond_;
    BasicBlock* true_block_ = nullptr;
    BasicBlock* false_block_ = nullptr;
};

/**
 * @brief 无条件跳转终结节点。
 */
class JumpNode final : public NonSSANode {
public:
    explicit JumpNode(BasicBlock* target, std::optional<SourceLocation> location = std::nullopt);

    BasicBlock* target() const;

private:
    BasicBlock* target_ = nullptr;
};

/**
 * @brief 函数返回终结节点。
 */
class ReturnNode final : public NonSSANode {
public:
    explicit ReturnNode(std::vector<NamedValue> values,
                        std::optional<SourceLocation> location = std::nullopt);

    const std::vector<NamedValue>& values() const;

private:
    std::vector<NamedValue> values_;
};

/**
 * @brief non-SSA 基本块。
 *
 * 结构保持和旧 IR 基本一致：线性节点序列加一个终结节点，以及前驱后继边。
 */
class BasicBlock {
public:
    explicit BasicBlock(std::string name);

    static bool contains_block(const std::vector<BasicBlock*>& blocks, const BasicBlock* target);

    Function* parent() const;
    const std::string& name() const;
    const std::vector<NonSSANode*>& instructions() const;
    const std::vector<BasicBlock*>& predecessors() const;
    const std::vector<BasicBlock*>& successors() const;
    NonSSANode* terminal() const;

    void append_instruction(NonSSANode* node);
    void add_successor(BasicBlock* successor);
    void set_terminal(NonSSANode* node);

private:
    friend class Function;

    void set_parent(Function* function);

    Function* parent_ = nullptr;
    std::string name_;
    std::vector<NonSSANode*> instructions_;
    std::vector<BasicBlock*> predecessors_;
    std::vector<BasicBlock*> successors_;
    NonSSANode* terminal_ = nullptr;
};

/**
 * @brief 函数级 IR 容器。
 */
class Function {
public:
    enum Type {
        Script,
        PrimaryFunction,
        LocalFunction,
    };

    Function(std::string name, Type type);

    const std::string& name() const;
    Module* parent() const;
    Type type() const;
    const std::vector<NamedValue>& inputs() const;
    const std::vector<NamedValue>& outputs() const;
    BasicBlock* entry_block() const;
    const std::vector<std::unique_ptr<BasicBlock>>& blocks() const;

    BasicBlock* create_block(std::string name);
    void set_entry_block(BasicBlock* block);
    void set_input_names(std::vector<std::string> names);
    void set_output_names(std::vector<std::string> names);

    template <typename T, typename... Args>
    T* create_node(Args&&... args) {
        auto node = std::make_unique<T>(std::forward<Args>(args)...);
        T* raw = node.get();
        node_storage_.push_back(std::move(node));
        return raw;
    }

private:
    friend class Module;

    void set_parent(Module* module);

    Module* parent_ = nullptr;
    std::string name_;
    Type type_ = PrimaryFunction;
    std::vector<NamedValue> inputs_;
    std::vector<NamedValue> outputs_;
    std::vector<std::unique_ptr<BasicBlock>> block_storage_;
    std::vector<std::unique_ptr<IRNode>> node_storage_;
    BasicBlock* entry_block_ = nullptr;
};

/**
 * @brief 源文件级的顶层 IR 容器。
 */
class Module {
public:
    enum Type {
        M_Script,
        M_Function,
    };

    Module(std::string name, std::string source_path, Type type);

    const std::string& name() const;
    Type type() const;
    const std::string& source_path() const;
    Function* entry_function() const;
    const std::vector<std::unique_ptr<Function>>& functions() const;

    Function* create_function(std::string name, Function::Type type);
    void set_entry_function(Function* function);

private:
    std::string name_;
    std::string source_path_;
    Type type_ = M_Function;
    std::vector<std::unique_ptr<Function>> function_storage_;
    Function* entry_function_ = nullptr;
};

/**
 * @brief 以接近 LLVM IR 的文本风格打印当前 IR。
 */
void print_ir(std::ostream& os, const Module& module);

}  // namespace baltam

#endif
