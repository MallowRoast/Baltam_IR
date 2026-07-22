#include "pass/constant_folding_pass.h"

#include "ba_obj/ba_obj.h"
#include "ba_obj/ba_type.h"
#include "baltam_worker/builtin_manager.h"

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace baltam {
namespace {

void ensure_builtin_library_loaded() {
    static std::once_flag once;
    std::call_once(once, [] {
        load_builtin_library();
    });
}

[[nodiscard]] TypeFact unknown_type_fact() noexcept {
    return {};
}

[[nodiscard]] TypeFact scalar_type_fact(TypeSet types) noexcept {
    TypeFact fact;
    fact.is_unknown = false;
    fact.is_scalar = true;
    fact.types = types;
    return fact;
}

[[nodiscard]] TypeSet type_set_from_runtime_type(int type) noexcept {
    switch (type) {
        case ba_bool_mat:
            return TypeSet::logical();
        case ba_int8_mat:
            return TypeSet::int8();
        case ba_int16_mat:
            return TypeSet::int16();
        case ba_int32_mat:
            return TypeSet::int32();
        case ba_int64_mat:
            return TypeSet::int64();
        case ba_uint8_mat:
            return TypeSet::uint8();
        case ba_uint16_mat:
            return TypeSet::uint16();
        case ba_uint32_mat:
            return TypeSet::uint32();
        case ba_uint64_mat:
            return TypeSet::uint64();
        case ba_single_mat:
            return TypeSet::float32();
        case ba_double_mat:
            return TypeSet::float64();
        default:
            break;
    }

    return TypeSet::bottom();
}

[[nodiscard]] bool is_supported_scalar_runtime_object(const ba_obj& obj) {
    if (!obj.is_scalar()) {
        return false;
    }

    return !type_set_from_runtime_type(obj.type()).empty();
}

[[nodiscard]] TypeFact type_fact_from_runtime_object(const ba_obj& obj) {
    const TypeSet types = type_set_from_runtime_type(obj.type());
    if (types.empty() || !obj.is_scalar()) {
        return unknown_type_fact();
    }

    return scalar_type_fact(types);
}

[[nodiscard]] std::optional<const_ba_obj_ptr> ba_obj_from_constant(
    const Constant& constant) {
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
    if (const auto* value = std::get_if<RuntimeObjectConstant>(&constant)) {
        if (value->value == nullptr ||
            !is_supported_scalar_runtime_object(*value->value)) {
            return std::nullopt;
        }
        return value->value;
    }

    return std::nullopt;
}

[[nodiscard]] const char* builtin_name_for_unary(UnaryOp op) noexcept {
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

[[nodiscard]] const char* builtin_name_for_binary(BinaryOp op) noexcept {
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

[[nodiscard]] const ConstInst* const_def_for_operand(
    const CodeUnit& unit,
    const Operand& operand) {
    const auto* value = std::get_if<ValueId>(&operand);
    if (value == nullptr) {
        return nullptr;
    }

    const ValueInfo* value_info = unit.value_table.find(*value);
    if (value_info == nullptr ||
        value_info->def == nullptr ||
        value_info->def->type() != Instruction::Const) {
        return nullptr;
    }

    return static_cast<const ConstInst*>(value_info->def);
}

[[nodiscard]] bool operands_are_constants(
    const CodeUnit& unit,
    const UnaryInst& inst) {
    return const_def_for_operand(unit, inst.operand) != nullptr;
}

[[nodiscard]] bool operands_are_constants(
    const CodeUnit& unit,
    const BinaryInst& inst) {
    return const_def_for_operand(unit, inst.lhs) != nullptr &&
        const_def_for_operand(unit, inst.rhs) != nullptr;
}

[[nodiscard]] bool operands_are_constants(
    const CodeUnit& unit,
    const CallInst& inst) {
    if (inst.callee_kind != CallInst::Direct ||
        !std::holds_alternative<InternedString>(inst.callee) ||
        std::get<InternedString>(inst.callee) != "sin" ||
        inst.arguments.size() != 1U ||
        inst.results.size() != 1U ||
        !inst.results.front().is_valid()) {
        return false;
    }

    for (const Operand& argument : inst.arguments) {
        if (const_def_for_operand(unit, argument) == nullptr) {
            return false;
        }
    }

    return true;
}

[[nodiscard]] bool is_foldable_inst(
    const CodeUnit& unit,
    const Instruction& instruction) {
    switch (instruction.type()) {
        case Instruction::Unary:
            return operands_are_constants(
                unit,
                static_cast<const UnaryInst&>(instruction));
        case Instruction::Binary:
            return operands_are_constants(
                unit,
                static_cast<const BinaryInst&>(instruction));
        case Instruction::Call:
            return operands_are_constants(
                unit,
                static_cast<const CallInst&>(instruction));
        default:
            break;
    }

    return false;
}

[[nodiscard]] std::optional<const_ba_obj_ptr> invoke_builtin(
    std::string_view name,
    std::vector<const_ba_obj_ptr> arguments) {
    if (name.empty()) {
        return std::nullopt;
    }

    try {
        ensure_builtin_library_loaded();

        baFunPtr function = nullptr;
        const std::string function_name(name);
        if (!lookup_builtin_function(function_name, function) || function == nullptr) {
            return std::nullopt;
        }

        std::vector<ba_obj_ptr> outputs;
        outputs.push_back(std::make_shared<ba_obj>());
        function(arguments, outputs);

        if (outputs.size() != 1U ||
            outputs.front() == nullptr ||
            !is_supported_scalar_runtime_object(*outputs.front())) {
            return std::nullopt;
        }

        return std::make_shared<ba_obj>(*outputs.front());
    } catch (...) {
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<const_ba_obj_ptr> fold_unary(
    const CodeUnit& unit,
    const UnaryInst& inst) {
    const ConstInst* operand_const = const_def_for_operand(unit, inst.operand);
    if (operand_const == nullptr) {
        return std::nullopt;
    }

    std::optional<const_ba_obj_ptr> operand = ba_obj_from_constant(operand_const->value);
    if (!operand.has_value() ||
        *operand == nullptr ||
        !is_supported_scalar_runtime_object(**operand)) {
        return std::nullopt;
    }

    std::vector<const_ba_obj_ptr> arguments;
    arguments.push_back(*operand);
    return invoke_builtin(builtin_name_for_unary(inst.op), std::move(arguments));
}

[[nodiscard]] std::optional<const_ba_obj_ptr> fold_binary(
    const CodeUnit& unit,
    const BinaryInst& inst) {
    const ConstInst* lhs_const = const_def_for_operand(unit, inst.lhs);
    const ConstInst* rhs_const = const_def_for_operand(unit, inst.rhs);
    if (lhs_const == nullptr || rhs_const == nullptr) {
        return std::nullopt;
    }

    std::optional<const_ba_obj_ptr> lhs = ba_obj_from_constant(lhs_const->value);
    std::optional<const_ba_obj_ptr> rhs = ba_obj_from_constant(rhs_const->value);
    if (!lhs.has_value() ||
        !rhs.has_value() ||
        *lhs == nullptr ||
        *rhs == nullptr ||
        !is_supported_scalar_runtime_object(**lhs) ||
        !is_supported_scalar_runtime_object(**rhs)) {
        return std::nullopt;
    }

    std::vector<const_ba_obj_ptr> arguments;
    arguments.push_back(*lhs);
    arguments.push_back(*rhs);
    return invoke_builtin(builtin_name_for_binary(inst.op), std::move(arguments));
}

[[nodiscard]] std::optional<const_ba_obj_ptr> fold_call(
    const CodeUnit& unit,
    const CallInst& inst) {
    if (inst.callee_kind != CallInst::Direct ||
        !std::holds_alternative<InternedString>(inst.callee) ||
        std::get<InternedString>(inst.callee) != "sin" ||
        inst.arguments.size() != 1U ||
        inst.results.size() != 1U ||
        !inst.results.front().is_valid()) {
        return std::nullopt;
    }

    std::vector<const_ba_obj_ptr> arguments;
    arguments.reserve(inst.arguments.size());
    for (const Operand& argument : inst.arguments) {
        const ConstInst* argument_const = const_def_for_operand(unit, argument);
        if (argument_const == nullptr) {
            return std::nullopt;
        }

        std::optional<const_ba_obj_ptr> argument_obj =
            ba_obj_from_constant(argument_const->value);
        if (!argument_obj.has_value() ||
            *argument_obj == nullptr ||
            !is_supported_scalar_runtime_object(**argument_obj)) {
            return std::nullopt;
        }

        arguments.push_back(*argument_obj);
    }

    return invoke_builtin(
        std::get<InternedString>(inst.callee),
        std::move(arguments));
}

[[nodiscard]] std::optional<const_ba_obj_ptr> fold_operator(
    const CodeUnit& unit,
    const Instruction& instruction) {
    switch (instruction.type()) {
        case Instruction::Unary:
            return fold_unary(unit, static_cast<const UnaryInst&>(instruction));
        case Instruction::Binary:
            return fold_binary(unit, static_cast<const BinaryInst&>(instruction));
        case Instruction::Call:
            return fold_call(unit, static_cast<const CallInst&>(instruction));
        default:
            break;
    }

    return std::nullopt;
}

[[nodiscard]] ValueId operator_result(const Instruction& instruction) noexcept {
    switch (instruction.type()) {
        case Instruction::Unary:
            return static_cast<const UnaryInst&>(instruction).result;
        case Instruction::Binary:
            return static_cast<const BinaryInst&>(instruction).result;
        case Instruction::Call: {
            const auto& inst = static_cast<const CallInst&>(instruction);
            return inst.results.size() == 1U ? inst.results.front() : InvalidValueId;
        }
        default:
            break;
    }

    return InvalidValueId;
}

void update_value_table_for_folded_const(
    CodeUnit& unit,
    ConstInst& inst,
    const ba_obj& value) {
    ValueInfo* value_info = unit.value_table.find(inst.result);
    if (value_info == nullptr) {
        return;
    }

    value_info->result_index = 0;
    value_info->def = &inst;
    value_info->type_fact = type_fact_from_runtime_object(value);
}

[[nodiscard]] bool replace_with_folded_const(
    CodeUnit& unit,
    std::unique_ptr<Instruction>& instruction_ptr,
    const_ba_obj_ptr folded_value) {
    if (instruction_ptr == nullptr || folded_value == nullptr) {
        return false;
    }

    Instruction& old_instruction = *instruction_ptr;
    const ValueId result = operator_result(old_instruction);
    if (!result.is_valid()) {
        return false;
    }

    auto folded_const = std::make_unique<ConstInst>();
    folded_const->parent = old_instruction.parent;
    folded_const->source_span = old_instruction.source_span;
    folded_const->attrs.is_synthetic = old_instruction.attrs.is_synthetic;
    folded_const->result = result;
    folded_const->value = RuntimeObjectConstant{std::move(folded_value), true};

    ConstInst* folded_const_ptr = folded_const.get();
    instruction_ptr = std::move(folded_const);

    const auto& runtime_constant =
        std::get<RuntimeObjectConstant>(folded_const_ptr->value);
    update_value_table_for_folded_const(unit, *folded_const_ptr, *runtime_constant.value);
    return true;
}

} // namespace

IRPassResult ConstantFoldingPass::run(CodeUnit& unit, IRPassContext& context) {
    (void)context;

    IRPassResult result;
    bool changed = false;

    for (const auto& block_ptr : unit.basic_blocks) {
        if (block_ptr == nullptr) {
            continue;
        }

        for (auto& instruction_ptr : block_ptr->instructions) {
            if (instruction_ptr == nullptr ||
                !is_foldable_inst(unit, *instruction_ptr)) {
                continue;
            }

            std::optional<const_ba_obj_ptr> folded_value =
                fold_operator(unit, *instruction_ptr);
            if (!folded_value.has_value() || *folded_value == nullptr) {
                continue;
            }

            changed = replace_with_folded_const(
                unit,
                instruction_ptr,
                std::move(*folded_value)) || changed;
        }
    }

    result.changed = changed;
    return result;
}

} // namespace baltam
