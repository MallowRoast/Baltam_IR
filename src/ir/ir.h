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

#include "ir/integer_constant.h"

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

using ValueId = std::uint32_t;
constexpr ValueId InvalidValueId = 0;

using NumberValue = std::variant<bool, IntegerConstant, double, std::complex<double>>;

/**
 * @brief 一元运算节点共享的操作码类型。
 */
enum class UnaryOpType {
    Logic_Not,
    UPlus,
    UMinus,
    Transpose,
    CTranspose,
};

/**
 * @brief 二元运算节点共享的操作码类型。
 */
enum class BinOpType {
    Add,
    Subtract,
    Eq,
    Ge,
    Gt,
    Le,
    Lt,
    Ne,
    And,
    Or,
    Power,
    LDivide,
    MLeftDivide,
    MPower,
    MRightDivide,
    Times,
    Multiply,
    RDivide,
};

class IRNode;
class BasicBlock;
class Function;
class Module;

/**
 * @brief SSA 阶段里对一个值定义的轻量引用。
 */
struct ValueRef {
    ValueId id = InvalidValueId;

    bool valid() const;
};

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

    enum Type {
        Number,
        Text,
        Assign,
        GlobalLoad,
        GlobalStore,
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

using NamedValue = NonSSANode::NamedValue;

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
 * @brief 所有 untyped SSA 节点的公共基类。
 */
class UntypedSSANode : public SSANode {
public:
    enum Type {
        SSA_Number,
        SSA_Text,
        SSA_Undef,
        SSA_Phi,
        SSA_Copy,
        SSA_GlobalLoad,
        SSA_GlobalStore,
        SSA_UnaryOp,
        SSA_BinOp,
        SSA_Call,
        SSA_CondJump,
        SSA_Jump,
        SSA_Return,
    };

    Type type() const;

protected:
    explicit UntypedSSANode(Type type, std::optional<SourceLocation> location = std::nullopt);

private:
    Type type_ = SSA_Number;
};

/**
 * @brief 数值常量节点。
 */
class NumberNode final : public NonSSANode {
public:
    using NumberValue = baltam::NumberValue;

    NumberNode(NamedValue result, NumberValue value,
               std::optional<SourceLocation> location = std::nullopt);
    NumberNode(NamedValue result, bool value,
               std::optional<SourceLocation> location = std::nullopt);
    NumberNode(NamedValue result, std::int8_t value,
               std::optional<SourceLocation> location = std::nullopt);
    NumberNode(NamedValue result, std::int16_t value,
               std::optional<SourceLocation> location = std::nullopt);
    NumberNode(NamedValue result, std::int32_t value,
               std::optional<SourceLocation> location = std::nullopt);
    NumberNode(NamedValue result, std::int64_t value,
               std::optional<SourceLocation> location = std::nullopt);
    NumberNode(NamedValue result, std::uint8_t value,
               std::optional<SourceLocation> location = std::nullopt);
    NumberNode(NamedValue result, std::uint16_t value,
               std::optional<SourceLocation> location = std::nullopt);
    NumberNode(NamedValue result, std::uint32_t value,
               std::optional<SourceLocation> location = std::nullopt);
    NumberNode(NamedValue result, std::uint64_t value,
               std::optional<SourceLocation> location = std::nullopt);
    NumberNode(NamedValue result, double value,
               std::optional<SourceLocation> location = std::nullopt);
    NumberNode(NamedValue result, std::complex<double> value,
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
 * @brief 从 global 工作区读取一个具名值。
 */
class GlobalLoadNode final : public NonSSANode {
public:
    GlobalLoadNode(NamedValue result, std::string symbol,
                   std::optional<SourceLocation> location = std::nullopt);

    const NamedValue& result() const;
    const std::string& symbol() const;

private:
    NamedValue result_;
    std::string symbol_;
};

/**
 * @brief 把一个值写回 global 工作区。
 */
class GlobalStoreNode final : public NonSSANode {
public:
    GlobalStoreNode(std::string symbol, NamedValue value,
                    std::optional<SourceLocation> location = std::nullopt);

    const std::string& symbol() const;
    const NamedValue& value() const;

private:
    std::string symbol_;
    NamedValue value_;
};

/**
 * @brief 单目运算节点。
 */
class UnaryOpNode final : public NonSSANode {
public:
    UnaryOpNode(UnaryOpType op, NamedValue result, NamedValue operand,
                std::optional<SourceLocation> location = std::nullopt);

    UnaryOpType op() const;
    const NamedValue& result() const;
    const NamedValue& operand() const;

private:
    UnaryOpType op_ = UnaryOpType::UMinus;
    NamedValue result_;
    NamedValue operand_;
};

/**
 * @brief 二元运算节点。
 */
class BinOpNode final : public NonSSANode {
public:
    BinOpNode(BinOpType op, NamedValue result, NamedValue lhs, NamedValue rhs,
              std::optional<SourceLocation> location = std::nullopt);

    BinOpType op() const;
    const NamedValue& result() const;
    const NamedValue& lhs() const;
    const NamedValue& rhs() const;

private:
    BinOpType op_ = BinOpType::Add;
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
 * @brief untyped SSA 数值常量节点。
 */
class SSANumberNode final : public UntypedSSANode {
public:
    using NumberValue = baltam::NumberValue;

    SSANumberNode(ValueId result, NumberValue value,
                  std::optional<SourceLocation> location = std::nullopt);
    SSANumberNode(ValueId result, bool value,
                  std::optional<SourceLocation> location = std::nullopt);
    SSANumberNode(ValueId result, std::int8_t value,
                  std::optional<SourceLocation> location = std::nullopt);
    SSANumberNode(ValueId result, std::int16_t value,
                  std::optional<SourceLocation> location = std::nullopt);
    SSANumberNode(ValueId result, std::int32_t value,
                  std::optional<SourceLocation> location = std::nullopt);
    SSANumberNode(ValueId result, std::int64_t value,
                  std::optional<SourceLocation> location = std::nullopt);
    SSANumberNode(ValueId result, std::uint8_t value,
                  std::optional<SourceLocation> location = std::nullopt);
    SSANumberNode(ValueId result, std::uint16_t value,
                  std::optional<SourceLocation> location = std::nullopt);
    SSANumberNode(ValueId result, std::uint32_t value,
                  std::optional<SourceLocation> location = std::nullopt);
    SSANumberNode(ValueId result, std::uint64_t value,
                  std::optional<SourceLocation> location = std::nullopt);
    SSANumberNode(ValueId result, double value,
                  std::optional<SourceLocation> location = std::nullopt);
    SSANumberNode(ValueId result, std::complex<double> value,
                  std::optional<SourceLocation> location = std::nullopt);

    ValueId result() const;
    const NumberValue& value() const;

private:
    ValueId result_ = InvalidValueId;
    NumberValue value_;
};

/**
 * @brief untyped SSA 文本常量节点。
 */
class SSATextNode final : public UntypedSSANode {
public:
    SSATextNode(ValueId result, std::string text,
                std::optional<SourceLocation> location = std::nullopt);

    ValueId result() const;
    const std::string& text() const;

private:
    ValueId result_ = InvalidValueId;
    std::string text_;
};

/**
 * @brief untyped SSA 未定义值节点。
 */
class SSAUndefNode final : public UntypedSSANode {
public:
    explicit SSAUndefNode(ValueId result,
                          std::optional<SourceLocation> location = std::nullopt);

    ValueId result() const;

private:
    ValueId result_ = InvalidValueId;
};

/**
 * @brief untyped SSA phi 节点。
 */
class SSAPhiNode final : public UntypedSSANode {
public:
    struct Incoming {
        BasicBlock* predecessor = nullptr;
        ValueRef value;
    };

    explicit SSAPhiNode(ValueId result, std::vector<Incoming> incomings = {},
                        std::optional<SourceLocation> location = std::nullopt);

    ValueId result() const;
    const std::vector<Incoming>& incomings() const;
    void add_incoming(BasicBlock* predecessor, ValueRef value);
    bool remove_incoming(BasicBlock* predecessor);
    bool replace_predecessor(BasicBlock* old_predecessor, BasicBlock* new_predecessor);
    bool replace_value(ValueRef old_value, ValueRef new_value);

private:
    ValueId result_ = InvalidValueId;
    std::vector<Incoming> incomings_;
};

/**
 * @brief untyped SSA 纯复制节点。
 */
class SSACopyNode final : public UntypedSSANode {
public:
    SSACopyNode(ValueId result, ValueRef src,
                std::optional<SourceLocation> location = std::nullopt);

    ValueId result() const;
    ValueRef src() const;
    void set_src(ValueRef src);

private:
    ValueId result_ = InvalidValueId;
    ValueRef src_;
};

/**
 * @brief untyped SSA global 读取节点。
 */
class SSAGlobalLoadNode final : public UntypedSSANode {
public:
    SSAGlobalLoadNode(ValueId result, std::string symbol,
                      std::optional<SourceLocation> location = std::nullopt);

    ValueId result() const;
    const std::string& symbol() const;

private:
    ValueId result_ = InvalidValueId;
    std::string symbol_;
};

/**
 * @brief untyped SSA global 写回节点。
 */
class SSAGlobalStoreNode final : public UntypedSSANode {
public:
    SSAGlobalStoreNode(std::string symbol, ValueRef value,
                       std::optional<SourceLocation> location = std::nullopt);

    const std::string& symbol() const;
    ValueRef value() const;
    void set_value(ValueRef value);

private:
    std::string symbol_;
    ValueRef value_;
};

/**
 * @brief untyped SSA 单目运算节点。
 */
class SSAUnaryOpNode final : public UntypedSSANode {
public:
    using Op = UnaryOpType;

    SSAUnaryOpNode(Op op, ValueId result, ValueRef operand,
                   std::optional<SourceLocation> location = std::nullopt);

    Op op() const;
    ValueId result() const;
    ValueRef operand() const;
    void set_operand(ValueRef operand);

private:
    Op op_ = Op::UMinus;
    ValueId result_ = InvalidValueId;
    ValueRef operand_;
};

/**
 * @brief untyped SSA 二元运算节点。
 */
class SSABinOpNode final : public UntypedSSANode {
public:
    using Op = BinOpType;

    SSABinOpNode(Op op, ValueId result, ValueRef lhs, ValueRef rhs,
                 std::optional<SourceLocation> location = std::nullopt);

    Op op() const;
    ValueId result() const;
    ValueRef lhs() const;
    ValueRef rhs() const;
    void set_lhs(ValueRef lhs);
    void set_rhs(ValueRef rhs);

private:
    Op op_ = Op::Add;
    ValueId result_ = InvalidValueId;
    ValueRef lhs_;
    ValueRef rhs_;
};

/**
 * @brief untyped SSA 调用节点。
 */
class SSACallNode final : public UntypedSSANode {
public:
    struct Callee {
        enum Type {
            Direct,
            Indirect,
        };

        Type type = Direct;
        std::string direct_symbol;
        ValueRef indirect_value;
    };

    SSACallNode(Callee callee, std::vector<ValueId> results, std::vector<ValueRef> inputs,
                std::optional<SourceLocation> location = std::nullopt);

    const Callee& callee() const;
    const std::vector<ValueId>& results() const;
    const std::vector<ValueRef>& inputs() const;
    void set_callee_indirect_value(ValueRef indirect_value);
    void set_input(std::size_t index, ValueRef input);

private:
    Callee callee_;
    std::vector<ValueId> results_;
    std::vector<ValueRef> inputs_;
};

/**
 * @brief untyped SSA 条件跳转终结节点。
 */
class SSACondJumpNode final : public UntypedSSANode {
public:
    SSACondJumpNode(ValueRef cond, BasicBlock* true_block, BasicBlock* false_block,
                    std::optional<SourceLocation> location = std::nullopt);

    ValueRef cond() const;
    BasicBlock* true_block() const;
    BasicBlock* false_block() const;
    void set_cond(ValueRef cond);

private:
    ValueRef cond_;
    BasicBlock* true_block_ = nullptr;
    BasicBlock* false_block_ = nullptr;
};

/**
 * @brief untyped SSA 无条件跳转终结节点。
 */
class SSAJumpNode final : public UntypedSSANode {
public:
    explicit SSAJumpNode(BasicBlock* target,
                         std::optional<SourceLocation> location = std::nullopt);

    BasicBlock* target() const;

private:
    BasicBlock* target_ = nullptr;
};

/**
 * @brief untyped SSA 返回终结节点。
 */
class SSAReturnNode final : public UntypedSSANode {
public:
    explicit SSAReturnNode(std::vector<ValueRef> values,
                           std::optional<SourceLocation> location = std::nullopt);

    const std::vector<ValueRef>& values() const;
    void set_value(std::size_t index, ValueRef value);

private:
    std::vector<ValueRef> values_;
};

/**
 * @brief 分阶段 IR 共用的基本块容器。
 *
 * 当前实际主要承载 non-SSA 节点，但容器层本身不再把接口写死到
 * `NonSSANode`，从而为后续复用到 SSA 阶段预留空间。
 */
class BasicBlock {
public:
    explicit BasicBlock(std::string name);

    static bool contains_block(const std::vector<BasicBlock*>& blocks, const BasicBlock* target);

    Function* parent() const;
    const std::string& name() const;
    const std::vector<IRNode*>& phi_nodes() const;
    const std::vector<IRNode*>& instructions() const;
    const std::vector<BasicBlock*>& predecessors() const;
    const std::vector<BasicBlock*>& successors() const;
    IRNode* terminal() const;

    void append_phi(IRNode* node);
    bool erase_phi(IRNode* node);
    void prepend_instruction(IRNode* node);
    void append_instruction(IRNode* node);
    std::vector<IRNode*> release_instructions();
    bool erase_instruction(IRNode* node);
    bool replace_instruction(IRNode* old_node, IRNode* new_node);
    void add_successor(BasicBlock* successor);
    bool remove_successor(BasicBlock* successor);
    IRNode* release_terminal();
    void set_terminal(IRNode* node);

private:
    friend class Function;

    void set_parent(Function* function);

    Function* parent_ = nullptr;
    std::string name_;
    std::vector<IRNode*> phi_nodes_;
    std::vector<IRNode*> instructions_;
    std::vector<BasicBlock*> predecessors_;
    std::vector<BasicBlock*> successors_;
    IRNode* terminal_ = nullptr;
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
    IRNode::Stage stage() const;
    const std::vector<NamedValue>& inputs() const;
    const std::vector<NamedValue>& outputs() const;
    const std::vector<ValueId>& argument_values() const;
    bool has_varargin() const;
    bool has_varargout() const;
    std::size_t fixed_input_count() const;
    std::size_t fixed_output_count() const;
    std::size_t value_count() const;
    bool has_value(ValueId id) const;
    bool is_user_visible_value(ValueId id) const;
    const std::string* find_value_debug_name(ValueId id) const;
    BasicBlock* entry_block() const;
    const std::vector<std::unique_ptr<BasicBlock>>& blocks() const;

    BasicBlock* create_block(std::string name);
    bool erase_block(BasicBlock* block);
    void set_entry_block(BasicBlock* block);
    void set_stage(IRNode::Stage stage);
    void set_input_names(std::vector<std::string> names);
    void set_output_names(std::vector<std::string> names);
    void set_has_varargin(bool has_varargin);
    void set_has_varargout(bool has_varargout);
    void set_argument_values(std::vector<ValueId> argument_values);
    ValueId create_value(std::string debug_name = {}, bool is_user_visible = false);
    bool erase_value(ValueId id);
    void set_value_debug_name(ValueId id, std::string debug_name);

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
    IRNode::Stage stage_ = IRNode::NonSSA;
    std::vector<NamedValue> inputs_;
    std::vector<NamedValue> outputs_;
    bool has_varargin_ = false;
    bool has_varargout_ = false;
    std::vector<ValueId> argument_values_;
    std::vector<std::optional<std::string>> value_debug_names_;
    std::vector<bool> value_user_visible_flags_;
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
    Module(Module&& other) noexcept;
    Module& operator=(Module&& other) noexcept;

    Module(const Module&) = delete;
    Module& operator=(const Module&) = delete;

    const std::string& name() const;
    Type type() const;
    const std::string& source_path() const;
    Function* entry_function() const;
    const std::vector<std::unique_ptr<Function>>& functions() const;

    Function* create_function(std::string name, Function::Type type);
    void set_entry_function(Function* function);

private:
    void rebind_function_parents();

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
