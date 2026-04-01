#include "interpreter/interpreter.h"

#include <algorithm>
#include <complex>
#include <cstdint>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include "ba_obj/ba_obj.h"
#include "ba_obj/cell.h"
#include "ba_obj/function_handle.h"
#include "ba_obj/matrix.h"
#include "baltam_worker/builtin_manager.h"
#include "print/obj2str.h"

namespace baltam {
Value eval_expr(Instruction* instruction, Frame& frame);
void exec_inst(Instruction* instruction, Frame& frame);
BasicBlock* exec_terminal(Instruction* instruction, Frame& frame);

namespace {
Value load_use_value(Frame& frame, Instruction* instruction, ValueRef ref);

void record_instruction_value(Frame& frame, Instruction& instruction, Value value) {
    if (!instruction.has_values()) {
        return;
    }
    frame.store_instruction_values(instruction, {std::move(value)});
}

Value record_and_return(Frame& frame, Instruction& instruction, Value value) {
    record_instruction_value(frame, instruction, value);
    return value;
}

void materialize_instruction_value(Frame& frame, Instruction& instruction) {
    if (!instruction.has_values()) {
        return;
    }

    (void)eval_expr(&instruction, frame);
}

Value eval_phi_instruction(const PhiInstruction& instruction, BasicBlock* predecessor, Frame& frame) {
    for (const PhiInstruction::Incoming& incoming : instruction.incomings()) {
        if (incoming.predecessor == predecessor) {
            return load_use_value(frame, nullptr, incoming.value_ref);
        }
    }

    const std::string predecessor_name = predecessor != nullptr ? predecessor->name() : "<entry>";
    throw std::runtime_error("phi 节点缺少来自前驱块 " + predecessor_name + " 的 incoming。");
}

void exec_phi_nodes(BasicBlock& block, BasicBlock* predecessor, Frame& frame) {
    for (Instruction* instruction : block.instructions()) {
        if (instruction == nullptr || instruction->type() != Instruction::Phi) {
            break;
        }

        Value value = eval_phi_instruction(*static_cast<const PhiInstruction*>(instruction),
                                           predecessor, frame);
        record_instruction_value(frame, *instruction, std::move(value));
    }
}

Function* value_owner_function(Frame& frame, Instruction* instruction) {
    if (instruction != nullptr && instruction->parent() != nullptr &&
        instruction->parent()->parent() != nullptr) {
        return instruction->parent()->parent();
    }
    return frame.function();
}

Value load_use_value(Frame& frame, Instruction* instruction, ValueRef ref) {
    if (ref.is_valid() && frame.has_value(ref.id)) {
        return frame.load_value(ref);
    }
    if (ref.is_valid()) {
        Function* function = value_owner_function(frame, instruction);
        if (function != nullptr) {
            if (Instruction* owner = function->find_value_owner(ref.id)) {
                materialize_instruction_value(frame, *owner);
                if (frame.has_value(ref.id)) {
                    return frame.load_value(ref);
                }
            }
        }
    }
    if (instruction == nullptr) {
        throw std::runtime_error("尝试读取空的 IR 操作数。");
    }
    return eval_expr(instruction, frame);
}

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
        case BinOpInstruction::Eq:
            name = "eq";
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
        case UnaryOpInstruction::Logic_Not:
            name = "not";
            break;
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
    Module* module = frame.function() != nullptr ? frame.function()->parent() : nullptr;
    auto invoke_module_function = [&](const std::string& function_name) -> std::vector<Value> {
        if (module == nullptr) {
            throw std::runtime_error("找不到可调用的函数：" + function_name);
        }

        for (const auto& function : module->functions()) {
            if (function != nullptr && function->name() == function_name) {
                Frame callee = execute_function(*function, in_args, &frame);
                if (callee.outputs().size() < out_count) {
                    throw std::runtime_error("函数输出个数不匹配：" + function_name);
                }
                if (out_count == 0) {
                    return {};
                }
                std::vector<Value> outputs = callee.outputs();
                outputs.resize(out_count);
                return outputs;
            }
        }
        throw std::runtime_error("找不到可调用的函数：" + function_name);
    };

    if (name == "__ir_make_cell__") {
        if (out_count == 0) {
            return {};
        }
        return {std::make_shared<ba_obj>(new cell_array(in_args, false))};
    }

    if (name == "__ir_make_function_handle__") {
        if (in_args.size() != 1 || in_args.front() == nullptr) {
            throw std::runtime_error("构造函数句柄时缺少函数名。");
        }
        if (out_count == 0) {
            return {};
        }
        const std::string function_name = in_args.front()->as_string();
        return {std::make_shared<ba_obj>(function_handle(fh_anonymous, function_name))};
    }

    if (name == "__ir_switch_match__") {
        if (in_args.size() != 2) {
            throw std::runtime_error("switch 匹配比较需要两个输入。");
        }
        if (out_count == 0) {
            return {};
        }

        const Value& lhs = in_args[0];
        const Value& rhs = in_args[1];
        bool matched = false;

        try {
            if (lhs != nullptr && rhs != nullptr &&
                ((lhs->_is_string() || lhs->is_char_vector()) &&
                 (rhs->_is_string() || rhs->is_char_vector()))) {
                matched = lhs->as_string() == rhs->as_string();
            } else {
                matched = eval_binop(BinOpInstruction::Eq, lhs, rhs)->as_bool();
            }
        } catch (const std::exception&) {
            matched = false;
        }

        return {std::make_shared<ba_obj>(matched)};
    }

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

    return invoke_module_function(name);
}

std::vector<Value> eval_call_value(const Value& callee_value, const std::vector<Value>& in_args,
                                   std::size_t out_count, Frame& frame) {
    if (callee_value == nullptr) {
        throw std::runtime_error("调用目标为空。");
    }
    if (callee_value->type() != ba_function_handle) {
        throw std::runtime_error("调用目标不是函数句柄。");
    }

    Module* module = frame.function() != nullptr ? frame.function()->parent() : nullptr;
    auto invoke_module_function = [&](const std::string& function_name) -> std::vector<Value> {
        if (module == nullptr) {
            throw std::runtime_error("找不到可调用的函数：" + function_name);
        }
        for (const auto& function : module->functions()) {
            if (function != nullptr && function->name() == function_name) {
                Frame callee = execute_function(*function, in_args, &frame);
                if (callee.outputs().size() < out_count) {
                    throw std::runtime_error("函数输出个数不匹配：" + function_name);
                }
                if (out_count == 0) {
                    return {};
                }
                std::vector<Value> outputs = callee.outputs();
                outputs.resize(out_count);
                return outputs;
            }
        }
        throw std::runtime_error("找不到可调用的函数：" + function_name);
    };

    const auto* handle = callee_value->cget<function_handle>();
    switch (handle->type()) {
        case fh_anonymous:
        case fh_mfunction:
            return invoke_module_function(handle->data());
        case fh_builtin: {
            baFunPtr function_ptr = nullptr;
            if (!try_lookup_builtin_function_cached(handle->data(), function_ptr)) {
                throw std::runtime_error("找不到内置函数句柄：" + handle->data());
            }
            const std::size_t actual_out_count =
                out_count == 0 ? 0 : std::max(out_count, required_builtin_out_count(handle->data()));
            return invoke_function_ptr(handle->data(), function_ptr, in_args, actual_out_count,
                                       CallableType::Builtin);
        }
        default:
            throw std::runtime_error("暂不支持该函数句柄类型：" +
                                     std::string(fh_type_string(handle->type())));
    }
}

std::vector<Value> eval_call_instruction(const CallInstruction& instruction, Frame& frame,
                                         std::size_t default_out_count) {
    std::vector<Value> in_args;
    const std::size_t in_arg_count = instruction.input_count();
    in_args.reserve(in_arg_count);
    for (std::size_t i = 0; i < in_arg_count; ++i) {
        in_args.push_back(load_use_value(frame, nullptr, instruction.input_ref(i)));
    }

    const std::size_t out_count =
        instruction.output_count() == 0 ? default_out_count : instruction.output_count();
    std::vector<Value> out_args =
        instruction.is_indirect()
            ? eval_call_value(load_use_value(frame, nullptr, instruction.callee_ref()), in_args,
                              out_count, frame)
            : eval_call(instruction.name(), in_args, out_count, frame);

    frame.store_instruction_values(instruction, out_args);

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

std::vector<Value> collect_return_values(const ReturnInstruction& instruction, Frame& frame) {
    const std::size_t value_count = instruction.return_value_count();
    if (value_count == 0) {
        return collect_function_outputs(*frame.function(), frame);
    }

    std::vector<Value> outputs;
    outputs.reserve(value_count);
    for (std::size_t i = 0; i < value_count; ++i) {
        const ValueRef ref = instruction.return_value_ref(i);
        outputs.push_back(load_use_value(frame, nullptr, ref));
    }
    return outputs;
}

Value maybe_eval_named_constant(const std::string& name) {
    if (name == "i" || name == "j") {
        return std::make_shared<ba_obj>(std::complex<double>{0.0, 1.0});
    }
    return nullptr;
}

template <typename T>
bool matrix_condition_value_from_data(const T* data, baSize size) {
    for (baIndex i = 0; i < size; ++i) {
        if (data[i] == T{}) {
            return false;
        }
    }
    return true;
}

template <typename T>
bool matrix_condition_value(const matrix<T>& mat) {
    const baSize size = mat.size();
    if (size == 0) {
        return false;
    }

    if (mat.is_contiguous()) {
        return matrix_condition_value_from_data(mat.data(), size);
    }

    const matrix<T> contiguous = mat.contiguous();
    return matrix_condition_value_from_data(contiguous.data(), size);
}

bool bool_matrix_condition_value(const matrix<bool>& mat) {
    const baSize size = mat.size();
    if (size == 0) {
        return false;
    }

    if (mat.is_contiguous()) {
        return matrix_condition_value_from_data(mat.data(), size);
    }

    const matrix<bool> contiguous = mat.contiguous();
    return matrix_condition_value_from_data(contiguous.data(), size);
}

bool char_matrix_condition_value(const matrix<char>& mat) {
    const baSize size = mat.size();
    if (size == 0) {
        return false;
    }

    if (mat.is_contiguous()) {
        return matrix_condition_value_from_data(mat.data(), size);
    }

    const matrix<char> contiguous = mat.contiguous();
    return matrix_condition_value_from_data(contiguous.data(), size);
}

bool complex_matrix_condition_value(const matrix<std::complex<double>>& mat) {
    const baSize size = mat.size();
    if (size == 0) {
        return false;
    }

    if (mat.is_contiguous()) {
        return matrix_condition_value_from_data(mat.data(), size);
    }

    const matrix<std::complex<double>> contiguous = mat.contiguous();
    return matrix_condition_value_from_data(contiguous.data(), size);
}

bool complex_matrix_condition_value(const matrix<std::complex<float>>& mat) {
    const baSize size = mat.size();
    if (size == 0) {
        return false;
    }

    if (mat.is_contiguous()) {
        return matrix_condition_value_from_data(mat.data(), size);
    }

    const matrix<std::complex<float>> contiguous = mat.contiguous();
    return matrix_condition_value_from_data(contiguous.data(), size);
}

bool condition_value_as_bool(const Value& value) {
    if (value == nullptr) {
        throw std::runtime_error("条件值为空。");
    }

    try {
        return value->as_bool();
    } catch (const std::invalid_argument&) {
    }

    switch (value->type()) {
        case ba_int8_mat:
            return matrix_condition_value(*value->cget<matrix<std::int8_t>>());
        case ba_int16_mat:
            return matrix_condition_value(*value->cget<matrix<std::int16_t>>());
        case ba_int_mat:
            return matrix_condition_value(*value->cget<matrix<std::int32_t>>());
        case ba_int64_mat:
            return matrix_condition_value(*value->cget<matrix<std::int64_t>>());
        case ba_uint8_mat:
            return matrix_condition_value(*value->cget<matrix<std::uint8_t>>());
        case ba_uint16_mat:
            return matrix_condition_value(*value->cget<matrix<std::uint16_t>>());
        case ba_uint_mat:
            return matrix_condition_value(*value->cget<matrix<std::uint32_t>>());
        case ba_uint64_mat:
            return matrix_condition_value(*value->cget<matrix<std::uint64_t>>());
        case ba_double_mat:
            return matrix_condition_value(*value->cget<matrix<double>>());
        case ba_single_mat:
            return matrix_condition_value(*value->cget<matrix<float>>());
        case ba_complex_double_mat:
            return complex_matrix_condition_value(*value->cget<matrix<std::complex<double>>>());
        case ba_complex_single_mat:
            return complex_matrix_condition_value(*value->cget<matrix<std::complex<float>>>());
        case ba_char_mat:
            return char_matrix_condition_value(*value->cget<matrix<char>>());
        case ba_bool_mat:
            return bool_matrix_condition_value(*value->cget<matrix<bool>>());
        default:
            break;
    }

    throw std::runtime_error("该 ba_obj 对象不能转化为 bool 标量。");
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

const Frame::ValueTable& Frame::values() const {
    return values_;
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

void Frame::store_value(ValueId id, Value value) {
    if (id == InvalidValueId) {
        return;
    }
    values_[id] = std::move(value);
}

void Frame::store_instruction_values(const Instruction& instruction, const std::vector<Value>& values) {
    const std::size_t count = std::min(instruction.value_count(), values.size());
    for (std::size_t i = 0; i < count; ++i) {
        const ValueRef ref = instruction.value_ref(i);
        if (!ref.is_valid()) {
            continue;
        }
        store_value(ref.id, values[i]);
    }
}

Value Frame::load(const std::string& name) const {
    auto it = symbols_.find(name);
    if (it == symbols_.end()) {
        if (Value constant = maybe_eval_named_constant(name); constant != nullptr) {
            return constant;
        }
        throw std::runtime_error("未定义的符号：" + name);
    }
    if (!it->second.initialized || it->second.value == nullptr) {
        throw std::runtime_error("符号尚未初始化：" + name);
    }
    return it->second.value;
}

Value Frame::load_value(ValueId id) const {
    auto it = values_.find(id);
    if (it == values_.end()) {
        throw std::runtime_error("未定义的 IR 值槽：" + std::to_string(id));
    }
    return it->second;
}

Value Frame::load_value(const ValueRef& ref) const {
    if (!ref.is_valid()) {
        throw std::runtime_error("尝试读取非法的 ValueRef。");
    }
    return load_value(ref.id);
}

bool Frame::contains(const std::string& name) const {
    return symbols_.find(name) != symbols_.end();
}

bool Frame::is_initialized(const std::string& name) const {
    auto it = symbols_.find(name);
    return it != symbols_.end() && it->second.initialized;
}

bool Frame::has_value(ValueId id) const {
    return values_.find(id) != values_.end();
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
            return record_and_return(frame, *instruction,
                                     std::make_shared<ba_obj>(text->text().c_str(), ba_char_mat));
        }
        case Instruction::Binding: {
            const auto* binding = static_cast<const BindingInstruction*>(instruction);
            if (!frame.contains(binding->name())) {
                if (Value constant = maybe_eval_named_constant(binding->name()); constant != nullptr) {
                    return record_and_return(frame, *instruction, constant);
                }
            }
            return record_and_return(frame, *instruction, frame.load(binding->name()));
        }
        case Instruction::Number: {
            const auto* number = static_cast<const NumberInstruction*>(instruction);
            return record_and_return(frame, *instruction, std::visit(
                [](const auto& item) -> Value { return std::make_shared<ba_obj>(item); },
                number->value()));
        }
        case Instruction::Undef:
            return record_and_return(frame, *instruction, nullptr);
        case Instruction::UnaryOp: {
            const auto* unaryop = static_cast<const UnaryOpInstruction*>(instruction);
            return record_and_return(frame, *instruction,
                                     eval_unaryop(unaryop->op(),
                                                  load_use_value(frame, nullptr,
                                                                 unaryop->operand_ref())));
        }
        case Instruction::BinOp: {
            const auto* binop = static_cast<const BinOpInstruction*>(instruction);
            return record_and_return(frame, *instruction,
                                     eval_binop(binop->op(),
                                                load_use_value(frame, nullptr, binop->lhs_ref()),
                                                load_use_value(frame, nullptr, binop->rhs_ref())));
        }
        case Instruction::Phi: {
            if (!instruction->has_values()) {
                throw std::runtime_error("phi 节点缺少结果值定义。");
            }
            const ValueRef ref = instruction->value_ref();
            if (!ref.is_valid() || !frame.has_value(ref.id)) {
                throw std::runtime_error("phi 节点必须在基本块入口先完成求值。");
            }
            return frame.load_value(ref);
        }
        case Instruction::Call: {
            const auto* call = static_cast<const CallInstruction*>(instruction);
            std::vector<Value> out_args = eval_call_instruction(*call, frame, 1);
            if (out_args.empty()) {
                return record_and_return(frame, *instruction, std::make_shared<ba_obj>());
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
            frame.store(assign->name(), load_use_value(frame, nullptr, assign->value_ref()));
            return;
        }
        case Instruction::Call: {
            const auto* call = static_cast<const CallInstruction*>(instruction);
            (void)eval_call_instruction(*call, frame, 0);
            return;
        }
        case Instruction::Phi:
            throw std::runtime_error("phi 节点必须位于基本块入口，不能按普通指令执行。");
        case Instruction::Text:
        case Instruction::Binding:
        case Instruction::Number:
        case Instruction::Undef:
        case Instruction::UnaryOp:
        case Instruction::BinOp:
            materialize_instruction_value(frame, *instruction);
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
            Value cond_value = load_use_value(frame, nullptr, cond_jump->cond_ref());
            return condition_value_as_bool(cond_value) ? cond_jump->true_block()
                                                       : cond_jump->false_block();
        }
        case Instruction::Jump: {
            const auto* jump = static_cast<const JumpInstruction*>(instruction);
            return jump->target();
        }
        case Instruction::Return:
            frame.set_returned(true);
            frame.set_outputs(
                collect_return_values(*static_cast<const ReturnInstruction*>(instruction), frame));
            return nullptr;
        case Instruction::Text:
        case Instruction::Binding:
        case Instruction::Number:
        case Instruction::Undef:
        case Instruction::UnaryOp:
        case Instruction::BinOp:
        case Instruction::Phi:
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
        frame.store_value(function.input_ref(i).id,
                          args[i] == nullptr ? nullptr : std::make_shared<ba_obj>(*args[i]));
    }
    for (const std::string& output_name : function.output_names()) {
        frame.declare(output_name);
    }

    BasicBlock* block = function.entry_block();
    BasicBlock* predecessor = nullptr;
    while (block != nullptr && !frame.returned()) {
        exec_phi_nodes(*block, predecessor, frame);

        for (Instruction* instruction : block->instructions()) {
            if (instruction != nullptr && instruction->type() == Instruction::Phi) {
                continue;
            }
            exec_inst(instruction, frame);
        }
        BasicBlock* next_block = exec_terminal(block->terminal(), frame);
        predecessor = block;
        block = next_block;
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
