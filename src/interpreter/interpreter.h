#ifndef BALTAM_IR_INTERPRETER_INTERPRETER_H
#define BALTAM_IR_INTERPRETER_INTERPRETER_H

#include <memory>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "ba_obj/ba_obj.h"
#include "ir/ir.h"

namespace baltam::interpreter {

/**
 * @brief untyped SSA 解释器中的运行时值。
 *
 * 当前解释器只区分“有具体运行时对象”和“undef”两种状态。
 */
struct Value {
    /**
     * @brief 具体运行时对象句柄。
     */
    using Object = std::shared_ptr<ba_obj>;

    enum Type {
        Concrete,
        Undef,
    };

    Type type = Undef;
    Object object;
};

struct NamedBindingSnapshot {
    std::string name;
    ValueId value_id = InvalidValueId;
};

struct ExecutionOptions {
    std::ostream* trace_stream = nullptr;
    bool print_final_named_bindings = false;
};

/**
 * @brief 函数解释执行的结果。
 *
 * 除了函数 `ret` 返回值之外，也对外暴露执行后仍可观察到的 SSA 值表和
 * 具名绑定快照，方便测试和调试。
 */
struct ExecResult {
    std::vector<Value> outputs;
    std::unordered_map<ValueId, Value> values;
    std::vector<NamedBindingSnapshot> final_named_bindings;
};

/**
 * @brief 执行一个 `UntypedSSA` 函数。
 *
 * `args` 按函数 `argument_values()` 的顺序传入；返回值中的 `outputs`
 * 与函数 `ret` 的返回顺序一致。结构非法或运行时失败时抛出异常。
 */
ExecResult execute_function(Function& function, const std::vector<Value::Object>& args = {},
                            const ExecutionOptions& options = ExecutionOptions{});

}  // namespace baltam::interpreter

#endif
