#include "interpreter/interpreter.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "analysis/verifier.h"
#include "ba_obj/ba_obj.h"
#include "ba_obj/cell.h"
#include "ba_obj/function_handle.h"
#include "baltam_worker/builtin_manager.h"

namespace baltam::interpreter {
namespace {

using ValueTable = std::unordered_map<ValueId, Value>;

struct ExecutionState {
    Function& function;
    ValueTable values;
    std::vector<Value> outputs;
    BasicBlock* predecessor = nullptr;

    const Value& load_value(ValueRef ref) const;
    const Value& require_concrete_value(ValueRef ref, const char* context) const;
    Value::Object require_concrete_object(ValueRef ref, const char* context) const;
    void store_value(ValueId id, Value value);
};

enum class CallableType {
    Builtin,
    Internal,
};

std::string value_label(const Function& function, ValueId id) {
    const std::string* debug_name = function.find_value_debug_name(id);
    if (debug_name != nullptr && !debug_name->empty()) {
        return "%" + *debug_name;
    }
    return "%" + std::to_string(id);
}

std::string block_label(const BasicBlock* block) {
    return block != nullptr ? block->name() : "<entry>";
}

const Value& ExecutionState::load_value(ValueRef ref) const {
    if (!ref.valid()) {
        throw std::runtime_error("尝试读取非法的 `ValueRef`。");
    }

    auto it = values.find(ref.id);
    if (it == values.end()) {
        throw std::runtime_error("函数 `" + function.name() + "` 读取了未定义的 SSA 值 `" +
                                 value_label(function, ref.id) + "`。");
    }
    return it->second;
}

const Value& ExecutionState::require_concrete_value(ValueRef ref, const char* context) const {
    const Value& value = load_value(ref);
    if (value.type == Value::Undef) {
        throw std::runtime_error("函数 `" + function.name() + "` 在 " + context +
                                 " 使用了 `undef` SSA 值 `" +
                                 value_label(function, ref.id) + "`。");
    }
    return value;
}

Value::Object ExecutionState::require_concrete_object(ValueRef ref, const char* context) const {
    return require_concrete_value(ref, context).object;
}

void ExecutionState::store_value(ValueId id, Value value) {
    if (id == InvalidValueId) {
        throw std::runtime_error("函数 `" + function.name() + "` 试图写入非法的 SSA 值。");
    }
    values[id] = std::move(value);
}

Value concrete(Value::Object object) {
    return Value{Value::Concrete, std::move(object)};
}

Value undef() {
    return Value{Value::Undef, nullptr};
}

bool condition_value_as_bool(const Value::Object& value) {
    if (value == nullptr) {
        throw std::runtime_error("条件值为空。");
    }

    try {
        return value->as_bool();
    } catch (const std::exception& ex) {
        throw std::runtime_error(std::string("条件值不能转成 bool：") + ex.what());
    }
}

const char* callable_type_text(CallableType type) {
    switch (type) {
        case CallableType::Builtin:
            return "内置函数";
        case CallableType::Internal:
            return "内部函数";
    }
    return "函数";
}

std::unordered_map<std::string, baFunPtr>& builtin_function_cache() {
    static std::unordered_map<std::string, baFunPtr> cache;
    return cache;
}

std::unordered_map<std::string, baFunPtr>& internal_function_cache() {
    static std::unordered_map<std::string, baFunPtr> cache;
    return cache;
}

bool try_lookup_builtin_function_cached(const std::string& name, baFunPtr& function_ptr) {
    auto& cache = builtin_function_cache();
    auto it = cache.find(name);
    if (it != cache.end()) {
        function_ptr = it->second;
        return function_ptr != nullptr;
    }

    function_ptr = nullptr;
    if (!lookup_builtin_function(name, function_ptr) || function_ptr == nullptr) {
        function_ptr = nullptr;
        cache.emplace(name, nullptr);
        return false;
    }

    cache.emplace(name, function_ptr);
    return true;
}

bool try_lookup_internal_function_cached(const std::string& name, baFunPtr& function_ptr) {
    auto& cache = internal_function_cache();
    auto it = cache.find(name);
    if (it != cache.end()) {
        function_ptr = it->second;
        return function_ptr != nullptr;
    }

    function_ptr = lookup_internal_function(name.c_str());
    cache.emplace(name, function_ptr);
    return function_ptr != nullptr;
}

std::size_t required_builtin_out_count(const std::string& name) {
    const auto [declared_nargin, declared_nargout] = lookup_builtin_function_narg(name);
    (void)declared_nargin;
    if (declared_nargout < 0) {
        return 0;
    }
    return static_cast<std::size_t>(declared_nargout);
}

std::vector<Value::Object> invoke_function_ptr(const std::string& name, baFunPtr function_ptr,
                                               const std::vector<Value::Object>& in_args,
                                               std::size_t out_count, CallableType type) {
    std::vector<const_ba_obj_ptr> runtime_in_args;
    runtime_in_args.reserve(in_args.size());
    for (const Value::Object& arg : in_args) {
        runtime_in_args.push_back(arg);
    }

    const bool use_ans_placeholder = out_count == 0;
    std::vector<Value::Object> out_args(use_ans_placeholder ? 1 : out_count);
    for (std::size_t i = 0; i < out_args.size(); ++i) {
        out_args[i] = use_ans_placeholder && i == 0 ? ba_obj::make_void(V_ANS)
                                                    : std::make_shared<ba_obj>();
    }

    try {
        function_ptr(runtime_in_args, out_args);
    } catch (const std::exception& ex) {
        throw std::runtime_error(std::string("调用") + callable_type_text(type) + "失败: " + name +
                                 "，输入个数 = " + std::to_string(in_args.size()) +
                                 "，输出个数 = " + std::to_string(out_count) +
                                 "，原因: " + ex.what());
    }
    return out_args;
}

Value::Object eval_binop(BinOpNode::Op op, Value::Object lhs, Value::Object rhs) {
    std::string name;
    switch (op) {
        case BinOpNode::Add:
            name = "plus";
            break;
        case BinOpNode::Subtract:
            name = "minus";
            break;
        case BinOpNode::Eq:
            name = "eq";
            break;
        case BinOpNode::Gt:
            name = "gt";
            break;
        case BinOpNode::Lt:
            name = "lt";
            break;
        case BinOpNode::Ne:
            name = "ne";
            break;
        case BinOpNode::Or:
            name = "or";
            break;
        case BinOpNode::MPower:
            name = "mpower";
            break;
        case BinOpNode::Multiply:
            name = "times";
            break;
    }

    baFunPtr function_ptr = nullptr;
    if (!try_lookup_builtin_function_cached(name, function_ptr)) {
        throw std::runtime_error("找不到内置函数：" + name);
    }
    std::vector<Value::Object> results =
        invoke_function_ptr(name, function_ptr, {std::move(lhs), std::move(rhs)}, 1,
                            CallableType::Builtin);
    return results.front();
}

Value::Object eval_unaryop(UnaryOpNode::Op op, Value::Object operand) {
    std::string name;
    switch (op) {
        case UnaryOpNode::Logic_Not:
            name = "not";
            break;
        case UnaryOpNode::UMinus:
            name = "uminus";
            break;
    }

    baFunPtr function_ptr = nullptr;
    if (!try_lookup_builtin_function_cached(name, function_ptr)) {
        throw std::runtime_error("找不到内置函数：" + name);
    }
    std::vector<Value::Object> results =
        invoke_function_ptr(name, function_ptr, {std::move(operand)}, 1, CallableType::Builtin);
    return results.front();
}

std::vector<Value> wrap_outputs(std::vector<Value::Object> values, std::size_t expected_out_count,
                                const std::string& callee_name) {
    if (values.size() < expected_out_count) {
        throw std::runtime_error("调用 `" + callee_name + "` 时返回值个数不足。");
    }

    std::vector<Value> outputs;
    outputs.reserve(expected_out_count);
    for (std::size_t i = 0; i < expected_out_count; ++i) {
        outputs.push_back(concrete(std::move(values[i])));
    }
    return outputs;
}

ExecResult execute_function_impl(Function& function, const std::vector<Value::Object>& args);

std::vector<Value> invoke_module_function(Function& caller, const std::string& function_name,
                                          const std::vector<Value::Object>& in_args,
                                          std::size_t expected_out_count) {
    Module* module = caller.parent();
    if (module == nullptr) {
        throw std::runtime_error("找不到可调用的函数 `" + function_name + "`：当前函数不在模块内。");
    }

    for (const auto& function : module->functions()) {
        if (function != nullptr && function->name() == function_name) {
            ExecResult callee_result = execute_function_impl(*function, in_args);
            if (callee_result.outputs.size() < expected_out_count) {
                throw std::runtime_error("函数 `" + function_name + "` 的输出个数不足。");
            }
            callee_result.outputs.resize(expected_out_count);
            return std::move(callee_result.outputs);
        }
    }

    throw std::runtime_error("找不到可调用的函数 `" + function_name + "`。");
}

std::vector<Value> invoke_direct_call(Function& caller, const std::string& callee_name,
                                      const std::vector<Value::Object>& in_args,
                                      std::size_t expected_out_count) {
    if (callee_name == "__ir_make_cell__") {
        if (expected_out_count == 0) {
            return {};
        }
        return {concrete(std::make_shared<ba_obj>(new cell_array(in_args, false)))};
    }

    if (callee_name == "__ir_make_function_handle__") {
        if (in_args.size() != 1 || in_args.front() == nullptr) {
            throw std::runtime_error("构造函数句柄时缺少函数名。");
        }
        if (expected_out_count == 0) {
            return {};
        }
        const std::string function_name = in_args.front()->as_string();
        return {concrete(std::make_shared<ba_obj>(function_handle(fh_anonymous, function_name)))};
    }

    if (callee_name == "__ir_switch_match__") {
        if (in_args.size() != 2) {
            throw std::runtime_error("switch 匹配比较需要两个输入。");
        }
        if (expected_out_count == 0) {
            return {};
        }

        const Value::Object& lhs = in_args[0];
        const Value::Object& rhs = in_args[1];
        bool matched = false;

        try {
            if (lhs != nullptr && rhs != nullptr &&
                ((lhs->_is_string() || lhs->is_char_vector()) &&
                 (rhs->_is_string() || rhs->is_char_vector()))) {
                matched = lhs->as_string() == rhs->as_string();
            } else {
                matched = eval_binop(BinOpNode::Eq, lhs, rhs)->as_bool();
            }
        } catch (const std::exception&) {
            matched = false;
        }

        return {concrete(std::make_shared<ba_obj>(matched))};
    }

    Module* module = caller.parent();
    if (module != nullptr) {
        for (const auto& function : module->functions()) {
            if (function != nullptr && function->name() == callee_name) {
                return invoke_module_function(caller, callee_name, in_args, expected_out_count);
            }
        }
    }

    baFunPtr function_ptr = nullptr;
    if (try_lookup_builtin_function_cached(callee_name, function_ptr)) {
        const std::size_t actual_out_count =
            expected_out_count == 0
                ? 0
                : std::max(expected_out_count, required_builtin_out_count(callee_name));
        return wrap_outputs(invoke_function_ptr(callee_name, function_ptr, in_args, actual_out_count,
                                                CallableType::Builtin),
                            expected_out_count, callee_name);
    }

    if (try_lookup_internal_function_cached(callee_name, function_ptr)) {
        return wrap_outputs(invoke_function_ptr(callee_name, function_ptr, in_args,
                                                expected_out_count, CallableType::Internal),
                            expected_out_count, callee_name);
    }

    throw std::runtime_error("找不到可调用的函数 `" + callee_name + "`。");
}

std::vector<Value> invoke_indirect_call(Function& caller, const Value::Object& callee_value,
                                        const std::vector<Value::Object>& in_args,
                                        std::size_t expected_out_count) {
    if (callee_value == nullptr) {
        throw std::runtime_error("调用目标为空。");
    }
    if (callee_value->type() != ba_function_handle) {
        throw std::runtime_error("调用目标不是函数句柄。");
    }

    const auto* handle = callee_value->cget<function_handle>();
    switch (handle->type()) {
        case fh_anonymous:
        case fh_mfunction:
        case fh_script:
            return invoke_module_function(caller, handle->data(), in_args, expected_out_count);
        case fh_builtin: {
            baFunPtr function_ptr = nullptr;
            if (!try_lookup_builtin_function_cached(handle->data(), function_ptr)) {
                throw std::runtime_error("找不到内置函数句柄 `" + handle->data() + "`。");
            }
            const std::size_t actual_out_count =
                expected_out_count == 0
                    ? 0
                    : std::max(expected_out_count, required_builtin_out_count(handle->data()));
            return wrap_outputs(
                invoke_function_ptr(handle->data(), function_ptr, in_args, actual_out_count,
                                    CallableType::Builtin),
                expected_out_count, handle->data());
        }
        case fh_variable:
            break;
    }

    throw std::runtime_error("暂不支持该函数句柄类型 `" +
                             std::string(fh_type_string(handle->type())) + "`。");
}

std::vector<Value> evaluate_call(const SSACallNode& call, ExecutionState& state) {
    std::vector<Value::Object> in_args;
    in_args.reserve(call.inputs().size());
    for (const ValueRef input : call.inputs()) {
        in_args.push_back(state.require_concrete_object(input, "调用实参"));
    }

    const std::size_t expected_out_count = call.results().size();
    if (call.callee().type == SSACallNode::Callee::Direct) {
        return invoke_direct_call(state.function, call.callee().direct_symbol, in_args,
                                  expected_out_count);
    }

    const Value::Object callee_value =
        state.require_concrete_object(call.callee().indirect_value, "间接调用目标");
    return invoke_indirect_call(state.function, callee_value, in_args, expected_out_count);
}

void execute_phi_nodes(const BasicBlock& block, ExecutionState& state) {
    std::vector<std::pair<ValueId, Value>> pending;
    pending.reserve(block.phi_nodes().size());

    for (IRNode* node : block.phi_nodes()) {
        if (node == nullptr) {
            continue;
        }

        const auto& phi = static_cast<const SSAPhiNode&>(*node);
        const SSAPhiNode::Incoming* selected = nullptr;
        for (const SSAPhiNode::Incoming& incoming : phi.incomings()) {
            if (incoming.predecessor == state.predecessor) {
                selected = &incoming;
                break;
            }
        }

        if (selected == nullptr) {
            throw std::runtime_error("函数 `" + state.function.name() + "` 的 phi `" +
                                     value_label(state.function, phi.result()) +
                                     "` 缺少来自前驱块 `" + block_label(state.predecessor) +
                                     "` 的 incoming。");
        }

        pending.emplace_back(phi.result(), state.load_value(selected->value));
    }

    for (auto& entry : pending) {
        state.store_value(entry.first, std::move(entry.second));
    }
}

void execute_instruction(const IRNode& node, ExecutionState& state) {
    const auto& ssa = static_cast<const UntypedSSANode&>(node);
    switch (ssa.type()) {
        case UntypedSSANode::SSA_Number: {
            const auto& number = static_cast<const SSANumberNode&>(ssa);
            Value::Object object = std::visit(
                [](const auto& item) -> Value::Object { return std::make_shared<ba_obj>(item); },
                number.value());
            state.store_value(number.result(), concrete(std::move(object)));
            return;
        }
        case UntypedSSANode::SSA_Text: {
            const auto& text = static_cast<const SSATextNode&>(ssa);
            state.store_value(text.result(),
                              concrete(std::make_shared<ba_obj>(text.text().c_str(), ba_char_mat)));
            return;
        }
        case UntypedSSANode::SSA_Undef: {
            const auto& undef_node = static_cast<const SSAUndefNode&>(ssa);
            state.store_value(undef_node.result(), undef());
            return;
        }
        case UntypedSSANode::SSA_Copy: {
            const auto& copy = static_cast<const SSACopyNode&>(ssa);
            state.store_value(copy.result(), state.load_value(copy.src()));
            return;
        }
        case UntypedSSANode::SSA_UnaryOp: {
            const auto& unary = static_cast<const SSAUnaryOpNode&>(ssa);
            state.store_value(unary.result(),
                              concrete(eval_unaryop(
                                  unary.op(),
                                  state.require_concrete_object(unary.operand(), "单目运算输入"))));
            return;
        }
        case UntypedSSANode::SSA_BinOp: {
            const auto& binop = static_cast<const SSABinOpNode&>(ssa);
            state.store_value(binop.result(),
                              concrete(eval_binop(
                                  binop.op(),
                                  state.require_concrete_object(binop.lhs(), "二元运算左输入"),
                                  state.require_concrete_object(binop.rhs(), "二元运算右输入"))));
            return;
        }
        case UntypedSSANode::SSA_Call: {
            const auto& call = static_cast<const SSACallNode&>(ssa);
            std::vector<Value> outputs = evaluate_call(call, state);
            if (outputs.size() != call.results().size()) {
                throw std::runtime_error("函数 `" + state.function.name() + "` 的调用节点返回值个数不匹配。");
            }
            for (std::size_t i = 0; i < outputs.size(); ++i) {
                state.store_value(call.results()[i], std::move(outputs[i]));
            }
            return;
        }
        case UntypedSSANode::SSA_Phi:
            throw std::runtime_error("phi 节点只能出现在 phi 区域。");
        case UntypedSSANode::SSA_CondJump:
        case UntypedSSANode::SSA_Jump:
        case UntypedSSANode::SSA_Return:
            throw std::runtime_error("终结节点不能出现在基本块正文中。");
    }
}

BasicBlock* execute_terminal(const BasicBlock& block, ExecutionState& state) {
    if (block.terminal() == nullptr) {
        throw std::runtime_error("函数 `" + state.function.name() + "` 的基本块 `" +
                                 block.name() + "` 缺少终结节点。");
    }

    const auto& terminal = static_cast<const UntypedSSANode&>(*block.terminal());
    switch (terminal.type()) {
        case UntypedSSANode::SSA_CondJump: {
            const auto& jump = static_cast<const SSACondJumpNode&>(terminal);
            return condition_value_as_bool(
                       state.require_concrete_object(jump.cond(), "条件跳转条件值"))
                       ? jump.true_block()
                       : jump.false_block();
        }
        case UntypedSSANode::SSA_Jump:
            return static_cast<const SSAJumpNode&>(terminal).target();
        case UntypedSSANode::SSA_Return: {
            const auto& ret = static_cast<const SSAReturnNode&>(terminal);
            state.outputs.clear();
            state.outputs.reserve(ret.values().size());
            for (const ValueRef value : ret.values()) {
                state.outputs.push_back(state.require_concrete_value(value, "返回值"));
            }
            return nullptr;
        }
        case UntypedSSANode::SSA_Number:
        case UntypedSSANode::SSA_Text:
        case UntypedSSANode::SSA_Undef:
        case UntypedSSANode::SSA_Phi:
        case UntypedSSANode::SSA_Copy:
        case UntypedSSANode::SSA_UnaryOp:
        case UntypedSSANode::SSA_BinOp:
        case UntypedSSANode::SSA_Call:
            break;
    }

    throw std::runtime_error("基本块 `" + block.name() + "` 的终结节点类型非法。");
}

ExecResult execute_function_impl(Function& function, const std::vector<Value::Object>& args) {
    if (function.stage() != IRNode::UntypedSSA) {
        throw std::runtime_error("解释器只支持执行 `UntypedSSA` 函数 `" + function.name() + "`。");
    }
    analysis::verify_function_or_throw(function);

    if (function.entry_block() == nullptr) {
        throw std::runtime_error("函数 `" + function.name() + "` 缺少入口块。");
    }
    if (function.argument_values().size() != function.inputs().size()) {
        throw std::runtime_error("函数 `" + function.name() + "` 的输入签名与 SSA 参数槽位个数不一致。");
    }
    if (args.size() != function.argument_values().size()) {
        throw std::runtime_error("执行函数 `" + function.name() + "` 时实参数量不匹配。");
    }

    ExecutionState state{function, {}, {}, nullptr};
    for (std::size_t i = 0; i < args.size(); ++i) {
        state.store_value(function.argument_values()[i], concrete(args[i]));
    }

    BasicBlock* current_block = function.entry_block();
    while (current_block != nullptr) {
        execute_phi_nodes(*current_block, state);
        for (IRNode* node : current_block->instructions()) {
            if (node != nullptr) {
                execute_instruction(*node, state);
            }
        }

        BasicBlock* next_block = execute_terminal(*current_block, state);
        state.predecessor = current_block;
        current_block = next_block;
    }

    return ExecResult{std::move(state.outputs)};
}

}  // namespace

ExecResult execute_function(Function& function, const std::vector<Value::Object>& args) {
    return execute_function_impl(function, args);
}

}  // namespace baltam::interpreter
