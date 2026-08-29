#include "runtime/ir_executor.h"

#include "runtime/interpreter_context.h"
#include "runtime/lookup.h"

#include <complex>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

namespace baltam {
namespace {

[[nodiscard]] ba_obj_ptr eval_constant(const Constant& constant) {
    if (const auto* value = std::get_if<LogicalConstant>(&constant)) {
        return std::make_shared<ba_obj>(value->value);
    }
    if (const auto* value = std::get_if<Int64Constant>(&constant)) {
        return std::make_shared<ba_obj>(value->value);
    }
    if (const auto* value = std::get_if<UInt64Constant>(&constant)) {
        return std::make_shared<ba_obj>(value->value);
    }
    if (const auto* value = std::get_if<Float64Constant>(&constant)) {
        return std::make_shared<ba_obj>(value->value);
    }
    if (const auto* value = std::get_if<Complex128Constant>(&constant)) {
        return std::make_shared<ba_obj>(
            std::complex<double>(value->re, value->im));
    }
    if (const auto* value = std::get_if<CharLiteralConstant>(&constant)) {
        return std::make_shared<ba_obj>(value->value);
    }
    if (const auto* value = std::get_if<StringLiteralConstant>(&constant)) {
        return std::make_shared<ba_obj>(value->value);
    }
    if (std::get_if<EmptyDoubleMatrixConstant>(&constant) != nullptr) {
        return ba_obj::make_default_value("double", {0, 0});
    }
    if (const auto* value = std::get_if<RuntimeObjectConstant>(&constant)) {
        if (value->value == nullptr) {
            throw std::runtime_error("运行时对象常量未绑定");
        }
        return std::make_shared<ba_obj>(*value->value);
    }

    throw std::runtime_error("未知的运行时常量类型");
}

[[nodiscard]] const char* unary_function_name(UnaryOp op) noexcept {
    switch (op) {
        case Uplus:
            return "uplus";
        case Uminus:
            return "uminus";
        case LogicalNot:
            return "not";
        case Transpose:
            return "transpose";
        case Ctranspose:
            return "ctranspose";
    }

    return "";
}

[[nodiscard]] const char* binary_function_name(BinaryOp op) noexcept {
    switch (op) {
        case Add:
            return "plus";
        case Sub:
            return "minus";
        case Mul:
            return "mtimes";
        case Rdiv:
            return "mrdivide";
        case Ldiv:
            return "mldivide";
        case Pow:
            return "mpower";
        case ElemMul:
            return "times";
        case ElemRdiv:
            return "rdivide";
        case ElemLdiv:
            return "ldivide";
        case ElemPow:
            return "power";
        case And:
            return "and";
        case Or:
            return "or";
        case Lt:
            return "lt";
        case Le:
            return "le";
        case Gt:
            return "gt";
        case Ge:
            return "ge";
        case Eq:
            return "eq";
        case Ne:
            return "ne";
    }

    return "";
}

[[nodiscard]] const SlotInfo& require_slot_info(
    const RuntimeFrame& frame,
    Slot slot) {
    const SlotInfo* info = frame.code->unit->slot_table.find_slot(slot);
    if (info == nullptr) {
        throw std::runtime_error("运行时 slot 不属于当前代码单元");
    }
    return *info;
}

[[nodiscard]] ba_obj_ptr eval_slot_value(RuntimeFrame& frame, Slot slot) {
    if (!slot.id.is_valid()) {
        throw std::runtime_error("运行时 slot 不属于当前代码单元");
    }

    const auto slot_index = static_cast<std::size_t>(slot.id.value());
    if (slot_index < frame.slot_values.size()) {
        if (ba_obj_ptr value = frame.slot_values[slot_index]) {
            return value;
        }
    }

    const SlotTable& slot_table = frame.code->unit->slot_table;
    if (slot_index >= slot_table.slots.size()) {
        throw std::runtime_error("运行时 slot 不属于当前代码单元");
    }

    const SlotInfo& info = slot_table.slots[slot_index];
    if (frame.dynamic_bindings != nullptr) {
        const auto it = frame.dynamic_bindings->values.find(info.name);
        if (it != frame.dynamic_bindings->values.end()) {
            if (it->second == nullptr) {
                throw std::runtime_error("运行时动态符号未绑定");
            }
            return it->second;
        }
    }

    if (frame.context != nullptr && info.slot.tag == SlotTag::BaseVar) {
        if (ba_obj_ptr value = frame.context->base_workspace.find(info.name)) {
            return value;
        }
    }

    throw std::runtime_error("运行时 slot 未绑定: " + info.name);
}

void store_slot(RuntimeFrame& frame, Slot slot, ba_obj_ptr value) {
    if (value == nullptr) {
        throw std::runtime_error("不能存储未绑定的运行时值");
    }
    const SlotInfo& info = require_slot_info(frame, slot);
    const auto slot_index = static_cast<std::size_t>(info.slot.id.value());

    switch (info.slot.tag) {
        case SlotTag::BaseVar:
            if (frame.context == nullptr) {
                throw std::runtime_error("基础工作区 slot 需要 InterpreterContext");
            }
            frame.slot_values[slot_index] = value;
            frame.context->base_workspace.values[info.name] = value;
            return;
        case SlotTag::Global:
            if (frame.context == nullptr) {
                throw std::runtime_error("全局 slot 需要 InterpreterContext");
            }
            frame.slot_values[slot_index] = value;
            frame.context->globals.values[info.name] = value;
            return;
        case SlotTag::Persistent:
            frame.slot_values[slot_index] = value;
            frame.code->persistent.values[info.slot.id] = value;
            return;
        case SlotTag::ScriptVar:
            frame.slot_values[slot_index] = value;
            if (frame.dynamic_bindings != nullptr) {
                frame.dynamic_bindings->values[info.name] = value;
            } else if (frame.context != nullptr &&
                       frame.code != nullptr &&
                       frame.code->unit != nullptr &&
                       frame.code->unit->is_script()) {
                frame.context->base_workspace.values[info.name] = value;
            }
            return;
        case SlotTag::Local:
        case SlotTag::Arg:
        case SlotTag::Ret:
        case SlotTag::Capture:
        case SlotTag::InternalLocal:
        case SlotTag::Nargin:
        case SlotTag::Nargout:
        case SlotTag::Varargin:
        case SlotTag::Varargout:
            frame.slot_values[slot_index] = std::move(value);
            return;
    }

    throw std::runtime_error("未知的运行时 slot tag");
}

[[nodiscard]] ba_obj_ptr eval_operand(RuntimeFrame& frame, const Operand& operand) {
    if (const auto* value = std::get_if<ValueId>(&operand)) {
        ba_obj_ptr result = frame.temporary(*value);
        if (result == nullptr) {
            throw std::runtime_error("运行时临时值未绑定");
        }
        return result;
    }
    if (const auto* slot = std::get_if<Slot>(&operand)) {
        return eval_slot_value(frame, *slot);
    }
    if (std::get_if<InternedString>(&operand) != nullptr) {
        throw std::runtime_error("名字操作数不能作为运行时值求值");
    }

    throw std::runtime_error("未知的运行时操作数类型");
}

[[nodiscard]] ba_obj_ptr eval_const(const ConstInst& inst) {
    return eval_constant(inst.value);
}

[[nodiscard]] ba_obj_ptr eval_load_slot(
    RuntimeFrame& frame,
    const LoadSlotInst& inst) {
    return eval_slot_value(frame, inst.slot);
}

void eval_global_decl(RuntimeFrame& frame, const GlobalDeclInst& inst) {
    if (frame.context == nullptr) {
        throw std::runtime_error("全局变量声明需要 InterpreterContext");
    }

    for (Slot slot : inst.slots) {
        const SlotInfo& info = require_slot_info(frame, slot);
        ba_obj_ptr& value = frame.context->globals.values[info.name];
        if (slot.id.is_valid() && slot.id.value() < frame.slot_values.size()) {
            frame.slot_values[slot.id.value()] = value;
        }
    }
}

void eval_persistent_decl(RuntimeFrame& frame, const PersistentDeclInst& inst) {
    for (Slot slot : inst.slots) {
        ba_obj_ptr& value = frame.code->persistent.values[slot.id];
        if (slot.id.is_valid() && slot.id.value() < frame.slot_values.size()) {
            frame.slot_values[slot.id.value()] = value;
        }
    }
}

[[noreturn]] void eval_create_named_function_handle(
    RuntimeFrame&,
    const CreateNamedFunctionHandleInst& inst) {
    throw std::runtime_error(
        "暂不支持的运行时指令: " +
        std::to_string(static_cast<int>(inst.type())));
}

[[noreturn]] void eval_create_anonymous_function_handle(
    RuntimeFrame&,
    const CreateAnonymousFunctionHandleInst& inst) {
    throw std::runtime_error(
        "暂不支持的运行时指令: " +
        std::to_string(static_cast<int>(inst.type())));
}

[[nodiscard]] std::vector<const_ba_obj_ptr> eval_call_arguments(
    RuntimeFrame& frame,
    const std::vector<Operand>& operands);

[[nodiscard]] std::vector<ba_obj_ptr> invoke_target(
    RuntimeFrame& caller,
    const RuntimeFunctionLookup& target,
    std::vector<const_ba_obj_ptr>& arguments,
    std::size_t output_count,
    const char* not_callable_prefix);

void store_call_outputs(
    RuntimeFrame& frame,
    const std::vector<ValueId>& results,
    const std::vector<ba_obj_ptr>& outputs);

[[nodiscard]] ba_obj_ptr lookup_dynamic_value(
    RuntimeFrame& frame,
    const InternedString& name) {
    if (frame.dynamic_bindings != nullptr) {
        const auto it = frame.dynamic_bindings->values.find(name);
        if (it != frame.dynamic_bindings->values.end()) {
            return it->second;
        }
    }
    if (frame.context != nullptr) {
        return frame.context->base_workspace.find(name);
    }
    return {};
}

void eval_apply(RuntimeFrame& frame, const ApplyInst& inst) {
    if (const auto* name = std::get_if<InternedString>(&inst.callee_or_base)) {
        if (lookup_dynamic_value(frame, *name) != nullptr) {
            throw std::runtime_error("暂不支持变量圆括号应用: " + *name);
        }

        const RuntimeFunctionLookup target = lookup_function(frame, *name);
        if (!target.found()) {
            throw std::runtime_error("未找到运行时 apply 目标: " + *name);
        }

        std::vector<const_ba_obj_ptr> arguments =
            eval_call_arguments(frame, inst.arguments);
        std::vector<ba_obj_ptr> outputs = invoke_target(
            frame,
            target,
            arguments,
            inst.results.size(),
            "运行时 apply 目标暂不可调用: ");
        store_call_outputs(frame, inst.results, outputs);
        return;
    }

    (void)eval_operand(frame, inst.callee_or_base);
    throw std::runtime_error("暂不支持值圆括号应用");
}

[[noreturn]] void eval_value_apply(RuntimeFrame&, const ValueApplyInst& inst) {
    throw std::runtime_error(
        "暂不支持的运行时指令: " +
        std::to_string(static_cast<int>(inst.type())));
}

[[noreturn]] void eval_magic_end(RuntimeFrame&, const MagicEndInst& inst) {
    throw std::runtime_error(
        "暂不支持的运行时指令: " +
        std::to_string(static_cast<int>(inst.type())));
}

[[nodiscard]] RuntimeFunctionLookup static_m_function_target(const CallInst& inst) {
    RuntimeFunctionLookup target;
    if (inst.m_function_target == nullptr) {
        return target;
    }

    target.kind = RuntimeFunctionKind::LocalMFunction;
    target.dispatch_type = MFunction;
    target.name = inst.m_function_target->name;
    target.m_function_target = inst.m_function_target;
    if (inst.m_function_target->file != nullptr) {
        target.source_file = inst.m_function_target->file->path;
    }
    return target;
}

[[nodiscard]] RuntimeFunctionLookup resolve_direct_call(
    RuntimeFrame& frame,
    const CallInst& inst) {
    if (inst.callee_kind != CallInst::Direct ||
        !std::holds_alternative<InternedString>(inst.callee)) {
        return {};
    }

    const InternedString& name = std::get<InternedString>(inst.callee);
    if (inst.dispatch_type == MFunction) {
        return static_m_function_target(inst);
    }

    return lookup_function(frame, name, inst.dispatch_type);
}

void append_argument(
    std::vector<const_ba_obj_ptr>& arguments,
    RuntimeFrame& frame,
    const Operand& operand,
    const char* unbound_message) {
    ba_obj_ptr value = eval_operand(frame, operand);
    if (value == nullptr) {
        throw std::runtime_error(unbound_message);
    }
    arguments.push_back(std::move(value));
}

[[nodiscard]] std::vector<const_ba_obj_ptr> eval_call_arguments(
    RuntimeFrame& frame,
    const std::vector<Operand>& operands) {
    std::vector<const_ba_obj_ptr> arguments;
    arguments.reserve(operands.size());
    for (const Operand& operand : operands) {
        append_argument(arguments, frame, operand, "运行时调用参数未绑定");
    }
    return arguments;
}

void store_call_outputs(
    RuntimeFrame& frame,
    const std::vector<ValueId>& results,
    const std::vector<ba_obj_ptr>& outputs) {
    if (outputs.size() < results.size()) {
        throw std::runtime_error("运行时调用返回值数量少于请求数量");
    }

    for (std::size_t i = 0; i < results.size(); ++i) {
        const ValueId result = results[i];
        if (!result.is_valid()) {
            continue;
        }
        if (outputs[i] == nullptr) {
            throw std::runtime_error("运行时调用返回值未绑定");
        }
        frame.temporary(result) = outputs[i];
    }
}

[[nodiscard]] std::vector<ba_obj_ptr> make_call_outputs(std::size_t count) {
    std::vector<ba_obj_ptr> outputs;
    outputs.reserve(count == 0 ? 1 : count);
    if (count == 0) {
        // 零输出调用也要给 entry-point 一个 ans 占位，供 tic/toc 一类 builtin
        // 通过 out_args[0]->is_ans() 判断真实输出是否为空。
        outputs.push_back(ba_obj::make_void(V_ANS));
        return outputs;
    }
    for (std::size_t i = 0; i < count; ++i) {
        outputs.push_back(std::make_shared<ba_obj>());
    }
    return outputs;
}

[[nodiscard]] std::shared_ptr<CodeObject> code_for_m_function(
    InterpreterContext& context,
    FunctionUnit& function) {
    const NormalizedPath source_file =
        function.file != nullptr ? function.file->path : NormalizedPath{};
    if (!source_file.empty()) {
        const CodeCacheKey key{source_file, function.name};
        if (std::shared_ptr<CodeObject> cached = context.code_cache.find(key);
            cached != nullptr && cached->unit == &function) {
            return cached;
        }
    }

    auto code = std::make_shared<CodeObject>();
    code->unit = &function;
    if (!source_file.empty()) {
        context.code_cache.insert({source_file, function.name}, code);
    }
    return code;
}

void initialize_function_runtime_slots(RuntimeFrame& frame) {
    if (frame.code == nullptr || frame.code->unit == nullptr) {
        return;
    }

    for (const SlotInfo& info : frame.code->unit->slot_table.slots) {
        if (!info.slot.id.is_valid() || info.slot.id.value() >= frame.slot_values.size()) {
            continue;
        }

        ba_obj_ptr value;
        switch (info.slot.tag) {
            case SlotTag::Nargin:
                value = std::make_shared<ba_obj>(static_cast<double>(frame.actual_nargin));
                break;
            case SlotTag::Nargout:
                value = std::make_shared<ba_obj>(static_cast<double>(frame.requested_nargout));
                break;
            default:
                break;
        }

        if (value != nullptr) {
            frame.slot_values[info.slot.id.value()] = std::move(value);
        }
    }
}

void bind_function_arguments(
    RuntimeFrame& frame,
    const FunctionUnit& function,
    const std::vector<const_ba_obj_ptr>& arguments) {
    if (arguments.size() > function.param_slots.size()) {
        throw std::runtime_error(
            "运行时 M 函数输入参数数量过多: " + function.name);
    }

    for (std::size_t i = 0; i < arguments.size(); ++i) {
        if (arguments[i] == nullptr) {
            throw std::runtime_error(
                "运行时 M 函数输入参数未绑定: " + function.name);
        }
        frame.slot_value(function.param_slots[i].id) =
            std::make_shared<ba_obj>(*arguments[i]);
    }
}

[[nodiscard]] std::vector<ba_obj_ptr> invoke_m_function(
    RuntimeFrame& caller,
    FunctionUnit& function,
    std::vector<const_ba_obj_ptr>& arguments,
    std::size_t output_count) {
    if (caller.context == nullptr) {
        throw std::runtime_error("运行时 M 函数调用需要 InterpreterContext: " + function.name);
    }
    if (exceeds_max_call_depth(&caller)) {
        throw std::runtime_error("运行时 M 函数调用深度超过限制: " + function.name);
    }

    std::shared_ptr<CodeObject> code = code_for_m_function(*caller.context, function);

    RuntimeFrame frame;
    frame.code = code.get();
    frame.actual_nargin = static_cast<std::uint32_t>(arguments.size());
    frame.requested_nargout = static_cast<std::uint32_t>(output_count);
    frame.initialize_storage();
    bind_function_arguments(frame, function, arguments);
    initialize_function_runtime_slots(frame);

    std::vector<ba_obj_ptr> outputs;
    {
        FrameScope scope(*caller.context, frame);
        outputs = execute_frame(frame);
    }

    if (outputs.size() < output_count) {
        throw std::runtime_error("运行时 M 函数返回值数量少于请求数量: " + function.name);
    }
    if (outputs.size() > output_count) {
        outputs.resize(output_count);
    }
    return outputs;
}

[[nodiscard]] std::vector<ba_obj_ptr> invoke_entry_point(
    const RuntimeFunctionLookup& target,
    std::vector<const_ba_obj_ptr>& arguments,
    std::size_t output_count) {
    if (target.entry_point == nullptr) {
        throw std::runtime_error(
            "运行时函数没有可调用入口: " + target.name);
    }

    std::vector<ba_obj_ptr> outputs = make_call_outputs(output_count);
    target.entry_point(arguments, outputs);
    return outputs;
}

[[nodiscard]] std::vector<ba_obj_ptr> invoke_target(
    RuntimeFrame& caller,
    const RuntimeFunctionLookup& target,
    std::vector<const_ba_obj_ptr>& arguments,
    std::size_t output_count,
    const char* not_callable_prefix) {
    if (target.entry_point != nullptr) {
        return invoke_entry_point(target, arguments, output_count);
    }

    if (target.m_function_target != nullptr) {
        return invoke_m_function(
            caller,
            *target.m_function_target,
            arguments,
            output_count);
    }

    throw std::runtime_error(std::string(not_callable_prefix) + target.name);
}

void eval_call(RuntimeFrame& frame, const CallInst& inst) {
    if (inst.callee_kind != CallInst::Direct) {
        throw std::runtime_error("暂不支持间接运行时调用");
    }

    const RuntimeFunctionLookup target = resolve_direct_call(frame, inst);
    if (!target.found()) {
        const InternedString name = std::holds_alternative<InternedString>(inst.callee)
            ? std::get<InternedString>(inst.callee)
            : InternedString("<indirect>");
        throw std::runtime_error("未找到运行时函数: " + name);
    }

    std::vector<const_ba_obj_ptr> arguments =
        eval_call_arguments(frame, inst.arguments);
    std::vector<ba_obj_ptr> outputs = invoke_target(
        frame,
        target,
        arguments,
        inst.results.size(),
        "运行时函数目标暂不可调用: ");
    store_call_outputs(frame, inst.results, outputs);
}

[[nodiscard]] RuntimeFunctionLookup resolve_unary_function(
    RuntimeFrame& frame,
    UnaryOp op,
    DispatchType dispatch_type) {
    const InternedString name(unary_function_name(op));
    if (name.empty()) {
        return {};
    }
    return lookup_function(
        frame,
        name,
        dispatch_type);
}

[[nodiscard]] RuntimeFunctionLookup resolve_binary_function(
    RuntimeFrame& frame,
    BinaryOp op,
    DispatchType dispatch_type) {
    const InternedString name(binary_function_name(op));
    if (name.empty()) {
        return {};
    }
    return lookup_function(
        frame,
        name,
        dispatch_type);
}

[[nodiscard]] ba_obj_ptr require_single_output(
    std::vector<ba_obj_ptr>& outputs,
    const char* no_output_message,
    const char* unbound_message) {
    if (outputs.empty()) {
        throw std::runtime_error(no_output_message);
    }
    if (outputs.front() == nullptr) {
        throw std::runtime_error(unbound_message);
    }
    return outputs.front();
}

[[nodiscard]] ba_obj_ptr eval_unary(RuntimeFrame& frame, const UnaryInst& inst) {
    const RuntimeFunctionLookup target =
        resolve_unary_function(frame, inst.op, inst.dispatch_type);
    if (!target.found()) {
        throw std::runtime_error(
            "未找到运行时一元运算符函数: " +
            std::string(unary_function_name(inst.op)));
    }

    std::vector<const_ba_obj_ptr> arguments;
    arguments.reserve(1);
    append_argument(
        arguments,
        frame,
        inst.operand,
        "运行时一元运算操作数未绑定");

    std::vector<ba_obj_ptr> outputs = invoke_target(
        frame,
        target,
        arguments,
        1U,
        "运行时一元运算符目标暂不可调用: ");
    return require_single_output(
        outputs,
        "运行时一元运算符没有返回值",
        "运行时一元运算返回值未绑定");
}

[[nodiscard]] ba_obj_ptr eval_binary(RuntimeFrame& frame, const BinaryInst& inst) {
    ba_obj_ptr lhs = eval_operand(frame, inst.lhs);
    if (lhs == nullptr) {
        throw std::runtime_error("运行时二元运算左操作数未绑定");
    }
    ba_obj_ptr rhs = eval_operand(frame, inst.rhs);
    if (rhs == nullptr) {
        throw std::runtime_error("运行时二元运算右操作数未绑定");
    }

    if (inst.dispatch_type == Internal) {
        switch (inst.op) {
            case Add:
                return std::make_shared<ba_obj>(
                    static_cast<std::int64_t>(lhs->as_int() + rhs->as_int()));
            case Gt:
                return std::make_shared<ba_obj>(lhs->as_int() > rhs->as_int());
            case Or:
                return std::make_shared<ba_obj>(lhs->as_bool() || rhs->as_bool());
            case Sub:
            case Mul:
            case Rdiv:
            case Ldiv:
            case Pow:
            case ElemMul:
            case ElemRdiv:
            case ElemLdiv:
            case ElemPow:
            case And:
            case Lt:
            case Le:
            case Ge:
            case Eq:
            case Ne:
                break;
        }

        throw std::runtime_error(
            "未支持的内部二元运算符: " +
            std::string(binary_function_name(inst.op)));
    }

    const RuntimeFunctionLookup target =
        resolve_binary_function(frame, inst.op, inst.dispatch_type);
    if (!target.found()) {
        throw std::runtime_error(
            "未找到运行时二元运算符函数: " +
            std::string(binary_function_name(inst.op)));
    }

    std::vector<const_ba_obj_ptr> arguments;
    arguments.reserve(2);
    arguments.push_back(std::move(lhs));
    arguments.push_back(std::move(rhs));
    std::vector<ba_obj_ptr> outputs = invoke_target(
        frame,
        target,
        arguments,
        1U,
        "运行时二元运算符目标暂不可调用: ");
    return require_single_output(
        outputs,
        "运行时二元运算符没有返回值",
        "运行时二元运算返回值未绑定");
}

[[nodiscard]] std::vector<ba_obj_ptr> eval_return(
    RuntimeFrame& frame,
    const ReturnInst& inst) {
    std::vector<ba_obj_ptr> outputs;
    outputs.reserve(inst.values.size());
    for (ValueId value : inst.values) {
        ba_obj_ptr output = frame.temporary(value);
        if (output == nullptr) {
            throw std::runtime_error("返回值未绑定");
        }
        outputs.push_back(std::move(output));
    }
    return outputs;
}

void eval_goto(RuntimeFrame& frame, const GotoInst& inst) {
    frame.block = inst.target;
    frame.instruction_index = 0;
}

void eval_branch(RuntimeFrame& frame, const BranchInst& inst) {
    const ba_obj_ptr condition = eval_operand(frame, inst.condition);
    if (condition == nullptr) {
        throw std::runtime_error("运行时分支条件未绑定");
    }
    BasicBlock* target = condition->as_bool()
        ? inst.true_target
        : inst.false_target;

    frame.block = target;
    frame.instruction_index = 0;
}

} // namespace

std::vector<ba_obj_ptr> execute_frame(RuntimeFrame& frame) {
    if (frame.block == nullptr) {
        throw std::runtime_error("运行时 frame 缺少当前基本块");
    }

    while (frame.block != nullptr) {
        if (frame.context != nullptr && frame.context->interrupt_requested.load()) {
            throw std::runtime_error("运行时执行被中断");
        }
        if (frame.instruction_index >= frame.block->instructions.size()) {
            throw std::runtime_error("运行时基本块缺少终结指令");
        }

        const Instruction& instruction =
            *frame.block->instructions[frame.instruction_index];
        ++frame.instruction_index;

        switch (instruction.type()) {
            case Instruction::Goto:
                eval_goto(frame, static_cast<const GotoInst&>(instruction));
                break;
            case Instruction::Branch:
                eval_branch(frame, static_cast<const BranchInst&>(instruction));
                break;
            case Instruction::Return:
                return eval_return(frame, static_cast<const ReturnInst&>(instruction));
            case Instruction::Const: {
                const auto& inst = static_cast<const ConstInst&>(instruction);
                frame.temporary(inst.result) = eval_const(inst);
                break;
            }
            case Instruction::LoadSlot: {
                const auto& inst = static_cast<const LoadSlotInst&>(instruction);
                frame.temporary(inst.result) = eval_load_slot(frame, inst);
                break;
            }
            case Instruction::StoreSlot:
                {
                    const auto& inst = static_cast<const StoreSlotInst&>(instruction);
                    ba_obj_ptr value = frame.temporary(inst.value);
                    if (value == nullptr) {
                        throw std::runtime_error("运行时 store 值未绑定");
                    }
                    store_slot(frame, inst.slot, std::move(value));
                }
                break;
            case Instruction::GlobalDecl:
                eval_global_decl(frame, static_cast<const GlobalDeclInst&>(instruction));
                break;
            case Instruction::PersistentDecl:
                eval_persistent_decl(
                    frame,
                    static_cast<const PersistentDeclInst&>(instruction));
                break;
            case Instruction::CreateNamedFunctionHandle:
                eval_create_named_function_handle(
                    frame,
                    static_cast<const CreateNamedFunctionHandleInst&>(instruction));
                break;
            case Instruction::CreateAnonymousFunctionHandle:
                eval_create_anonymous_function_handle(
                    frame,
                    static_cast<const CreateAnonymousFunctionHandleInst&>(instruction));
                break;
            case Instruction::Apply:
                eval_apply(frame, static_cast<const ApplyInst&>(instruction));
                break;
            case Instruction::ValueApply:
                eval_value_apply(frame, static_cast<const ValueApplyInst&>(instruction));
                break;
            case Instruction::MagicEnd:
                eval_magic_end(frame, static_cast<const MagicEndInst&>(instruction));
                break;
            case Instruction::Call:
                eval_call(frame, static_cast<const CallInst&>(instruction));
                break;
            case Instruction::Unary: {
                const auto& inst = static_cast<const UnaryInst&>(instruction);
                frame.temporary(inst.result) = eval_unary(frame, inst);
                break;
            }
            case Instruction::Binary: {
                const auto& inst = static_cast<const BinaryInst&>(instruction);
                frame.temporary(inst.result) = eval_binary(frame, inst);
                break;
            }
        }
    }

    throw std::runtime_error("运行时执行离开控制流图但没有返回");
}

} // namespace baltam
