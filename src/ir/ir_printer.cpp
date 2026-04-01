#include "ir/ir.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
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

std::string format_number(const NumberInstruction::NumberValue& value) {
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

std::string binop_opcode(BinOpInstruction::Type op) {
    switch (op) {
        case BinOpInstruction::Add:
            return "add";
        case BinOpInstruction::Subtract:
            return "sub";
        case BinOpInstruction::Eq:
            return "cmp.eq";
        case BinOpInstruction::Gt:
            return "cmp.gt";
        case BinOpInstruction::Lt:
            return "cmp.lt";
        case BinOpInstruction::Ne:
            return "cmp.ne";
        case BinOpInstruction::Or:
            return "or";
        case BinOpInstruction::MPower:
            return "pow";
        case BinOpInstruction::Multiply:
            return "mul";
    }

    return "unknown.binop";
}

std::string unaryop_opcode(UnaryOpInstruction::Type op) {
    switch (op) {
        case UnaryOpInstruction::Logic_Not:
            return "not";
        case UnaryOpInstruction::UMinus:
            return "neg";
    }

    return "unknown.unaryop";
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

using DisplayNameMap = std::unordered_map<ValueId, std::string>;

const DisplayNameMap* current_display_names = nullptr;

struct DisplayValueInfo {
    ValueId id = InvalidValueId;
    std::string debug_name;
    bool is_phi = false;
};

std::string format_value_ref(const Instruction* context, ValueRef ref);
std::string format_inst_values(const std::vector<InstValue>& values);
std::string format_argument_list(const std::vector<InstValue>& values);
std::string format_output_list(const std::vector<std::string>& names);
std::string format_block_ref_list(const std::vector<BasicBlock*>& blocks);
std::string format_function_ref(const std::string& name);
std::string format_block_ref(const BasicBlock* block);
std::string format_instruction_text(const Instruction& instruction);

struct SourceComment {
    std::string filename;
    int line = 0;
    std::string text;
};

std::optional<SourceComment> source_comment_from_location(
    const std::optional<SourceLocation>& location);

void collect_display_value_infos(std::vector<DisplayValueInfo>& infos,
                                 const std::vector<InstValue>& values, bool is_phi) {
    for (const InstValue& value : values) {
        if (!value.is_valid()) {
            continue;
        }
        infos.push_back(DisplayValueInfo{value.id, value.debug_name, is_phi});
    }
}

DisplayNameMap build_display_names(const Function& function) {
    std::vector<DisplayValueInfo> infos;
    collect_display_value_infos(infos, function.input_values(), false);

    for (const auto& block : function.blocks()) {
        for (Instruction* instruction : block->instructions()) {
            if (instruction == nullptr) {
                continue;
            }
            collect_display_value_infos(infos, instruction->value_defs(),
                                        instruction->type() == Instruction::Phi);
        }
        if (block->terminal() != nullptr) {
            collect_display_value_infos(infos, block->terminal()->value_defs(),
                                        block->terminal()->type() == Instruction::Phi);
        }
    }

    std::unordered_map<std::string, std::size_t> name_counts;
    std::unordered_map<std::string, std::size_t> first_indices;
    std::unordered_map<std::string, std::size_t> first_phi_indices;
    for (std::size_t i = 0; i < infos.size(); ++i) {
        if (infos[i].debug_name.empty()) {
            continue;
        }
        ++name_counts[infos[i].debug_name];
        first_indices.emplace(infos[i].debug_name, i);
        if (infos[i].is_phi) {
            first_phi_indices.emplace(infos[i].debug_name, i);
        }
    }

    std::unordered_map<std::string, std::size_t> canonical_indices;
    for (const auto& [name, count] : name_counts) {
        if (count <= 1) {
            continue;
        }
        const auto phi_it = first_phi_indices.find(name);
        canonical_indices[name] =
            phi_it != first_phi_indices.end() ? phi_it->second : first_indices.at(name);
    }

    DisplayNameMap display_names;
    std::unordered_map<std::string, std::size_t> next_versions;
    std::unordered_set<std::string> used_display_names;
    std::size_t next_anonymous_id = 1;

    for (std::size_t i = 0; i < infos.size(); ++i) {
        const DisplayValueInfo& info = infos[i];
        if (info.debug_name.empty()) {
            std::string display_name;
            do {
                display_name = "%" + std::to_string(next_anonymous_id++);
            } while (used_display_names.find(display_name) != used_display_names.end());
            display_names[info.id] = display_name;
            used_display_names.insert(display_name);
            continue;
        }

        const auto count_it = name_counts.find(info.debug_name);
        const bool has_conflict = count_it != name_counts.end() && count_it->second > 1;
        const auto canonical_it = canonical_indices.find(info.debug_name);
        if (!has_conflict || (canonical_it != canonical_indices.end() && canonical_it->second == i)) {
            const std::string display_name = "%" + info.debug_name;
            display_names[info.id] = display_name;
            used_display_names.insert(display_name);
            continue;
        }

        std::size_t& next_version = next_versions[info.debug_name];
        if (next_version == 0) {
            next_version = 1;
        }
        std::string display_name;
        do {
            display_name = "%" + info.debug_name + "." + std::to_string(next_version++);
        } while (used_display_names.find(display_name) != used_display_names.end());
        display_names[info.id] = display_name;
        used_display_names.insert(display_name);
    }

    return display_names;
}

std::string expr_text(const Instruction* instruction) {
    if (instruction == nullptr) {
        return "<null>";
    }

    switch (instruction->type()) {
        case Instruction::Text:
            return "const.text " +
                   format_quoted_string(static_cast<const TextInstruction*>(instruction)->text());
        case Instruction::Binding:
            return "load.binding " +
                   format_quoted_string(static_cast<const BindingInstruction*>(instruction)->name());
        case Instruction::Number: {
            const auto* number = static_cast<const NumberInstruction*>(instruction);
            return "const " + format_number(number->value());
        }
        case Instruction::Undef:
            return "undef";
        case Instruction::UnaryOp: {
            const auto* unaryop = static_cast<const UnaryOpInstruction*>(instruction);
            const ValueRef operand_ref = unaryop->operand_ref();
            return unaryop_opcode(unaryop->op()) + " " +
                   (operand_ref.is_valid() ? format_value_ref(instruction, operand_ref) : "<null>");
        }
        case Instruction::BinOp: {
            const auto* binop = static_cast<const BinOpInstruction*>(instruction);
            const ValueRef lhs_ref = binop->lhs_ref();
            const ValueRef rhs_ref = binop->rhs_ref();
            const std::string lhs_text =
                lhs_ref.is_valid() ? format_value_ref(instruction, lhs_ref) : "<null>";
            const std::string rhs_text =
                rhs_ref.is_valid() ? format_value_ref(instruction, rhs_ref) : "<null>";
            return binop_opcode(binop->op()) + " " + lhs_text + ", " + rhs_text;
        }
        case Instruction::Phi: {
            const auto* phi = static_cast<const PhiInstruction*>(instruction);
            std::string text = "phi ";
            for (std::size_t i = 0; i < phi->incoming_count(); ++i) {
                if (i != 0) {
                    text += ", ";
                }
                const PhiInstruction::Incoming* incoming = phi->incoming(i);
                if (incoming == nullptr) {
                    text += "[ <null>, %<null> ]";
                    continue;
                }
                text += "[ ";
                text += incoming->value_ref.is_valid()
                            ? format_value_ref(instruction, incoming->value_ref)
                            : "<null>";
                text += ", ";
                text += format_block_ref(incoming->predecessor);
                text += " ]";
            }
            return text;
        }
        case Instruction::Asgn: {
            const auto* asgn = static_cast<const AssignInstruction*>(instruction);
            const ValueRef value_ref = asgn->value_ref();
            return "store.binding " +
                   (value_ref.is_valid() ? format_value_ref(instruction, value_ref) : "<null>") +
                   ", " + format_quoted_string(asgn->name());
        }
        case Instruction::Call: {
            const auto* call = static_cast<const CallInstruction*>(instruction);
            std::string text = "call ";
            if (call->is_indirect()) {
                text += format_value_ref(instruction, call->callee_ref());
            } else {
                text += format_function_ref(call->name());
            }
            text += "(";
            const std::size_t in_arg_count = call->input_count();
            for (std::size_t i = 0; i < in_arg_count; ++i) {
                if (i != 0) {
                    text += ", ";
                }
                const ValueRef in_arg_ref = call->input_ref(i);
                text += in_arg_ref.is_valid() ? format_value_ref(instruction, in_arg_ref) : "<null>";
            }
            text += ")";
            return text;
        }
        case Instruction::CondJump: {
            const auto* cond_jump = static_cast<const CondJumpInstruction*>(instruction);
            const ValueRef cond_ref = cond_jump->cond_ref();
            return "br " +
                   (cond_ref.is_valid() ? format_value_ref(instruction, cond_ref) : "<null>") +
                   ", label " + format_block_ref(cond_jump->true_block()) + ", label " +
                   format_block_ref(cond_jump->false_block());
        }
        case Instruction::Jump: {
            const auto* jump = static_cast<const JumpInstruction*>(instruction);
            return "br label " + format_block_ref(jump->target());
        }
        case Instruction::Return: {
            const auto* ret = static_cast<const ReturnInstruction*>(instruction);
            std::string text = "ret";
            const std::size_t value_count = ret->return_value_count();
            if (value_count == 0) {
                text += " void";
                return text;
            }
            text += " ";
            for (std::size_t i = 0; i < value_count; ++i) {
                if (i != 0) {
                    text += ", ";
                }
                const ValueRef value_ref = ret->return_value_ref(i);
                text += value_ref.is_valid() ? format_value_ref(instruction, value_ref) : "<null>";
            }
            return text;
        }
    }

    return "<inst>";
}

std::string format_inst_value(const InstValue& value) {
    if (current_display_names != nullptr) {
        const auto it = current_display_names->find(value.id);
        if (it != current_display_names->end()) {
            return it->second;
        }
    }
    if (!value.debug_name.empty()) {
        return "%" + value.debug_name;
    }
    return "%" + std::to_string(value.id);
}

const Function* parent_function_from_instruction(const Instruction* instruction) {
    if (instruction == nullptr || instruction->parent() == nullptr) {
        return nullptr;
    }
    return instruction->parent()->parent();
}

std::string format_value_ref(const Instruction* context, ValueRef ref) {
    if (!ref.is_valid()) {
        return "<invalid>";
    }
    if (current_display_names != nullptr) {
        const auto it = current_display_names->find(ref.id);
        if (it != current_display_names->end()) {
            return it->second;
        }
    }
    if (const Function* function = parent_function_from_instruction(context)) {
        if (const InstValue* value = function->find_value(ref.id)) {
            return format_inst_value(*value);
        }
    }
    return "%" + std::to_string(ref.id);
}

std::string format_inst_values(const std::vector<InstValue>& values) {
    std::ostringstream oss;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            oss << ", ";
        }
        oss << format_inst_value(values[i]);
    }
    return oss.str();
}

std::string format_argument_list(const std::vector<InstValue>& values) {
    std::ostringstream oss;
    oss << "(";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            oss << ", ";
        }
        oss << format_inst_value(values[i]);
    }
    oss << ")";
    return oss.str();
}

std::string format_output_list(const std::vector<std::string>& names) {
    std::ostringstream oss;
    oss << "(";
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (i != 0) {
            oss << ", ";
        }
        oss << names[i];
    }
    oss << ")";
    return oss.str();
}

std::string format_function_ref(const std::string& name) {
    return "@" + name;
}

std::string format_block_ref(const BasicBlock* block) {
    return "%" + std::string(block != nullptr ? block->name() : "<null>");
}

std::string format_block_ref_list(const std::vector<BasicBlock*>& blocks) {
    if (blocks.empty()) {
        return {};
    }

    std::ostringstream oss;
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        if (i != 0) {
            oss << ", ";
        }
        oss << format_block_ref(blocks[i]);
    }
    return oss.str();
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
            const std::string relative_text = relative.generic_string();
            if (!relative_text.empty() && relative_text.rfind("..", 0) != 0) {
                return relative_text;
            }
        }
    }

    const std::filesystem::path basename = path.filename();
    return basename.empty() ? path.generic_string() : basename.generic_string();
}

std::string format_instruction_text(const Instruction& instruction) {
    std::string text;
    if (instruction.has_values()) {
        text += format_inst_values(instruction.value_defs());
        text += " = ";
    }
    text += expr_text(&instruction);
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

std::optional<SourceComment> source_comment_from_location(
    const std::optional<SourceLocation>& location) {
    if (!location.has_value()) {
        return std::nullopt;
    }

    if (location->begin_line <= 0 || location->filename.empty()) {
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

    return SourceComment{location->filename, location->begin_line, source_line};
}

std::size_t source_comment_column(const Function& function) {
    std::size_t max_width = 0;
    for (const auto& block : function.blocks()) {
        for (Instruction* instruction : block->instructions()) {
            if (instruction == nullptr) {
                continue;
            }
            max_width = std::max(max_width, format_instruction_text(*instruction).size());
        }
        if (block->terminal() != nullptr) {
            max_width = std::max(max_width, format_instruction_text(*block->terminal()).size());
        }
    }
    return max_width == 0 ? 0 : max_width + 2;
}

void print_instruction(std::ostream& os, const Instruction& instruction, std::size_t comment_column,
                       std::optional<SourceComment>& last_source_comment) {
    std::optional<SourceComment> current_source_comment =
        source_comment_from_location(instruction.source_location());
    bool emit_source_comment = false;
    if (current_source_comment.has_value()) {
        if (!last_source_comment.has_value() ||
            last_source_comment->filename != current_source_comment->filename ||
            last_source_comment->line != current_source_comment->line) {
            emit_source_comment = true;
            last_source_comment = current_source_comment;
        }
    }

    os << "  " << format_instruction_text(instruction);
    if (emit_source_comment && last_source_comment.has_value()) {
        const std::string text = format_instruction_text(instruction);
        if (comment_column > text.size()) {
            os << std::string(comment_column - text.size(), ' ');
        } else {
            os << "  ";
        }
        os << "; " << last_source_comment->text;
    }
    os << "\n";
}

}  // namespace

void print_ir(std::ostream& os, const Module& module) {
    os << "; ModuleID = " << format_quoted_string(module.name()) << "\n";
    os << "source_filename = " << format_quoted_string(format_display_filename(module.source_path()))
       << "\n";
    os << "; module_type = " << format_quoted_string(module_type_name(module.type())) << "\n";
    os << "; entry = "
       << (module.entry_function() != nullptr ? format_function_ref(module.entry_function()->name())
                                              : "@<null>")
       << "\n";

    for (const auto& function : module.functions()) {
        const DisplayNameMap display_names = build_display_names(*function);
        current_display_names = &display_names;
        os << "\ndefine " << format_function_ref(function->name())
           << format_argument_list(function->input_values()) << " {\n";
        os << "  ; kind = " << function_type_name(function->type()) << "\n";
        os << "  ; outputs = " << format_output_list(function->output_names()) << "\n";
        os << "  ; entry = "
           << (function->entry_block() != nullptr ? format_block_ref(function->entry_block())
                                                  : "%<null>")
           << "\n";
        const std::size_t comment_column = source_comment_column(*function);
        std::optional<SourceComment> last_source_comment;
        for (const auto& block : function->blocks()) {
            os << "\n";
            os << block->name() << ":";
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
            for (Instruction* instruction : block->instructions()) {
                if (instruction != nullptr) {
                    print_instruction(os, *instruction, comment_column, last_source_comment);
                }
            }
            if (block->terminal() != nullptr) {
                print_instruction(os, *block->terminal(), comment_column, last_source_comment);
            }
        }

        os << "\n";
        os << "}\n";
        current_display_names = nullptr;
    }
}

}  // namespace baltam
