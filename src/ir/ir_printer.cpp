#include "ir/ir_printer.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
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

std::string format_number(const NumberNode::NumberValue& value) {
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

std::string unary_opcode(UnaryOpNode::Op op) {
    switch (op) {
        case UnaryOpNode::Logic_Not:
            return "not";
        case UnaryOpNode::UMinus:
            return "neg";
    }

    return "unknown.unaryop";
}

std::string binop_opcode(BinOpNode::Op op) {
    switch (op) {
        case BinOpNode::Add:
            return "add";
        case BinOpNode::Subtract:
            return "sub";
        case BinOpNode::Eq:
            return "icmp eq";
        case BinOpNode::Gt:
            return "icmp sgt";
        case BinOpNode::Lt:
            return "icmp slt";
        case BinOpNode::Ne:
            return "icmp ne";
        case BinOpNode::Or:
            return "or";
        case BinOpNode::MPower:
            return "pow";
        case BinOpNode::Multiply:
            return "mul";
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

std::string format_node_text(const NonSSANode& node) {
    std::string text;
    switch (node.type()) {
        case NonSSANode::Number:
            text = format_named_value(static_cast<const NumberNode&>(node).result()) + " = ";
            break;
        case NonSSANode::Text:
            text = format_named_value(static_cast<const TextNode&>(node).result()) + " = ";
            break;
        case NonSSANode::Assign:
            text = format_named_value(static_cast<const AssignNode&>(node).dst()) + " = ";
            break;
        case NonSSANode::UnaryOp:
            text = format_named_value(static_cast<const UnaryOpNode&>(node).result()) + " = ";
            break;
        case NonSSANode::BinOp:
            text = format_named_value(static_cast<const BinOpNode&>(node).result()) + " = ";
            break;
        case NonSSANode::Call: {
            const auto& call = static_cast<const CallNode&>(node);
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

    text += expr_text(node);
    return text;
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
    std::size_t max_width = 0;
    for (const auto& block : function.blocks()) {
        for (NonSSANode* node : block->instructions()) {
            if (node != nullptr) {
                max_width = std::max(max_width, format_node_text(*node).size());
            }
        }
        if (block->terminal() != nullptr) {
            max_width = std::max(max_width, format_node_text(*block->terminal()).size());
        }
    }
    return max_width == 0 ? 0 : max_width + 2;
}

void print_node(std::ostream& os, const NonSSANode& node, std::size_t comment_column,
                std::optional<SourceComment>& last_comment) {
    const std::string text = format_node_text(node);
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

}  // namespace

void print_ir(std::ostream& os, const Module& module) {
    os << "; ModuleID = " << format_quoted_string(module.name()) << "\n";
    os << "source_filename = " << format_quoted_string(format_display_filename(module.source_path()))
       << "\n";
    os << "; stage = \"non-ssa\"\n";
    os << "; module_type = " << format_quoted_string(module_type_name(module.type())) << "\n";
    os << "; entry = "
       << (module.entry_function() != nullptr ? format_function_ref(module.entry_function()->name())
                                              : "@<null>")
       << "\n";

    for (const auto& function : module.functions()) {
        os << "\ndefine " << function_type_name(function->type()) << " "
           << format_function_ref(function->name()) << format_argument_list(function->inputs())
           << " {\n";
        os << "  ; outputs = " << format_argument_list(function->outputs()) << "\n";
        os << "  ; entry = "
           << (function->entry_block() != nullptr ? format_block_ref(function->entry_block())
                                                  : "%<null>")
           << "\n";

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

            for (NonSSANode* node : block->instructions()) {
                if (node != nullptr) {
                    print_node(os, *node, comment_column, last_comment);
                }
            }
            if (block->terminal() != nullptr) {
                print_node(os, *block->terminal(), comment_column, last_comment);
            }
        }

        os << "\n}\n";
    }
}

}  // namespace baltam
