#include "ir/ir_unit.h"

#include "ba_obj/ba_obj.h"
#include "print/obj2str.h"

#include <stdexcept>
#include <string>
#include <utility>

namespace baltam {

Effect Instruction::effect() const noexcept {
    switch (type_) {
        case Type::Const:
        case Type::CellConstruct:
        case Type::MagicColon:
        case Type::Goto:
        case Type::Branch:
        case Type::Return:
        case Type::LoadSlot:
        case Type::Intrinsic:
            return Effect::NoSlotWrite;

        case Type::Concat:
        case Type::Colon:
        case Type::MagicEnd:
        case Type::GlobalDecl:
        case Type::PersistentDecl:
        case Type::Apply:
        case Type::ValueApply:
        case Type::Call:
        case Type::ParenReference:
        case Type::BraceReference:
        case Type::DotAccess:
        case Type::DotReference:
        case Type::ParenAssign:
        case Type::ParenDelete:
        case Type::BraceAssign:
        case Type::DotAssign:
        case Type::Unary:
        case Type::Binary:
        case Type::StoreSlot:
            return Effect::MayWriteSlot;
    }
    return Effect::MayWriteSlot;
}

std::string Instruction::to_string() const {
    throw std::logic_error("指令的字符串表示尚未实现");
}

namespace {

std::string quote_literal(std::string_view text, char quote) {
    std::string result(1, quote);
    for (char ch : text) {
        if (ch == quote) result += quote;
        if (ch == '\n') result += "\\n";
        else if (ch == '\r') result += "\\r";
        else if (ch == '\t') result += "\\t";
        else result += ch;
    }
    result += quote;
    return result;
}

std::string dot_member(const Operand& member) {
    if (const auto* name = std::get_if<InternedString>(&member))
        return "." + *name;
    return ".(%" + std::to_string(std::get<ValueId>(member).index) + ")";
}

std::string result_prefix(const std::vector<ValueId> &results) {
    if (results.empty())
        return {};
    if (results.size() == 1)
        return "%" + std::to_string(results.front().index) + " = ";
    std::string text = "[";
    for (std::size_t i = 0; i < results.size(); ++i) {
        if (i)
            text += ", ";
        text += "%" + std::to_string(results[i].index);
    }
    return text + "] = ";
}

const char *dispatch_name(DispatchType type) {
    switch (type) {
    case DispatchType::Dynamic:
        return "dynamic";
    case DispatchType::MFunction:
        return "mfunction";
    case DispatchType::Builtin:
        return "builtin";
    case DispatchType::Internal:
        return "internal";
    case DispatchType::Plugin:
        return "plugin";
    }
    throw std::invalid_argument("无效的调用分派类型");
}

} // 匿名命名空间

std::string ConstInst::to_string() const {
    std::string result = "%" + std::to_string(this->result.index) + " = const";
    if (!value) return result + " <null>";
    std::string literal;
    if (value->is_logical() && value->is_scalar()) {
        literal = value->as_bool() ? "true" : "false";
    }
    else if (value->is_numeric() && value->is_scalar()) {
        literal = internal::obj2str_one_line(*value);
    }
    else if (value->type() == ba_char_mat && value->is_row_vector()) {
        literal = quote_literal(value->as_string(), '\'');
    }
    else if (value->type() == ba_string && value->is_scalar()) {
        literal = quote_literal(value->as_string(), '"');
    }
    std::string summary = value->brief_type_str();
    const auto empty = summary.find(" empty ");
    if (empty != std::string::npos) summary.erase(empty, 6);
    if (!literal.empty()) result += " " + literal;
    return result + " : " + summary;
}

std::string StoreSlotInst::to_string() const {
    return "store slot" + std::to_string(slot.index) + " %" +
           std::to_string(value.index);
}

std::string BraceReferenceInst::to_string() const {
    std::string text = "%" + std::to_string(result.index) + " = brace_ref %" +
                       std::to_string(base.index);
    for (ValueId index : indices)
        text += ", %" + std::to_string(index.index);
    return text;
}

std::string BraceAssignInst::to_string() const {
    std::string text = "%" + std::to_string(result.index) + " = brace_assign %" +
                       std::to_string(base.index);
    for (ValueId index : indices)
        text += ", %" + std::to_string(index.index);
    return text + ", %" + std::to_string(value.index);
}

std::string ApplyInst::to_string() const {
    std::string text = result_prefix(results) + "apply ";
    if (const auto* name = std::get_if<InternedString>(&callee_or_base))
        text += *name;
    else
        text += "%" + std::to_string(std::get<ValueId>(callee_or_base).index);
    text += "(";
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        if (i) text += ", ";
        text += "%" + std::to_string(arguments[i].index);
    }
    return text + ")";
}

std::string CallInst::to_string() const {
    std::string text = result_prefix(results) + "call " + callee + "(";
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        if (i) text += ", ";
        text += "%" + std::to_string(arguments[i].index);
    }
    text += ")";
    if (dispatch_type != DispatchType::Dynamic || m_function_target) {
        text += " [dispatch=";
        text += dispatch_name(dispatch_type);
        if (m_function_target) {
            if (!parent() || !parent()->parent() || !parent()->parent()->parent())
                throw std::invalid_argument("静态调用目标需要所属 IRModule");
            text += ",target=@" + parent()->parent()->parent()->function_path(m_function_target);
        }
        text += "]";
    }
    return text;
}

std::string ValueApplyInst::to_string() const {
    std::string text = result_prefix(results) + "value_apply %" + std::to_string(base.index) + "(";
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        if (i) text += ", ";
        text += "%" + std::to_string(arguments[i].index);
    }
    return text + ")";
}

std::string ParenReferenceInst::to_string() const {
    std::string text = "%" + std::to_string(result.index) + " = paren_ref %" +
                       std::to_string(base.index);
    for (ValueId index : indices) text += ", %" + std::to_string(index.index);
    return text;
}

std::string ParenAssignInst::to_string() const {
    std::string text = "%" + std::to_string(result.index) + " = paren_assign %" +
                       std::to_string(base.index);
    for (ValueId index : indices) text += ", %" + std::to_string(index.index);
    return text + ", %" + std::to_string(value.index);
}

std::string ParenDeleteInst::to_string() const {
    std::string text = "%" + std::to_string(result.index) + " = paren_delete %" +
                       std::to_string(base.index);
    for (ValueId index : indices) text += ", %" + std::to_string(index.index);
    return text;
}

std::string DotAccessInst::to_string() const {
    std::string base;
    if (const auto* name = std::get_if<InternedString>(&base_or_name))
        base = *name;
    else base = "%" + std::to_string(std::get<ValueId>(base_or_name).index);
    return "%" + std::to_string(result.index) + " = dot_access " +
           base + dot_member(member);
}

std::string DotReferenceInst::to_string() const {
    return "%" + std::to_string(result.index) + " = dot_ref %" +
           std::to_string(base.index) + dot_member(member);
}

std::string DotAssignInst::to_string() const {
    return "%" + std::to_string(result.index) + " = dot_assign %" +
           std::to_string(base.index) + dot_member(member) + ", %" +
           std::to_string(value.index);
}

std::string LoadSlotInst::to_string() const {
    return "%" + std::to_string(result.index) + " = load slot" +
           std::to_string(slot.index);
}

std::string ConcatInst::to_string() const {
    std::string text = "%" + std::to_string(result.index) +
                       (axis == Axis::Horizontal ? " = horzcat" : " = vertcat");
    for (std::size_t i = 0; i < items.size(); ++i) {
        text += i == 0 ? " %" : ", %";
        text += std::to_string(items[i].index);
    }
    return text;
}

std::string CellConstructInst::to_string() const {
    std::string text = "%" + std::to_string(result.index) + " = cell";
    for (std::size_t i = 0; i < items.size(); ++i) {
        text += i == 0 ? " %" : ", %";
        text += std::to_string(items[i].index);
    }
    return text + " : " + std::to_string(rows) + "x" +
           std::to_string(cols) + " cell array";
}

std::string ColonInst::to_string() const {
    std::string text = "%" + std::to_string(result.index) + " = colon %" +
                       std::to_string(start.index);
    if (step) text += ", %" + std::to_string(step->index);
    return text + ", %" + std::to_string(stop.index);
}

std::string UnaryInst::to_string() const {
    const char* name = nullptr;
    switch (op) {
        case UnaryOp::Uplus: name = "uplus"; break;
        case UnaryOp::Uminus: name = "uminus"; break;
        case UnaryOp::LogicalNot: name = "not"; break;
        case UnaryOp::Transpose: name = "transpose"; break;
        case UnaryOp::Ctranspose: name = "ctranspose"; break;
    }
    return "%" + std::to_string(result.index) + " = " + name + " %" +
           std::to_string(operand.index);
}

std::string BinaryInst::to_string() const {
    const char* name = nullptr;
    switch (op) {
        case BinaryOp::Add: name = "plus"; break;
        case BinaryOp::Sub: name = "minus"; break;
        case BinaryOp::Mul: name = "mtimes"; break;
        case BinaryOp::Rdiv: name = "mrdivide"; break;
        case BinaryOp::Ldiv: name = "mldivide"; break;
        case BinaryOp::Pow: name = "mpower"; break;
        case BinaryOp::ElemMul: name = "times"; break;
        case BinaryOp::ElemRdiv: name = "rdivide"; break;
        case BinaryOp::ElemLdiv: name = "ldivide"; break;
        case BinaryOp::ElemPow: name = "power"; break;
        case BinaryOp::And: name = "and"; break;
        case BinaryOp::Or: name = "or"; break;
        case BinaryOp::Lt: name = "lt"; break;
        case BinaryOp::Le: name = "le"; break;
        case BinaryOp::Gt: name = "gt"; break;
        case BinaryOp::Ge: name = "ge"; break;
        case BinaryOp::Eq: name = "eq"; break;
        case BinaryOp::Ne: name = "ne"; break;
    }
    return "%" + std::to_string(result.index) + " = " + name + " %" +
           std::to_string(lhs.index) + ", %" + std::to_string(rhs.index);
}

std::string GlobalDeclInst::to_string() const {
    std::string text = "global";
    for (std::size_t i = 0; i < slots.size(); ++i)
        text += (i == 0 ? " slot" : ", slot") + std::to_string(slots[i].index);
    return text;
}

std::string PersistentDeclInst::to_string() const {
    std::string text = "persistent";
    for (std::size_t i = 0; i < slots.size(); ++i)
        text += (i == 0 ? " slot" : ", slot") + std::to_string(slots[i].index);
    return text;
}

std::string GotoInst::to_string() const {
    return "goto " + (target ? target->label() : "<null>");
}

std::string BranchInst::to_string() const {
    return "branch %" + std::to_string(condition.index) + ", " +
           (true_target ? true_target->label() : "<null>") + ", " +
           (false_target ? false_target->label() : "<null>");
}

std::string IntrinsicInst::to_string() const {
    const char* name = nullptr;
    switch (id) {
        case IntrinsicId::SwitchMatch: name = "switch_match"; break;
        case IntrinsicId::ForeachInit: name = "foreach_init"; break;
        case IntrinsicId::ForeachCount: name = "foreach_count"; break;
        case IntrinsicId::ForeachGet: name = "foreach_get"; break;
        case IntrinsicId::IntGt: name = "int_gt"; break;
        case IntrinsicId::IntAdd: name = "int_add"; break;
        case IntrinsicId::LogicalScalar: name = "logical_scalar"; break;
    }
    std::string text = "%" + std::to_string(result.index) +
                       " = intrinsic." + name;
    for (std::size_t i = 0; i < operands.size(); ++i)
        text += (i == 0 ? " %" : ", %") + std::to_string(operands[i].index);
    return text;
}

std::string ReturnInst::to_string() const {
    return "ret";
}

} // baltam 命名空间
