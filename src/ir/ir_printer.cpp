#include "ir/ir_printer.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace baltam {
namespace {

std::string format_double(double value) {
    std::ostringstream oss;
    oss << value;
    return oss.str();
}

std::string format_complex(const std::complex<double>& value) {
    std::ostringstream oss;
    oss << value.real();
    if (value.imag() >= 0) {
        oss << "+";
    }
    oss << value.imag() << "i";
    return oss.str();
}

template <typename NumberValue>
std::string format_number(const NumberValue& value) {
    return std::visit(
        [](const auto& item) -> std::string {
            using T = std::decay_t<decltype(item)>;

            if constexpr (std::is_same_v<T, bool>) {
                return item ? "true" : "false";
            } else if constexpr (std::is_same_v<T, std::int64_t>) {
                return std::to_string(item);
            } else if constexpr (std::is_same_v<T, std::uint64_t>) {
                return std::to_string(item);
            } else if constexpr (std::is_same_v<T, double>) {
                return format_double(item);
            } else if constexpr (std::is_same_v<T, std::complex<double>>) {
                return format_complex(item);
            }

            return "<number>";
        },
        value);
}

std::string format_quoted_string(const std::string& text) {
    std::ostringstream oss;
    oss << "\"";
    for (const unsigned char ch : text) {
        switch (ch) {
            case '\\':
                oss << "\\\\";
                break;
            case '"':
                oss << "\\\"";
                break;
            case '\n':
                oss << "\\n";
                break;
            case '\r':
                oss << "\\r";
                break;
            case '\t':
                oss << "\\t";
                break;
            default:
                if (ch >= 0x80 || std::isprint(ch) != 0) {
                    oss << static_cast<char>(ch);
                } else {
                    oss << "\\x";
                    constexpr char hex[] = "0123456789ABCDEF";
                    oss << hex[(ch >> 4) & 0xF] << hex[ch & 0xF];
                }
                break;
        }
    }
    oss << "\"";
    return oss.str();
}

std::string format_named_value(const NamedValue& value) {
    return "%" + value.name;
}

const NonSSANode& require_non_ssa_node(const IRNode& node) {
    const auto* non_ssa = dynamic_cast<const NonSSANode*>(&node);
    if (non_ssa == nullptr) {
        throw std::runtime_error("print_ir 收到了非 `NonSSANode` 节点。");
    }
    return *non_ssa;
}

const UntypedSSANode& require_untyped_ssa_node(const IRNode& node) {
    const auto* untyped_ssa = dynamic_cast<const UntypedSSANode*>(&node);
    if (untyped_ssa == nullptr) {
        throw std::runtime_error("print_ir 收到了非 `UntypedSSANode` 节点。");
    }
    return *untyped_ssa;
}

const char* function_type_name(Function::Type type) {
    switch (type) {
        case Function::Script:
            return "script";
        case Function::PrimaryFunction:
            return "primary_function";
        case Function::LocalFunction:
            return "local_function";
    }

    return "unknown_function_type";
}

const char* module_type_name(Module::Type type) {
    switch (type) {
        case Module::M_Script:
            return "script";
        case Module::M_Function:
            return "function";
    }

    return "unknown_module_type";
}

const char* stage_name(IRNode::Stage stage) {
    switch (stage) {
        case IRNode::NonSSA:
            return "non-ssa";
        case IRNode::UntypedSSA:
            return "untyped-ssa";
        case IRNode::TypedSSA:
            return "typed-ssa";
    }

    return "unknown";
}

std::string module_stage_name(const Module& module) {
    bool has_function = false;
    IRNode::Stage stage = IRNode::NonSSA;
    for (const auto& function : module.functions()) {
        if (function == nullptr) {
            continue;
        }
        if (!has_function) {
            stage = function->stage();
            has_function = true;
            continue;
        }
        if (function->stage() != stage) {
            return "mixed";
        }
    }

    return has_function ? stage_name(stage) : "empty";
}

std::string format_block_ref(const BasicBlock* block) {
    return "%" + std::string(block != nullptr ? block->name() : "<null>");
}

std::string format_block_ref_list(const std::vector<BasicBlock*>& blocks) {
    std::ostringstream oss;
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        if (i != 0) {
            oss << ", ";
        }
        oss << format_block_ref(blocks[i]);
    }
    return oss.str();
}

std::string format_function_ref(const std::string& name) {
    return "@" + name;
}

std::string format_value_list(const std::vector<NamedValue>& values) {
    std::ostringstream oss;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            oss << ", ";
        }
        oss << format_named_value(values[i]);
    }
    return oss.str();
}

std::string format_argument_list(const std::vector<NamedValue>& values) {
    std::ostringstream oss;
    oss << "(";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            oss << ", ";
        }
        oss << format_named_value(values[i]);
    }
    oss << ")";
    return oss.str();
}

struct SSADisplayNames {
    std::unordered_map<ValueId, std::string> by_id;
};

std::string base_ssa_name(const Function& function, ValueId value_id,
                          const std::string& fallback_name = {}) {
    if (value_id == InvalidValueId) {
        return {};
    }

    std::string debug_name = fallback_name;
    if (const std::string* info = function.find_value_debug_name(value_id);
        info != nullptr && !info->empty()) {
        debug_name = *info;
    }

    return debug_name;
}

void append_ssa_definition(std::unordered_map<std::string, std::vector<ValueId>>& ids_by_name,
                           const std::string& name, ValueId value_id) {
    if (value_id == InvalidValueId || name.empty()) {
        return;
    }
    ids_by_name[name].push_back(value_id);
}

void collect_ssa_result_ids(const UntypedSSANode& node, std::vector<ValueId>& result_ids) {
    switch (node.type()) {
        case UntypedSSANode::SSA_Number:
            result_ids.push_back(static_cast<const SSANumberNode&>(node).result());
            return;
        case UntypedSSANode::SSA_Text:
            result_ids.push_back(static_cast<const SSATextNode&>(node).result());
            return;
        case UntypedSSANode::SSA_Undef:
            result_ids.push_back(static_cast<const SSAUndefNode&>(node).result());
            return;
        case UntypedSSANode::SSA_Phi:
            result_ids.push_back(static_cast<const SSAPhiNode&>(node).result());
            return;
        case UntypedSSANode::SSA_Copy:
            result_ids.push_back(static_cast<const SSACopyNode&>(node).result());
            return;
        case UntypedSSANode::SSA_UnaryOp:
            result_ids.push_back(static_cast<const SSAUnaryOpNode&>(node).result());
            return;
        case UntypedSSANode::SSA_BinOp:
            result_ids.push_back(static_cast<const SSABinOpNode&>(node).result());
            return;
        case UntypedSSANode::SSA_Call: {
            const auto& call = static_cast<const SSACallNode&>(node);
            result_ids.insert(result_ids.end(), call.results().begin(), call.results().end());
            return;
        }
        case UntypedSSANode::SSA_CondJump:
        case UntypedSSANode::SSA_Jump:
        case UntypedSSANode::SSA_Return:
            return;
    }
}

SSADisplayNames build_ssa_display_names(const Function& function) {
    SSADisplayNames result;
    std::unordered_map<std::string, std::vector<ValueId>> ids_by_name;

    const std::size_t argument_count =
        std::max(function.inputs().size(), function.argument_values().size());
    for (std::size_t i = 0; i < argument_count; ++i) {
        if (i >= function.argument_values().size()) {
            continue;
        }
        const std::string fallback =
            i < function.inputs().size() ? function.inputs()[i].name : std::string{};
        append_ssa_definition(ids_by_name, base_ssa_name(function, function.argument_values()[i], fallback),
                              function.argument_values()[i]);
    }

    std::vector<ValueId> result_ids;
    for (const auto& block : function.blocks()) {
        if (block == nullptr) {
            continue;
        }

        for (IRNode* node : block->phi_nodes()) {
            if (node == nullptr) {
                continue;
            }
            result_ids.clear();
            collect_ssa_result_ids(require_untyped_ssa_node(*node), result_ids);
            for (ValueId value_id : result_ids) {
                append_ssa_definition(ids_by_name, base_ssa_name(function, value_id), value_id);
            }
        }

        for (IRNode* node : block->instructions()) {
            if (node == nullptr) {
                continue;
            }
            result_ids.clear();
            collect_ssa_result_ids(require_untyped_ssa_node(*node), result_ids);
            for (ValueId value_id : result_ids) {
                append_ssa_definition(ids_by_name, base_ssa_name(function, value_id), value_id);
            }
        }
    }

    for (const auto& entry : ids_by_name) {
        const std::string& name = entry.first;
        const std::vector<ValueId>& ids = entry.second;
        if (ids.size() == 1) {
            result.by_id.emplace(ids.front(), "%" + name);
            continue;
        }

        for (std::size_t i = 0; i < ids.size(); ++i) {
            result.by_id.emplace(ids[i], "%" + name + "." + std::to_string(i + 1));
        }
    }

    return result;
}

std::string format_ssa_value(const Function& function, const SSADisplayNames& display_names,
                             ValueId value_id, const std::string& fallback_name = {}) {
    if (value_id == InvalidValueId) {
        return "%<invalid>";
    }

    auto it = display_names.by_id.find(value_id);
    if (it != display_names.by_id.end()) {
        return it->second;
    }

    const std::string debug_name = base_ssa_name(function, value_id, fallback_name);
    if (!debug_name.empty()) {
        return "%" + debug_name;
    }

    return "%" + std::to_string(value_id);
}

std::string format_ssa_value(const Function& function, const SSADisplayNames& display_names,
                             ValueRef value,
                             const std::string& fallback_name = {}) {
    return format_ssa_value(function, display_names, value.id, fallback_name);
}

std::string format_ssa_value_list(const Function& function, const SSADisplayNames& display_names,
                                  const std::vector<ValueRef>& values) {
    std::ostringstream oss;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            oss << ", ";
        }
        oss << format_ssa_value(function, display_names, values[i]);
    }
    return oss.str();
}

std::string format_ssa_result_list(const Function& function, const SSADisplayNames& display_names,
                                   const std::vector<ValueId>& values) {
    std::ostringstream oss;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            oss << ", ";
        }
        oss << format_ssa_value(function, display_names, values[i]);
    }
    return oss.str();
}

std::string format_ssa_argument_list(const Function& function, const SSADisplayNames& display_names) {
    std::ostringstream oss;
    oss << "(";

    const std::size_t count = std::max(function.inputs().size(), function.argument_values().size());
    for (std::size_t i = 0; i < count; ++i) {
        if (i != 0) {
            oss << ", ";
        }

        if (i < function.argument_values().size()) {
            const std::string fallback =
                i < function.inputs().size() ? function.inputs()[i].name : std::string{};
            oss << format_ssa_value(function, display_names, function.argument_values()[i], fallback);
        } else {
            oss << format_named_value(function.inputs()[i]);
        }
    }

    oss << ")";
    return oss.str();
}

std::string unary_opcode(UnaryOpNode::Op op) {
    switch (op) {
        case UnaryOpNode::Logic_Not:
            return "not";
        case UnaryOpNode::UPlus:
            return "uplus";
        case UnaryOpNode::UMinus:
            return "uminus";
        case UnaryOpNode::Transpose:
            return "transpose";
        case UnaryOpNode::CTranspose:
            return "ctranspose";
    }

    return "unknown.unaryop";
}

std::string binop_opcode(BinOpNode::Op op) {
    switch (op) {
        case BinOpNode::Add:
            return "plus";
        case BinOpNode::Subtract:
            return "minus";
        case BinOpNode::Eq:
            return "eq";
        case BinOpNode::Ge:
            return "ge";
        case BinOpNode::Gt:
            return "gt";
        case BinOpNode::Le:
            return "le";
        case BinOpNode::Lt:
            return "lt";
        case BinOpNode::Ne:
            return "ne";
        case BinOpNode::And:
            return "and";
        case BinOpNode::Or:
            return "or";
        case BinOpNode::Power:
            return "power";
        case BinOpNode::LDivide:
            return "ldivide";
        case BinOpNode::MLeftDivide:
            return "mldivide";
        case BinOpNode::MPower:
            return "mpower";
        case BinOpNode::MRightDivide:
            return "mrdivide";
        case BinOpNode::Times:
            return "times";
        case BinOpNode::Multiply:
            return "mtimes";
        case BinOpNode::RDivide:
            return "rdivide";
    }

    return "unknown.binop";
}

std::string expr_text(const NonSSANode& node) {
    switch (node.type()) {
        case NonSSANode::Number: {
            const auto& number = static_cast<const NumberNode&>(node);
            return "const " + format_number(number.value());
        }
        case NonSSANode::Text: {
            const auto& text = static_cast<const TextNode&>(node);
            return "const.text " + format_quoted_string(text.text());
        }
        case NonSSANode::Assign: {
            const auto& assign = static_cast<const AssignNode&>(node);
            return "copy " + format_named_value(assign.src());
        }
        case NonSSANode::UnaryOp: {
            const auto& unary = static_cast<const UnaryOpNode&>(node);
            return unary_opcode(unary.op()) + " " + format_named_value(unary.operand());
        }
        case NonSSANode::BinOp: {
            const auto& binop = static_cast<const BinOpNode&>(node);
            return binop_opcode(binop.op()) + " " + format_named_value(binop.lhs()) + ", " +
                   format_named_value(binop.rhs());
        }
        case NonSSANode::Call: {
            const auto& call = static_cast<const CallNode&>(node);
            std::ostringstream oss;
            oss << "call ";
            if (call.callee_type() == CallNode::Direct) {
                oss << format_function_ref(call.callee());
            } else {
                oss << "%" << call.callee();
            }
            oss << "(" << format_value_list(call.inputs()) << ")";
            return oss.str();
        }
        case NonSSANode::CondJump: {
            const auto& jump = static_cast<const CondJumpNode&>(node);
            return "br " + format_named_value(jump.cond()) + ", label " +
                   format_block_ref(jump.true_block()) + ", label " +
                   format_block_ref(jump.false_block());
        }
        case NonSSANode::Jump: {
            const auto& jump = static_cast<const JumpNode&>(node);
            return "br label " + format_block_ref(jump.target());
        }
        case NonSSANode::Return: {
            const auto& ret = static_cast<const ReturnNode&>(node);
            if (ret.values().empty()) {
                return "ret void";
            }
            return "ret " + format_value_list(ret.values());
        }
    }

    return "<node>";
}

std::string format_phi_incomings(const Function& function, const SSADisplayNames& display_names,
                                 const std::vector<SSAPhiNode::Incoming>& incomings) {
    std::ostringstream oss;
    for (std::size_t i = 0; i < incomings.size(); ++i) {
        if (i != 0) {
            oss << ", ";
        }
        oss << "[ " << format_ssa_value(function, display_names, incomings[i].value) << ", "
            << format_block_ref(incomings[i].predecessor) << " ]";
    }
    return oss.str();
}

std::string format_ssa_callee(const Function& function, const SSADisplayNames& display_names,
                              const SSACallNode::Callee& callee) {
    if (callee.type == SSACallNode::Callee::Direct) {
        return format_function_ref(callee.direct_symbol);
    }
    return format_ssa_value(function, display_names, callee.indirect_value);
}

std::string expr_text(const Function& function, const SSADisplayNames& display_names,
                      const UntypedSSANode& node) {
    switch (node.type()) {
        case UntypedSSANode::SSA_Number: {
            const auto& number = static_cast<const SSANumberNode&>(node);
            return "const " + format_number(number.value());
        }
        case UntypedSSANode::SSA_Text: {
            const auto& text = static_cast<const SSATextNode&>(node);
            return "const.text " + format_quoted_string(text.text());
        }
        case UntypedSSANode::SSA_Undef:
            return "undef";
        case UntypedSSANode::SSA_Phi: {
            const auto& phi = static_cast<const SSAPhiNode&>(node);
            return "phi " + format_phi_incomings(function, display_names, phi.incomings());
        }
        case UntypedSSANode::SSA_Copy: {
            const auto& copy = static_cast<const SSACopyNode&>(node);
            return "copy " + format_ssa_value(function, display_names, copy.src());
        }
        case UntypedSSANode::SSA_UnaryOp: {
            const auto& unary = static_cast<const SSAUnaryOpNode&>(node);
            return unary_opcode(unary.op()) + " " + format_ssa_value(function, display_names,
                                                                     unary.operand());
        }
        case UntypedSSANode::SSA_BinOp: {
            const auto& binop = static_cast<const SSABinOpNode&>(node);
            return binop_opcode(binop.op()) + " " +
                   format_ssa_value(function, display_names, binop.lhs()) + ", " +
                   format_ssa_value(function, display_names, binop.rhs());
        }
        case UntypedSSANode::SSA_Call: {
            const auto& call = static_cast<const SSACallNode&>(node);
            std::ostringstream oss;
            // 对 untyped SSA 来说，indirect `call %v(...)` 仍然保留运行时分派边界：
            // `%v` 既可能是 function_handle 调用，也可能是 runtime `paren get`。
            // 因此 printer 不能仅凭 indirect 形态就把它改写成 `paren_get`。
            oss << "call " << format_ssa_callee(function, display_names, call.callee()) << "("
                << format_ssa_value_list(function, display_names, call.inputs()) << ")";
            return oss.str();
        }
        case UntypedSSANode::SSA_CondJump: {
            const auto& jump = static_cast<const SSACondJumpNode&>(node);
            return "br " + format_ssa_value(function, display_names, jump.cond()) + ", label " +
                   format_block_ref(jump.true_block()) + ", label " +
                   format_block_ref(jump.false_block());
        }
        case UntypedSSANode::SSA_Jump: {
            const auto& jump = static_cast<const SSAJumpNode&>(node);
            return "br label " + format_block_ref(jump.target());
        }
        case UntypedSSANode::SSA_Return: {
            const auto& ret = static_cast<const SSAReturnNode&>(node);
            if (ret.values().empty()) {
                return "ret void";
            }
            return "ret " + format_ssa_value_list(function, display_names, ret.values());
        }
    }

    return "<ssa-node>";
}

std::string format_node_text(const Function& function, const SSADisplayNames& display_names,
                             const IRNode& node) {
    switch (function.stage()) {
        case IRNode::NonSSA: {
            const NonSSANode& non_ssa = require_non_ssa_node(node);
            std::string text;
            switch (non_ssa.type()) {
                case NonSSANode::Number:
                    text = format_named_value(static_cast<const NumberNode&>(non_ssa).result()) + " = ";
                    break;
                case NonSSANode::Text:
                    text = format_named_value(static_cast<const TextNode&>(non_ssa).result()) + " = ";
                    break;
                case NonSSANode::Assign:
                    text = format_named_value(static_cast<const AssignNode&>(non_ssa).dst()) + " = ";
                    break;
                case NonSSANode::UnaryOp:
                    text = format_named_value(static_cast<const UnaryOpNode&>(non_ssa).result()) + " = ";
                    break;
                case NonSSANode::BinOp:
                    text = format_named_value(static_cast<const BinOpNode&>(non_ssa).result()) + " = ";
                    break;
                case NonSSANode::Call: {
                    const auto& call = static_cast<const CallNode&>(non_ssa);
                    if (!call.outputs().empty()) {
                        text = format_value_list(call.outputs()) + " = ";
                    }
                    break;
                }
                case NonSSANode::CondJump:
                case NonSSANode::Jump:
                case NonSSANode::Return:
                    break;
            }

            text += expr_text(non_ssa);
            return text;
        }
        case IRNode::UntypedSSA: {
            const UntypedSSANode& ssa = require_untyped_ssa_node(node);
            std::string text;
            switch (ssa.type()) {
                case UntypedSSANode::SSA_Number:
                    text = format_ssa_value(function, display_names,
                                            static_cast<const SSANumberNode&>(ssa).result()) +
                           " = ";
                    break;
                case UntypedSSANode::SSA_Text:
                    text = format_ssa_value(function, display_names,
                                            static_cast<const SSATextNode&>(ssa).result()) +
                           " = ";
                    break;
                case UntypedSSANode::SSA_Undef:
                    text = format_ssa_value(function, display_names,
                                            static_cast<const SSAUndefNode&>(ssa).result()) +
                           " = ";
                    break;
                case UntypedSSANode::SSA_Phi:
                    text = format_ssa_value(function, display_names,
                                            static_cast<const SSAPhiNode&>(ssa).result()) +
                           " = ";
                    break;
                case UntypedSSANode::SSA_Copy:
                    text = format_ssa_value(function, display_names,
                                            static_cast<const SSACopyNode&>(ssa).result()) +
                           " = ";
                    break;
                case UntypedSSANode::SSA_UnaryOp:
                    text = format_ssa_value(function, display_names,
                                            static_cast<const SSAUnaryOpNode&>(ssa).result()) +
                           " = ";
                    break;
                case UntypedSSANode::SSA_BinOp:
                    text = format_ssa_value(function, display_names,
                                            static_cast<const SSABinOpNode&>(ssa).result()) +
                           " = ";
                    break;
                case UntypedSSANode::SSA_Call: {
                    const auto& call = static_cast<const SSACallNode&>(ssa);
                    if (!call.results().empty()) {
                        text = format_ssa_result_list(function, display_names, call.results()) + " = ";
                    }
                    break;
                }
                case UntypedSSANode::SSA_CondJump:
                case UntypedSSANode::SSA_Jump:
                case UntypedSSANode::SSA_Return:
                    break;
            }

            text += expr_text(function, display_names, ssa);
            return text;
        }
        case IRNode::TypedSSA:
            throw std::runtime_error("print_ir 当前尚未支持 `TypedSSA`。");
    }

    throw std::runtime_error("print_ir 遇到了未知的函数 stage。");
}

std::string trim_copy(std::string text) {
    const std::size_t begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return {};
    }

    const std::size_t end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

const std::vector<std::string>& source_file_lines(const std::string& filename) {
    static std::unordered_map<std::string, std::vector<std::string>> cache;

    const auto cached = cache.find(filename);
    if (cached != cache.end()) {
        return cached->second;
    }

    std::vector<std::string> lines;
    std::ifstream input(filename);
    std::string line;
    while (std::getline(input, line)) {
        lines.push_back(std::move(line));
    }

    return cache.emplace(filename, std::move(lines)).first->second;
}

std::string format_display_filename(const std::string& filename) {
    if (filename.empty()) {
        return {};
    }

    const std::filesystem::path path(filename);
    if (!path.is_absolute()) {
        return path.generic_string();
    }

    std::error_code error;
    const std::filesystem::path cwd = std::filesystem::current_path(error);
    if (!error) {
        const std::filesystem::path relative = path.lexically_relative(cwd);
        if (!relative.empty()) {
            const std::string text = relative.generic_string();
            if (!text.empty() && text.rfind("..", 0) != 0) {
                return text;
            }
        }
    }

    const std::filesystem::path basename = path.filename();
    return basename.empty() ? path.generic_string() : basename.generic_string();
}

struct SourceComment {
    std::string filename;
    int line = 0;
    std::string text;
};

std::optional<SourceComment> source_comment_from_location(
    const std::optional<SourceLocation>& location) {
    if (!location.has_value() || location->begin_line <= 0 || location->filename.empty()) {
        return std::nullopt;
    }

    const std::vector<std::string>& lines = source_file_lines(location->filename);
    const std::size_t line_index = static_cast<std::size_t>(location->begin_line - 1);
    if (line_index >= lines.size()) {
        return std::nullopt;
    }

    const std::string source_line = trim_copy(lines[line_index]);
    if (source_line.empty()) {
        return std::nullopt;
    }

    return SourceComment{format_display_filename(location->filename), location->begin_line, source_line};
}

std::size_t source_comment_column(const Function& function) {
    const SSADisplayNames display_names =
        function.stage() == IRNode::UntypedSSA ? build_ssa_display_names(function) : SSADisplayNames{};
    std::size_t max_width = 0;
    for (const auto& block : function.blocks()) {
        for (IRNode* node : block->phi_nodes()) {
            if (node != nullptr) {
                max_width = std::max(max_width, format_node_text(function, display_names, *node).size());
            }
        }
        for (IRNode* node : block->instructions()) {
            if (node != nullptr) {
                max_width = std::max(max_width, format_node_text(function, display_names, *node).size());
            }
        }
        if (block->terminal() != nullptr) {
            max_width = std::max(max_width,
                                 format_node_text(function, display_names, *block->terminal()).size());
        }
    }
    return max_width == 0 ? 0 : max_width + 2;
}

void print_node(std::ostream& os, const Function& function, const SSADisplayNames& display_names,
                const IRNode& node,
                std::size_t comment_column, std::optional<SourceComment>& last_comment) {
    const std::string text = format_node_text(function, display_names, node);
    const std::optional<SourceComment> source = source_comment_from_location(node.source_location());

    bool emit_comment = false;
    if (source.has_value()) {
        if (!last_comment.has_value() || last_comment->filename != source->filename ||
            last_comment->line != source->line) {
            emit_comment = true;
            last_comment = source;
        }
    }

    os << "  " << text;
    if (emit_comment && last_comment.has_value()) {
        if (comment_column > text.size()) {
            os << std::string(comment_column - text.size(), ' ');
        } else {
            os << "  ";
        }
        os << "; " << last_comment->filename << ":" << last_comment->line << "  "
           << last_comment->text;
    }
    os << "\n";
}

std::string format_function_arguments(const Function& function) {
    if (function.stage() == IRNode::UntypedSSA) {
        return format_ssa_argument_list(function, build_ssa_display_names(function));
    }
    return format_argument_list(function.inputs());
}

}  // namespace

void print_ir(std::ostream& os, const Module& module) {
    os << "; ModuleID = " << format_quoted_string(module.name()) << "\n";
    os << "source_filename = " << format_quoted_string(format_display_filename(module.source_path()))
       << "\n";
    os << "; stage = " << format_quoted_string(module_stage_name(module)) << "\n";
    os << "; module_type = " << format_quoted_string(module_type_name(module.type())) << "\n";
    os << "; entry = "
       << (module.entry_function() != nullptr ? format_function_ref(module.entry_function()->name())
                                              : "@<null>")
       << "\n";

    for (const auto& function : module.functions()) {
        os << "\ndefine " << function_type_name(function->type()) << " "
           << format_function_ref(function->name()) << format_function_arguments(*function)
           << " {\n";
        os << "  ; stage = " << format_quoted_string(stage_name(function->stage())) << "\n";
        os << "  ; outputs = " << format_argument_list(function->outputs()) << "\n";
        os << "  ; entry = "
           << (function->entry_block() != nullptr ? format_block_ref(function->entry_block())
                                                  : "%<null>")
           << "\n";

        const SSADisplayNames display_names =
            function->stage() == IRNode::UntypedSSA ? build_ssa_display_names(*function)
                                                    : SSADisplayNames{};
        const std::size_t comment_column = source_comment_column(*function);
        std::optional<SourceComment> last_comment;
        for (const auto& block : function->blocks()) {
            os << "\n" << block->name() << ":";
            const std::string preds = format_block_ref_list(block->predecessors());
            const std::string succs = format_block_ref_list(block->successors());
            if (!preds.empty() || !succs.empty()) {
                os << " ;";
                if (!preds.empty()) {
                    os << " preds = " << preds;
                }
                if (!succs.empty()) {
                    if (!preds.empty()) {
                        os << ";";
                    }
                    os << " succs = " << succs;
                }
            }
            os << "\n";

            for (IRNode* node : block->phi_nodes()) {
                if (node != nullptr) {
                    print_node(os, *function, display_names, *node, comment_column, last_comment);
                }
            }
            for (IRNode* node : block->instructions()) {
                if (node != nullptr) {
                    print_node(os, *function, display_names, *node, comment_column, last_comment);
                }
            }
            if (block->terminal() != nullptr) {
                print_node(os, *function, display_names, *block->terminal(), comment_column,
                           last_comment);
            }
        }

        os << "\n}\n";
    }
}

}  // namespace baltam
