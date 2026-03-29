#include "interpreter/interpreter.h"

#include <stdexcept>
#include <unordered_map>
#include <utility>

#include "ba_obj/ba_obj.h"
#include "baltam_worker/builtin_manager.h"
#include "print/obj2str.h"

namespace baltam {
Value eval_expr(Instruction* instruction, Frame& frame);
void exec_inst(Instruction* instruction, Frame& frame);
BasicBlock* exec_terminal(Instruction* instruction, Frame& frame);

namespace {

enum class CallableType {
    Builtin,
    Internal,
};

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
    if (function_ptr == nullptr) {
        return false;
    }

    cache.emplace(name, function_ptr);
    return true;
}

std::size_t required_builtin_out_count(const std::string& name) {
    const auto [declared_nargin, declared_nargout] = lookup_builtin_function_narg(name);
    (void)declared_nargin;
    if (declared_nargout < 0) {
        return 0;
    }
    return static_cast<std::size_t>(declared_nargout);
}

std::vector<Value> invoke_function_ptr(const std::string& name, baFunPtr function_ptr,
                                       const std::vector<Value>& in_args,
                                       std::size_t out_count, CallableType type) {
    std::vector<__const_ba_obj_p> runtime_in_args;
    runtime_in_args.reserve(in_args.size());
    for (const Value& arg : in_args) {
        runtime_in_args.push_back(arg);
    }

    // 语句位置调用仍然为运行时构造一个 ans 占位槽位，避免 out_args 为空。
    const bool use_ans_placeholder = out_count == 0;
    std::vector<Value> out_args(use_ans_placeholder ? 1 : out_count);
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

Value eval_binop(BinOpInstruction::Type op, Value lhs, Value rhs) {
    std::string name;
    switch (op) {
        case BinOpInstruction::Add:
            name = "plus";
            break;
        case BinOpInstruction::Subtract:
            name = "minus";
            break;
        case BinOpInstruction::Gt:
            name = "gt";
            break;
        case BinOpInstruction::Lt:
            name = "lt";
            break;
        case BinOpInstruction::Ne:
            name = "ne";
            break;
        case BinOpInstruction::Or:
            name = "or";
            break;
        case BinOpInstruction::MPower:
            name = "mpower";
            break;
        case BinOpInstruction::Multiply:
            name = "times";
            break;
    }

    baFunPtr function_ptr = nullptr;
    if (!try_lookup_builtin_function_cached(name, function_ptr)) {
        throw std::runtime_error("找不到内置函数：" + name);
    }
    std::vector<Value> results =
        invoke_function_ptr(name, function_ptr, {std::move(lhs), std::move(rhs)}, 1,
                            CallableType::Builtin);
    return results.front();
}

Value eval_unaryop(UnaryOpInstruction::Type op, Value operand) {
    std::string name;
    switch (op) {
        case UnaryOpInstruction::UMinus:
            name = "uminus";
            break;
    }

    baFunPtr function_ptr = nullptr;
    if (!try_lookup_builtin_function_cached(name, function_ptr)) {
        throw std::runtime_error("找不到内置函数：" + name);
    }
    std::vector<Value> results =
        invoke_function_ptr(name, function_ptr, {std::move(operand)}, 1, CallableType::Builtin);
    return results.front();
}

std::vector<Value> eval_call(const std::string& name, const std::vector<Value>& in_args,
                             std::size_t out_count, Frame& frame) {
    baFunPtr function_ptr = nullptr;

    if (try_lookup_builtin_function_cached(name, function_ptr)) {
        const std::size_t actual_out_count =
            out_count == 0 ? 0 : std::max(out_count, required_builtin_out_count(name));
        return invoke_function_ptr(name, function_ptr, in_args, actual_out_count,
                                   CallableType::Builtin);
    }

    if (try_lookup_internal_function_cached(name, function_ptr)) {
        return invoke_function_ptr(name, function_ptr, in_args, out_count,
                                   CallableType::Internal);
    }

    Module* module = frame.function() != nullptr ? frame.function()->parent() : nullptr;
    if (module == nullptr) {
        throw std::runtime_error("找不到可调用的函数：" + name);
    }

    for (const auto& function : module->functions()) {
        if (function != nullptr && function->name() == name) {
            Frame callee = execute_function(*function, in_args, &frame);
            if (callee.outputs().size() < out_count) {
                throw std::runtime_error("函数输出个数不匹配：" + name);
            }
            if (out_count == 0) {
                return {};
            }
            std::vector<Value> outputs = callee.outputs();
            outputs.resize(out_count);
            return outputs;
        }
    }

    throw std::runtime_error("找不到可调用的函数：" + name);
}

void assign_output_operand(Instruction* operand, Value value, Frame& frame) {
    if (operand == nullptr) {
        throw std::runtime_error("函数调用的输出操作数为空。");
    }
    if (operand->type() != Instruction::Name) {
        throw std::runtime_error("函数调用的输出目标目前只支持 NameInstruction。");
    }

    const auto* name_instruction = static_cast<const NameInstruction*>(operand);
    frame.store(name_instruction->name(), std::move(value));
}

std::vector<Value> eval_call_instruction(const CallInstruction& instruction, Frame& frame,
                                         std::size_t default_out_count) {
    std::vector<Value> in_args;
    in_args.reserve(instruction.in_args().size());
    for (Instruction* in_arg : instruction.in_args()) {
        in_args.push_back(eval_expr(in_arg, frame));
    }

    const std::size_t out_count = instruction.out_args().empty() ? default_out_count
                                                                 : instruction.out_args().size();
    std::vector<Value> out_args = eval_call(instruction.name(), in_args, out_count, frame);

    for (std::size_t i = 0; i < instruction.out_args().size(); ++i) {
        assign_output_operand(instruction.out_args()[i], out_args[i], frame);
    }

    return out_args;
}

std::vector<Value> collect_function_outputs(Function& function, const Frame& frame) {
    std::vector<Value> outputs;
    outputs.reserve(function.output_names().size());
    for (const std::string& name : function.output_names()) {
        outputs.push_back(frame.load(name));
    }
    return outputs;
}

}  // namespace

Frame::Frame(Function* function, Frame* caller, int nargin, int nargout)
    : function_(function), caller_(caller), nargin_(nargin), nargout_(nargout) {}

Function* Frame::function() const {
    return function_;
}

Frame* Frame::caller() const {
    return caller_;
}

int Frame::nargin() const {
    return nargin_;
}

int Frame::nargout() const {
    return nargout_;
}

bool Frame::returned() const {
    return returned_;
}

const Frame::SymbolTable& Frame::symbols() const {
    return symbols_;
}

const std::vector<Value>& Frame::outputs() const {
    return outputs_;
}

void Frame::declare(const std::string& name) {
    symbols_.try_emplace(name);
}

void Frame::store(const std::string& name, Value value) {
    Binding& binding = symbols_[name];
    binding.value = std::move(value);
    binding.initialized = true;
}

Value Frame::load(const std::string& name) const {
    auto it = symbols_.find(name);
    if (it == symbols_.end()) {
        throw std::runtime_error("未定义的符号：" + name);
    }
    if (!it->second.initialized || it->second.value == nullptr) {
        throw std::runtime_error("符号尚未初始化：" + name);
    }
    return it->second.value;
}

bool Frame::contains(const std::string& name) const {
    return symbols_.find(name) != symbols_.end();
}

bool Frame::is_initialized(const std::string& name) const {
    auto it = symbols_.find(name);
    return it != symbols_.end() && it->second.initialized;
}

void Frame::set_returned(bool returned) {
    returned_ = returned;
}

void Frame::set_outputs(std::vector<Value> outputs) {
    outputs_ = std::move(outputs);
}

Value eval_expr(Instruction* instruction, Frame& frame) {
    if (instruction == nullptr) {
        throw std::runtime_error("不能对空指令求值。");
    }

    switch (instruction->type()) {
        case Instruction::Text: {
            const auto* text = static_cast<const TextInstruction*>(instruction);
            return std::make_shared<ba_obj>(text->text().c_str(), ba_char_mat);
        }
        case Instruction::Name: {
            const auto* name = static_cast<const NameInstruction*>(instruction);
            return frame.load(name->name());
        }
        case Instruction::Number: {
            const auto* number = static_cast<const NumberInstruction*>(instruction);
            return std::visit(
                [](const auto& item) -> Value { return std::make_shared<ba_obj>(item); },
                number->value());
        }
        case Instruction::UnaryOp: {
            const auto* unaryop = static_cast<const UnaryOpInstruction*>(instruction);
            return eval_unaryop(unaryop->op(), eval_expr(unaryop->operand(), frame));
        }
        case Instruction::BinOp: {
            const auto* binop = static_cast<const BinOpInstruction*>(instruction);
            return eval_binop(binop->op(), eval_expr(binop->lhs(), frame),
                              eval_expr(binop->rhs(), frame));
        }
        case Instruction::Call: {
            const auto* call = static_cast<const CallInstruction*>(instruction);
            std::vector<Value> out_args = eval_call_instruction(*call, frame, 1);
            if (out_args.empty()) {
                return std::make_shared<ba_obj>();
            }
            return out_args.front();
        }
        case Instruction::Asgn:
        case Instruction::CondJump:
        case Instruction::Jump:
        case Instruction::Return:
            break;
    }

    throw std::runtime_error("该指令不能作为表达式求值。");
}

void exec_inst(Instruction* instruction, Frame& frame) {
    if (instruction == nullptr) {
        return;
    }

    switch (instruction->type()) {
        case Instruction::Asgn: {
            const auto* assign = static_cast<const AssignInstruction*>(instruction);
            frame.store(assign->name(), eval_expr(assign->value(), frame));
            return;
        }
        case Instruction::Call: {
            const auto* call = static_cast<const CallInstruction*>(instruction);
            (void)eval_call_instruction(*call, frame, 0);
            return;
        }
        case Instruction::Text:
        case Instruction::Name:
        case Instruction::Number:
        case Instruction::UnaryOp:
        case Instruction::BinOp:
            return;
        case Instruction::CondJump:
        case Instruction::Jump:
        case Instruction::Return:
            break;
    }

    throw std::runtime_error("终结指令不能出现在基本块正文中。");
}

BasicBlock* exec_terminal(Instruction* instruction, Frame& frame) {
    if (instruction == nullptr) {
        frame.set_returned(true);
        frame.set_outputs(collect_function_outputs(*frame.function(), frame));
        return nullptr;
    }

    switch (instruction->type()) {
        case Instruction::CondJump: {
            const auto* cond_jump = static_cast<const CondJumpInstruction*>(instruction);
            Value cond_value = eval_expr(cond_jump->cond(), frame);
            if (cond_value == nullptr) {
                throw std::runtime_error("条件值为空。");
            }
            return cond_value->as_bool() ? cond_jump->true_block() : cond_jump->false_block();
        }
        case Instruction::Jump: {
            const auto* jump = static_cast<const JumpInstruction*>(instruction);
            return jump->target();
        }
        case Instruction::Return:
            frame.set_returned(true);
            frame.set_outputs(collect_function_outputs(*frame.function(), frame));
            return nullptr;
        case Instruction::Text:
        case Instruction::Name:
        case Instruction::Number:
        case Instruction::UnaryOp:
        case Instruction::BinOp:
        case Instruction::Asgn:
        case Instruction::Call:
            break;
    }

    throw std::runtime_error("基本块的 terminal 不是合法的终结指令。");
}

Frame execute_function(Function& function, const std::vector<Value>& args, Frame* caller) {
    if (function.entry_block() == nullptr) {
        throw std::runtime_error("函数缺少入口基本块：" + function.name());
    }
    if (args.size() != function.input_names().size()) {
        throw std::runtime_error("执行函数时参数个数不匹配：" + function.name());
    }

    Frame frame(&function, caller, static_cast<int>(args.size()),
                static_cast<int>(function.output_names().size()));

    for (std::size_t i = 0; i < function.input_names().size(); ++i) {
        // MATLAB-like 函数参数更接近按值传递；这里复制一份 ba_obj 包装，
        // 避免后续运行时调用意外共享并改写入口实参对象。
        frame.store(function.input_names()[i],
                    args[i] == nullptr ? nullptr : std::make_shared<ba_obj>(*args[i]));
    }
    for (const std::string& output_name : function.output_names()) {
        frame.declare(output_name);
    }

    BasicBlock* block = function.entry_block();
    while (block != nullptr && !frame.returned()) {
        for (Instruction* instruction : block->instructions()) {
            exec_inst(instruction, frame);
        }
        block = exec_terminal(block->terminal(), frame);
    }

    if (!frame.returned()) {
        frame.set_outputs(collect_function_outputs(function, frame));
    }
    return frame;
}

std::string value_text(const Value& value) {
    if (value == nullptr) {
        return "<null>";
    }
    return internal::obj2str(*value);
}

}  // namespace baltam
