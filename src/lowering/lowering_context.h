#ifndef BALTAM_IR_LOWERING_CONTEXT_H
#define BALTAM_IR_LOWERING_CONTEXT_H

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ir/ir.h"

namespace baltam {

/**
 * @brief lowering 过程中的可变上下文。
 *
 * 这个上下文集中保存“当前正在往哪里写 IR”以及“当前名字对应哪个
 * ValueRef”这两类状态：
 *
 * 1. CFG/构造状态
 * 当前基本块、块编号计数器、隐藏名字计数器。
 *
 * 2. 名字到值的映射
 * `symbol_table` 记录在当前 lowering 路径上，每个符号名当前对应的
 * `ValueRef`，用于把局部读取尽量 lower 成直接的 value use，而不是
 * 再回退成运行时绑定读取。
 *
 * 3. 循环控制流状态
 * `loop_stack` 保存当前嵌套循环的 break/continue 目标块。
 *
 * 同时，它也提供了一组高频成员函数，把“追加指令”“挂接结果值”
 * “同步符号表”等操作收敛到一个对象上，减少在 lowering.cpp 里来回
 * 跳转查找辅助函数的成本。
 */
struct LoweringContext {
    using SymbolTable = std::unordered_map<std::string, ValueRef>;

    /**
     * @brief 一条控制流路径在合流点需要带回的值快照。
     *
     * `predecessor` 表示这组值来自哪个前驱块；`value_refs` 按名字列表顺序
     * 保存该路径结束时每个名字当前对应的 ValueRef。
     */
    struct MergeSnapshot {
        BasicBlock* predecessor = nullptr;
        std::vector<ValueRef> value_refs;
    };

    /**
     * @brief 一层循环对应的控制流目标。
     *
     * `break_target` 和 `continue_target` 分别指向当前循环语义下
     * `break` 与 `continue` 应跳转到的基本块。若该循环正在做 loop-carried
     * SSA 合流，`carried_names` 和 `continue_snapshots` 会额外记录 continue
     * 路径需要回灌到循环头 phi 的名字列表与取值快照。
     */
    struct LoopContext {
        BasicBlock* break_target = nullptr;
        BasicBlock* continue_target = nullptr;
        const std::vector<std::string>* carried_names = nullptr;
        std::vector<MergeSnapshot>* continue_snapshots = nullptr;
        const std::vector<std::string>* break_names = nullptr;
        std::vector<MergeSnapshot>* break_snapshots = nullptr;
    };

    /**
     * @brief 当前正在追加 IR 的基本块。
     */
    BasicBlock* current_block = nullptr;

    /**
     * @brief 当前函数内下一个自动生成块编号。
     */
    int next_block_id = 0;

    /**
     * @brief 当前函数内下一个隐藏名字编号。
     */
    int next_hidden_id = 0;

    /**
     * @brief 当前 lowering 路径上的“符号名 -> ValueRef”映射。
     */
    SymbolTable symbol_table;

    /**
     * @brief 当前嵌套循环栈。
     */
    std::vector<LoopContext> loop_stack;

    /**
     * @brief 返回当前基本块所属函数。
     *
     * 若当前上下文尚未绑定到合法基本块，会抛出异常。
     */
    Function& function();

    /**
     * @brief 返回当前基本块所属函数的只读引用。
     */
    const Function& function() const;

    /**
     * @brief 为当前函数创建一个带自增编号的新基本块。
     *
     * @param prefix 基本块名前缀，例如 `if_true`。
     * @return 新创建的基本块。
     */
    BasicBlock* create_block(const std::string& prefix);

    /**
     * @brief 生成一个新的隐藏名字。
     *
     * @param prefix 隐藏名字前缀，例如 `switch_value`。
     * @return 形如 `__prefix_N` 的隐藏名字。
     */
    std::string create_hidden_name(const std::string& prefix);

    /**
     * @brief 查询某个符号名在当前路径上的 ValueRef。
     *
     * 若当前路径上该符号还没有已知值，则返回非法 `ValueRef`。
     */
    ValueRef lookup_symbol_value_ref(const std::string& name) const;

    /**
     * @brief 记录某个符号名在当前路径上对应的 ValueRef。
     */
    void bind_symbol_value(const std::string& name, ValueRef ref);

    /**
     * @brief 从当前路径符号表中移除某个符号名。
     */
    void erase_symbol_value(const std::string& name);

    /**
     * @brief 向当前基本块追加一条普通指令。
     */
    void append_instruction(Instruction* instruction);

    /**
     * @brief 给指令挂接一个结果值定义，并追加到当前基本块。
     *
     * @param instruction 待追加的产值指令。
     * @param debug_name 结果值的调试名；可为空。
     * @return 原指令指针，便于链式使用。
     */
    Instruction* append_valued_instruction(Instruction* instruction, std::string debug_name = {});

    /**
     * @brief 给指令挂接多个结果值定义，并追加到当前基本块。
     *
     * @param instruction 待追加的多返回值指令。
     * @param debug_names 各结果值的调试名；为空时默认挂一个匿名结果值。
     * @return 原指令指针，便于链式使用。
     */
    Instruction* append_valued_instruction(Instruction* instruction,
                                           const std::vector<std::string>& debug_names);

    /**
     * @brief 追加一条按 ValueRef 赋值的 `store`，并同步当前符号表。
     */
    void append_bound_assignment_from_ref(const std::string& name, ValueRef value_ref,
                                          std::optional<SourceLocation> location);

    /**
     * @brief 追加一条按指令结果赋值的 `store`，并同步当前符号表。
     */
    void append_bound_assignment(const std::string& name, Instruction* value,
                                 std::optional<SourceLocation> location);

    /**
     * @brief 追加一次运行时绑定读取，并为其挂接结果值。
     */
    Instruction* append_binding_instruction(const std::string& name,
                                            std::optional<SourceLocation> location);

    /**
     * @brief 创建一条单目运算指令。
     */
    UnaryOpInstruction* create_unaryop_instruction(UnaryOpInstruction::Type op,
                                                   Instruction* operand,
                                                   std::optional<SourceLocation> location);

    /**
     * @brief 直接基于 ValueRef 创建一条单目运算指令。
     */
    UnaryOpInstruction* create_unaryop_instruction(UnaryOpInstruction::Type op, ValueRef operand_ref,
                                                   std::optional<SourceLocation> location);

    /**
     * @brief 创建一条二元运算指令。
     */
    BinOpInstruction* create_binop_instruction(BinOpInstruction::Type op, Instruction* lhs,
                                               Instruction* rhs,
                                               std::optional<SourceLocation> location);

    /**
     * @brief 直接基于两个 ValueRef 创建一条二元运算指令。
     */
    BinOpInstruction* create_binop_instruction(BinOpInstruction::Type op, ValueRef lhs_ref,
                                               ValueRef rhs_ref,
                                               std::optional<SourceLocation> location);

    /**
     * @brief 创建一条调用指令。
     */
    CallInstruction* create_call_instruction(std::string name, std::size_t output_count,
                                             std::vector<Instruction*> in_args,
                                             std::optional<SourceLocation> location);

    /**
     * @brief 直接基于 ValueRef 列表创建一条调用指令。
     */
    CallInstruction* create_call_instruction(std::string name, std::size_t output_count,
                                             std::vector<ValueRef> in_arg_refs,
                                             std::optional<SourceLocation> location);

    /**
     * @brief 基于一个运行时 callee 值创建间接调用指令。
     */
    CallInstruction* create_call_instruction(ValueRef callee_ref, std::size_t output_count,
                                             std::vector<ValueRef> in_arg_refs,
                                             std::optional<SourceLocation> location);

    /**
     * @brief 创建一条条件跳转终结指令。
     */
    CondJumpInstruction* create_cond_jump_instruction(Instruction* cond, BasicBlock* true_block,
                                                      BasicBlock* false_block,
                                                      std::optional<SourceLocation> location);

    /**
     * @brief 直接基于条件 ValueRef 创建一条条件跳转终结指令。
     */
    CondJumpInstruction* create_cond_jump_instruction(ValueRef cond_ref, BasicBlock* true_block,
                                                      BasicBlock* false_block,
                                                      std::optional<SourceLocation> location);

    /**
     * @brief 创建一条返回终结指令。
     */
    ReturnInstruction* create_return_instruction(std::vector<Instruction*> values,
                                                 std::optional<SourceLocation> location);

    /**
     * @brief 直接基于返回值 ValueRef 列表创建一条返回终结指令。
     */
    ReturnInstruction* create_return_instruction(std::vector<ValueRef> value_refs,
                                                 std::optional<SourceLocation> location);

    // 这些成员函数当前仍保持 public，是因为 lowering.cpp 仍以文件内
    // 自由函数为主来组织 lowering 流程。如果继续把表达式/语句 lowering
    // 逐步内聚为 LoweringContext 的成员方法，再收窄为 private 会更合适。

private:
    /**
     * @brief 统一封装对当前函数 `create_instruction<T>()` 的转调。
     *
     * 具名的 `create_xxx_instruction()` 负责表达 lowering 语义，并在必要时
     * 做 `Instruction* -> ValueRef` 等参数转换；真正的指令构造由这里统一
     * 转发给当前函数，避免重复写样板代码。
     */
    template <typename InstT, typename... Args>
    InstT* create_instruction(Args&&... args) {
        return function().create_instruction<InstT>(std::forward<Args>(args)...);
    }
};

}  // namespace baltam

#endif
