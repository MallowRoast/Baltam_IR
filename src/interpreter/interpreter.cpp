#include "interpreter/interpreter.h"

#include <stdexcept>
#include <utility>

#include "ba_obj/ba_obj.h"
#include "baltam_worker/builtin_manager.h"
#include "print/obj2str.h"

namespace baltam {
Value eval_expr(Instruction* instruction, Frame& frame);
void exec_inst(Instruction* instruction, Frame& frame);
BasicBlock* exec_terminal(Instruction* instruction, Frame& frame);

namespace {

std::vector<Value> invoke_builtin(const std::string& name, const std::vector<Value>& in_args,
                                  std::size_t out_count) {
    baFunPtr function_ptr = nullptr;
    if (!lookup_builtin_function(name, function_ptr) || function_ptr == nullptr) {
        throw std::runtime_error("找不到内建函数：" + name);
    }

    std::vector<__const_ba_obj_p> builtin_in_args;
    builtin_in_args.reserve(in_args.size());
    for (const Value& arg : in_args) {
        builtin_in_args.push_back(arg);
    }

    std::vector<Value> out_args(out_count);
    for (Value& out_arg : out_args) {
        out_arg = std::make_shared<ba_obj>();
    }

    function_ptr(builtin_in_args, out_args);
    return out_args;
}

Value eval_binop(BinOpInstruction::Type op, Value lhs, Value rhs) {
    std::string name;
    switch (op) {
        case BinOpInstruction::Add:
            name = "plus";
            break;
        case BinOpInstruction::Gt:
            name = "gt";
            break;
        case BinOpInstruction::Multiply:
            name = "times";
            break;
    }

    std::vector<Value> results = invoke_builtin(name, {std::move(lhs), std::move(rhs)}, 1);
    return results.front();
}

std::vector<Value> eval_call(const std::string& name, const std::vector<Value>& in_args,
                             std::size_t out_count) {
    return invoke_builtin(name, in_args, out_count);
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

std::vector<Value> eval_call_instruction(const CallInstruction& instruction, Frame& frame) {
    std::vector<Value> in_args;
    in_args.reserve(instruction.in_args().size());
    for (Instruction* in_arg : instruction.in_args()) {
        in_args.push_back(eval_expr(in_arg, frame));
    }

    const std::size_t out_count = instruction.out_args().empty() ? 1 : instruction.out_args().size();
    std::vector<Value> out_args = eval_call(instruction.name(), in_args, out_count);

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
        case Instruction::BinOp: {
            const auto* binop = static_cast<const BinOpInstruction*>(instruction);
            return eval_binop(binop->op(), eval_expr(binop->lhs(), frame),
                              eval_expr(binop->rhs(), frame));
        }
        case Instruction::Call: {
            const auto* call = static_cast<const CallInstruction*>(instruction);
            std::vector<Value> out_args = eval_call_instruction(*call, frame);
            if (out_args.empty()) {
                return std::make_shared<ba_obj>();
            }
            return out_args.front();
        }
        case Instruction::Text:
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
            (void)eval_call_instruction(*call, frame);
            return;
        }
        case Instruction::Text:
        case Instruction::Name:
        case Instruction::Number:
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
        frame.store(function.input_names()[i], args[i]);
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
