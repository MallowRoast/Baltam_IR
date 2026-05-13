#include "ir/ir_print.h"

#include "ir/ir_units.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <optional>
#include <sstream>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace baltam {
namespace {

struct IRRenderedLine {
    std::string text;
    std::string source_comment;
};

std::string escape_text(std::string_view text, char quote = '"') {
    std::string escaped;
    escaped.reserve(text.size());

    for (char ch : text) {
        switch (ch) {
            case '\\':
                escaped += "\\\\";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                if (ch == quote) {
                    escaped.push_back('\\');
                }
                escaped.push_back(ch);
                break;
        }
    }

    return escaped;
}

bool is_simple_identifier(std::string_view text) {
    if (text.empty()) {
        return false;
    }

    auto is_head = [](unsigned char ch) {
        return std::isalpha(ch) != 0 || ch == '_' || ch == '.';
    };
    auto is_tail = [](unsigned char ch) {
        return std::isalnum(ch) != 0 || ch == '_' || ch == '.';
    };

    if (!is_head(static_cast<unsigned char>(text.front()))) {
        return false;
    }

    for (char ch : text.substr(1)) {
        if (!is_tail(static_cast<unsigned char>(ch))) {
            return false;
        }
    }

    return true;
}

std::string format_symbol(std::string_view text) {
    if (is_simple_identifier(text)) {
        return '@' + std::string(text);
    }
    return "@\"" + escape_text(text) + '"';
}

std::string format_value_id(ValueId value_id) {
    if (!value_id.is_valid()) {
        return "%<invalid>";
    }
    return '%' + std::to_string(value_id.value());
}

std::string format_slot_id(SlotId slot_id) {
    if (!slot_id.is_valid()) {
        return "%slot<invalid>";
    }
    return "%slot" + std::to_string(slot_id.value());
}

std::string format_anonymous_function_id(AnonymousFunctionId function_id) {
    if (!function_id.is_valid()) {
        return "#anon<invalid>";
    }
    return "#anon" + std::to_string(function_id.value());
}

std::string format_constant(const Constant& constant) {
    return std::visit(
        [](const auto& value) -> std::string {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, LogicalConstant>) {
                return value.value ? "true" : "false";
            } else if constexpr (std::is_same_v<T, Int64Constant>) {
                return std::to_string(value.value);
            } else if constexpr (std::is_same_v<T, UInt64Constant>) {
                return std::to_string(value.value);
            } else if constexpr (std::is_same_v<T, Float64Constant>) {
                std::ostringstream os;
                os << value.value;
                return os.str();
            } else if constexpr (std::is_same_v<T, Complex128Constant>) {
                std::ostringstream os;
                os << '(' << value.re << ", " << value.im << ')';
                return os.str();
            } else if constexpr (std::is_same_v<T, CharLiteralConstant>) {
                return '\'' + escape_text(value.value, '\'') + '\'';
            } else if constexpr (std::is_same_v<T, StringLiteralConstant>) {
                return '"' + escape_text(value.value) + '"';
            } else {
                return "[]";
            }
        },
        constant);
}

const char* unary_mnemonic(UnaryOp op) noexcept {
    switch (op) {
        case Uplus:
            return "uplus";
        case Uminus:
            return "neg";
        case LogicalNot:
            return "not";
        case Transpose:
            return "transpose";
        case Ctranspose:
            return "ctranspose";
    }
    return "unary";
}

const char* binary_mnemonic(BinaryOp op) noexcept {
    switch (op) {
        case Add:
            return "add";
        case Sub:
            return "sub";
        case Mul:
            return "mul";
        case Rdiv:
            return "rdiv";
        case Ldiv:
            return "ldiv";
        case Pow:
            return "pow";
        case ElemMul:
            return "elem.mul";
        case ElemRdiv:
            return "elem.rdiv";
        case ElemLdiv:
            return "elem.ldiv";
        case ElemPow:
            return "elem.pow";
        case And:
            return "and";
        case Or:
            return "or";
        case Lt:
            return "cmp.lt";
        case Le:
            return "cmp.le";
        case Gt:
            return "cmp.gt";
        case Ge:
            return "cmp.ge";
        case Eq:
            return "cmp.eq";
        case Ne:
            return "cmp.ne";
    }
    return "binary";
}

const char* internal_binary_mnemonic(BinaryOp op) noexcept {
    switch (op) {
        case Add:
            return "internal.add";
        case Gt:
            return "internal.cmp_gt";
        case Or:
            return "internal.or";
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
    return "internal";
}

const char* call_mnemonic(const CallInst& inst) noexcept {
    switch (inst.dispatch_type) {
        case Dynamic:
            return "call";
        case Builtin:
            return "call builtin";
        case Internal:
            return "call";
        case MFunction:
            return "call mfunc";
    }
    return "call";
}

const char* function_handle_dispatch_name(DispatchType dispatch_type) noexcept {
    switch (dispatch_type) {
        case Dynamic:
            return "dynamic";
        case Builtin:
            return "builtin";
        case Internal:
            return "internal";
        case MFunction:
            return "mfunc";
    }
    return "dynamic";
}

const char* slot_type_name(Slot::Type type) noexcept {
    switch (type) {
        case Slot::Arg:
            return "arg";
        case Slot::Local:
            return "local";
        case Slot::InternalLocal:
            return "internal_local";
        case Slot::Capture:
            return "capture";
        case Slot::Ret:
            return "ret";
        case Slot::Hidden:
            return "hidden";
    }
    return "slot";
}

const char* hidden_role_name(SlotAttrs::HiddenRole role) noexcept {
    switch (role) {
        case SlotAttrs::None:
            return "none";
        case SlotAttrs::Nargin:
            return "nargin";
        case SlotAttrs::Nargout:
            return "nargout";
        case SlotAttrs::Varargin:
            return "varargin";
        case SlotAttrs::Varargout:
            return "varargout";
        case SlotAttrs::WorkspaceHandle:
            return "env";
    }
    return "hidden";
}

const char* fixed_slot_type_name(SlotAttrs::FixedType fixed_type) noexcept {
    switch (fixed_type) {
        case SlotAttrs::Unknown:
            return "";
        case SlotAttrs::Int64Scalar:
            return "int64";
    }
    return "";
}

std::string trim_ascii_spaces(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }

    std::size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }

    return std::string(text.substr(begin, end - begin));
}

std::string collapse_source_excerpt(std::string_view text) {
    std::string collapsed;
    collapsed.reserve(text.size());

    bool need_space = false;
    for (char ch : text) {
        if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
            need_space = !collapsed.empty();
            continue;
        }

        if (need_space) {
            collapsed.push_back(' ');
            need_space = false;
        }

        collapsed.push_back(ch);
    }

    return trim_ascii_spaces(collapsed);
}

std::string try_read_text_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }

    std::ostringstream os;
    os << input.rdbuf();
    return os.str();
}

class IRPrinter final {
public:
    IRPrinter(const IRPrintOptions& options, const MFileUnit& owner)
        : options_(options) {
        if (!options_.source_text.empty()) {
            source_storage_.assign(options_.source_text.data(), options_.source_text.size());
            source_text_ = source_storage_;
        } else if (options_.load_source_from_path) {
            source_storage_ = try_read_text_file(owner.path);
            source_text_ = source_storage_;
        }

        build_source_line_offsets();
    }

    void render_file(const MFileUnit& mfile) {
        if (options_.print_file_header) {
            lines_.push_back({"; mfile \"" + escape_text(mfile.path.string()) + '"', {}});
            if (!mfile.code_units.empty()) {
                lines_.push_back({});
            }
        }

        for (std::size_t i = 0; i < mfile.code_units.size(); ++i) {
            const CodeUnit* unit = mfile.code_units[i].get();
            if (unit != nullptr) {
                render_unit(*unit);
            }

            if (i + 1U < mfile.code_units.size()) {
                lines_.push_back({});
            }
        }

        if (!mfile.anonymous_functions.empty()) {
            if (!mfile.code_units.empty()) {
                lines_.push_back({});
            }

            for (std::size_t i = 0; i < mfile.anonymous_functions.functions.size(); ++i) {
                const AnonymousFunctionUnit* unit = mfile.anonymous_functions.functions[i].get();
                if (unit != nullptr) {
                    render_unit(*unit);
                }

                if (i + 1U < mfile.anonymous_functions.functions.size()) {
                    lines_.push_back({});
                }
            }
        }
    }

    [[nodiscard]] std::string str() const {
        std::ostringstream os;
        print(os);
        return os.str();
    }

    void print(std::ostream& os) const {
        std::size_t comment_column = options_.min_comment_column;
        for (const IRRenderedLine& line : lines_) {
            if (!line.source_comment.empty()) {
                comment_column = std::max(comment_column, line.text.size() + 2U);
            }
        }

        for (std::size_t i = 0; i < lines_.size(); ++i) {
            const IRRenderedLine& line = lines_[i];
            os << line.text;
            if (!line.source_comment.empty()) {
                if (line.text.size() < comment_column) {
                    os << std::string(comment_column - line.text.size(), ' ');
                } else {
                    os << ' ';
                }
                os << "; " << line.source_comment;
            }
            if (i + 1U < lines_.size()) {
                os << '\n';
            }
        }
    }

private:
    void render_unit(const CodeUnit& unit) {
        assign_slot_refs(unit.slot_table);

        if (unit.is_function()) {
            const auto& function = static_cast<const FunctionUnit&>(unit);
            std::string header = "define " + format_symbol(function.name) + '(';
            header += join_signature_slots(function.slot_table, function.param_slots);
            header += ')';
            if (!function.return_slots.empty()) {
                header += " -> (";
                header += join_signature_slots(function.slot_table, function.return_slots);
                header += ')';
            }
            header += " {";
            emit_raw(std::move(header));
        } else if (unit.is_anonymous_function()) {
            const auto& function = static_cast<const AnonymousFunctionUnit&>(unit);
            std::string header = "anon ";
            header += format_anonymous_function_id(function.id);
            header += '(';
            header += join_signature_slots(function.slot_table, function.param_slots);
            header += ')';
            if (!function.capture_slots.empty()) {
                header += " captures (";
                header += join_signature_slots(function.slot_table, function.capture_slots);
                header += ')';
            }
            header += " {";
            emit_raw(std::move(header));
        } else {
            emit_raw("script " + format_symbol(unit.name) + " {");
        }

        if (options_.print_slot_table && !unit.slot_table.empty()) {
            emit_raw("  ; slots:");
            for (const Slot& slot : unit.slot_table.slots) {
                emit_raw("  " + format_slot_decl(slot));
            }
            if (!unit.basic_blocks.empty()) {
                emit_raw({});
            }
        }

        assign_block_labels(unit);
        const std::vector<const BasicBlock*> blocks = layout_blocks(unit);
        for (std::size_t i = 0; i < blocks.size(); ++i) {
            const BasicBlock* block = blocks[i];
            if (block == nullptr) {
                continue;
            }

            emit_raw(block_labels_[block] + ':');
            for (const std::unique_ptr<Instruction>& instruction : block->instructions) {
                if (instruction != nullptr) {
                    emit_instruction(unit, *instruction);
                }
            }

            if (i + 1U < blocks.size()) {
                emit_raw({});
            }
        }

        emit_raw("}");
    }

    [[nodiscard]] std::string join_signature_slots(
        const SlotTable& slot_table,
        const std::vector<SlotId>& slot_ids) const {
        std::string text;
        for (std::size_t i = 0; i < slot_ids.size(); ++i) {
            const SlotId slot_id = slot_ids[i];
            if (i != 0) {
                text += ", ";
            }

            text += format_slot_ref(slot_id);
            if (const Slot* slot = slot_table.find_slot(slot_id);
                slot != nullptr && !slot->name.empty()) {
                text += " " + format_symbol(slot->name);
            }
        }
        return text;
    }

    void emit_raw(std::string text) {
        lines_.push_back({std::move(text), {}});
    }

    void emit_instruction(const CodeUnit& unit, const Instruction& instruction) {
        IRRenderedLine line;
        line.text = "  " + format_instruction(unit, instruction);
        line.source_comment = format_instruction_source_comment(instruction);
        lines_.push_back(std::move(line));
    }

    [[nodiscard]] std::vector<const BasicBlock*> layout_blocks(const CodeUnit& unit) const {
        std::vector<const BasicBlock*> ordered;
        std::unordered_set<const BasicBlock*> visited;
        std::vector<const BasicBlock*> deferred;
        std::vector<const BasicBlock*> deferred_returns;

        const auto visit = [&](const BasicBlock* start, const auto& visit_ref) -> void {
            if (start == nullptr || !visited.insert(start).second) {
                return;
            }

            ordered.push_back(start);
            for (const BasicBlock* successor : layout_successors(*start)) {
                if (should_defer_layout_successor(*start, successor)) {
                    if (is_return_block(successor)) {
                        deferred_returns.push_back(successor);
                    } else {
                        deferred.push_back(successor);
                    }
                    continue;
                }
                visit_ref(successor, visit_ref);
            }
        };

        visit(unit.entry_block, visit);

        for (std::size_t i = 0; i < deferred.size(); ++i) {
            visit(deferred[i], visit);
        }

        for (std::size_t i = 0; i < deferred_returns.size(); ++i) {
            visit(deferred_returns[i], visit);
        }

        for (const auto& block_ptr : unit.basic_blocks) {
            visit(block_ptr.get(), visit);
        }

        return ordered;
    }

    [[nodiscard]] std::vector<const BasicBlock*> layout_successors(
        const BasicBlock& block) const {
        const Instruction* terminator = block.terminator();
        if (terminator == nullptr) {
            return {};
        }

        switch (terminator->type()) {
            case Instruction::Goto: {
                const auto& go = static_cast<const GotoInst&>(*terminator);
                return {go.target};
            }
            case Instruction::Branch: {
                const auto& branch = static_cast<const BranchInst&>(*terminator);
                if (is_loop_exit_block(branch.true_target) &&
                    !is_loop_exit_block(branch.false_target)) {
                    return unique_layout_successors({branch.false_target, branch.true_target});
                }
                return unique_layout_successors({branch.true_target, branch.false_target});
            }
            case Instruction::Return:
            default:
                return {};
        }
    }

    [[nodiscard]] std::vector<const BasicBlock*> unique_layout_successors(
        std::initializer_list<const BasicBlock*> successors) const {
        std::vector<const BasicBlock*> unique;
        for (const BasicBlock* successor : successors) {
            if (successor == nullptr) {
                continue;
            }
            if (std::find(unique.begin(), unique.end(), successor) == unique.end()) {
                unique.push_back(successor);
            }
        }
        return unique;
    }

    [[nodiscard]] bool should_defer_layout_successor(
        const BasicBlock& block,
        const BasicBlock* successor) const {
        if (successor == nullptr) {
            return false;
        }

        if (is_loop_latch_block(successor) &&
            !is_loop_latch_block(&block)) {
            return true;
        }

        if (is_loop_exit_block(successor)) {
            return true;
        }

        return false;
    }

    [[nodiscard]] bool is_loop_latch_block(const BasicBlock* block) const {
        return block != nullptr && block->label == "for.latch";
    }

    [[nodiscard]] bool is_loop_exit_block(const BasicBlock* block) const {
        return block != nullptr && block->label == "for.end";
    }

    [[nodiscard]] bool is_return_block(const BasicBlock* block) const {
        return block != nullptr &&
            block->terminator() != nullptr &&
            block->terminator()->type() == Instruction::Return;
    }

    [[nodiscard]] std::string format_source_comment(SourceSpan source_span) const {
        if (!options_.print_source_comments ||
            !source_span.is_valid() ||
            source_text_.empty() ||
            source_span.end_offset > source_text_.size()) {
            return {};
        }

        std::string excerpt = collapse_source_excerpt(
            source_text_.substr(
                source_span.begin_offset,
                source_span.end_offset - source_span.begin_offset));
        if (excerpt.empty()) {
            return {};
        }

        if (!options_.print_source_line_numbers) {
            return excerpt;
        }

        const std::string line_tag = format_source_line_tag(source_span);
        if (line_tag.empty()) {
            return excerpt;
        }

        return line_tag + ": " + excerpt;
    }

    [[nodiscard]] bool should_print_instruction_source_comment(
        const Instruction& instruction) const {
        switch (instruction.type()) {
            case Instruction::StoreSlot:
            case Instruction::StoreWorkspace:
            case Instruction::Branch:
                return true;
            case Instruction::Goto:
                return is_explicit_loop_control_goto(instruction);
            case Instruction::Return:
                return instruction.source_span.is_valid();
            case Instruction::Apply: {
                const auto& inst = static_cast<const ApplyInst&>(instruction);
                return inst.results.empty();
            }
            case Instruction::ValueApply: {
                const auto& inst = static_cast<const ValueApplyInst&>(instruction);
                return inst.results.empty();
            }
            case Instruction::Call: {
                const auto& inst = static_cast<const CallInst&>(instruction);
                return inst.results.empty();
            }
            default:
                return false;
        }
    }

    [[nodiscard]] std::string format_instruction_source_comment(
        const Instruction& instruction) const {
        if (!should_print_instruction_source_comment(instruction)) {
            return {};
        }
        return format_source_comment(instruction.source_span);
    }

    [[nodiscard]] bool is_explicit_loop_control_goto(
        const Instruction& instruction) const {
        if (instruction.attrs.is_synthetic != 0 ||
            !instruction.source_span.is_valid() ||
            source_text_.empty() ||
            instruction.source_span.end_offset > source_text_.size()) {
            return false;
        }

        const std::string excerpt = collapse_source_excerpt(
            source_text_.substr(
                instruction.source_span.begin_offset,
                instruction.source_span.end_offset - instruction.source_span.begin_offset));
        return excerpt == "break;" ||
            excerpt == "break" ||
            excerpt == "continue;" ||
            excerpt == "continue";
    }

    void build_source_line_offsets() {
        source_line_offsets_.clear();
        if (source_text_.empty()) {
            return;
        }

        source_line_offsets_.push_back(0);
        for (std::size_t i = 0; i < source_text_.size(); ++i) {
            if (source_text_[i] == '\n') {
                source_line_offsets_.push_back(i + 1U);
            }
        }
    }

    [[nodiscard]] std::size_t source_line_number_from_offset(std::size_t offset) const {
        if (source_line_offsets_.empty() || offset > source_text_.size()) {
            return 0;
        }

        const auto it = std::upper_bound(
            source_line_offsets_.begin(),
            source_line_offsets_.end(),
            offset);
        return static_cast<std::size_t>(std::distance(source_line_offsets_.begin(), it));
    }

    [[nodiscard]] std::optional<std::pair<std::size_t, std::size_t>> source_line_range(
        SourceSpan source_span) const {
        if (!source_span.is_valid() ||
            source_span.begin_offset >= source_text_.size() ||
            source_span.end_offset > source_text_.size()) {
            return std::nullopt;
        }

        const std::size_t begin_line = source_line_number_from_offset(source_span.begin_offset);
        const std::size_t end_line = source_line_number_from_offset(
            source_span.end_offset > source_span.begin_offset
                ? (source_span.end_offset - 1U)
                : source_span.begin_offset);
        if (begin_line == 0 || end_line == 0) {
            return std::nullopt;
        }

        return std::pair(begin_line, end_line);
    }

    [[nodiscard]] std::string format_source_line_tag(SourceSpan source_span) const {
        const auto range = source_line_range(source_span);
        if (!range.has_value()) {
            return {};
        }

        if (range->first == range->second) {
            return "line " + std::to_string(range->first);
        }

        return "line " + std::to_string(range->first) +
            "-" + std::to_string(range->second);
    }

    void assign_block_labels(const CodeUnit& unit) {
        block_labels_.clear();

        std::unordered_map<std::string, std::size_t> counts;
        for (std::size_t i = 0; i < unit.basic_blocks.size(); ++i) {
            const BasicBlock* block = unit.basic_blocks[i].get();
            if (block == nullptr) {
                continue;
            }

            std::string base = block->label.empty()
                ? ("bb" + std::to_string(i))
                : std::string(block->label);

            std::size_t& count = counts[base];
            std::string label = count == 0
                ? base
                : (base + "." + std::to_string(count));
            ++count;

            block_labels_.emplace(block, std::move(label));
        }
    }

    void assign_slot_refs(const SlotTable& slot_table) {
        slot_refs_.clear();

        for (const Slot& slot : slot_table.slots) {
            if (slot.is_hidden() && slot.attrs.hidden_role != SlotAttrs::None) {
                if (slot.attrs.hidden_role == SlotAttrs::WorkspaceHandle &&
                    !slot.name.empty() &&
                    is_simple_identifier(slot.name)) {
                    slot_refs_.emplace(slot.slot_id, "%" + std::string(slot.name));
                    continue;
                }

                slot_refs_.emplace(
                    slot.slot_id,
                    "%" + std::string(hidden_role_name(
                        static_cast<SlotAttrs::HiddenRole>(slot.attrs.hidden_role))));
                continue;
            }

            slot_refs_.emplace(slot.slot_id, format_slot_id(slot.slot_id));
        }
    }

    [[nodiscard]] std::string format_block_ref(const BasicBlock* block) const {
        if (block == nullptr) {
            return "%<null-block>";
        }

        const auto it = block_labels_.find(block);
        if (it == block_labels_.end()) {
            return "%<unknown-block>";
        }

        return '%' + it->second;
    }

    [[nodiscard]] std::string format_slot_ref(SlotId slot_id) const {
        const auto it = slot_refs_.find(slot_id);
        if (it != slot_refs_.end()) {
            return it->second;
        }

        return format_slot_id(slot_id);
    }

    [[nodiscard]] std::string format_operand(const Operand& operand) const {
        return std::visit(
            [this](const auto& value) -> std::string {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, ValueId>) {
                    return format_value_id(value);
                } else if constexpr (std::is_same_v<T, SlotId>) {
                    return format_slot_ref(value);
                } else {
                    return format_symbol(value);
                }
            },
            operand);
    }

    [[nodiscard]] std::string format_result_prefix(
        const CodeUnit& unit,
        const std::vector<ValueId>& results) const {
        if (results.empty()) {
            return {};
        }
        if (results.size() == 1U) {
            return format_value_result(unit, results.front()) + " = ";
        }

        std::string text = "(";
        for (std::size_t i = 0; i < results.size(); ++i) {
            if (i != 0) {
                text += ", ";
            }
            text += format_value_result(unit, results[i]);
        }
        text += ") = ";
        return text;
    }

    [[nodiscard]] std::string format_type_fact(const TypeFact& fact) const {
        if (fact.is_unknown) {
            return "unknown";
        }

        std::ostringstream os;
        os << fact.types;
        return os.str();
    }

    [[nodiscard]] std::string format_value_type(
        const CodeUnit& unit,
        ValueId value_id) const {
        if (!options_.print_type_facts ||
            !value_id.is_valid()) {
            return {};
        }

        const ValueInfo* value_info = unit.value_table.find(value_id);
        if (value_info == nullptr) {
            return {};
        }

        return format_type_fact(value_info->type_fact);
    }

    [[nodiscard]] std::string format_value_result(
        const CodeUnit& unit,
        ValueId value_id) const {
        const std::string value_text = format_value_id(value_id);
        const std::string type_text = format_value_type(unit, value_id);
        if (type_text.empty()) {
            return value_text;
        }

        return "[" + value_text + ", " + type_text + "]";
    }

    [[nodiscard]] std::string format_operand_list(const std::vector<Operand>& operands) const {
        std::string text;
        for (std::size_t i = 0; i < operands.size(); ++i) {
            if (i != 0) {
                text += ", ";
            }
            text += format_operand(operands[i]);
        }
        return text;
    }

    [[nodiscard]] std::string format_call_like(
        const CodeUnit& unit,
        std::string_view opcode,
        const std::vector<ValueId>& results,
        const Operand& callee,
        const std::vector<Operand>& arguments,
        bool direct_callee,
        std::string_view direct_callee_prefix = {}) const {
        std::string text = format_result_prefix(unit, results);
        text += std::string(opcode);
        text += ' ';
        if (direct_callee) {
            const InternedString& direct_name = std::get<InternedString>(callee);
            if (direct_callee_prefix.empty()) {
                text += format_symbol(direct_name);
            } else {
                text += format_symbol(std::string(direct_callee_prefix) + std::string(direct_name));
            }
        } else {
            text += format_operand(callee);
        }
        text += '(';
        text += format_operand_list(arguments);
        text += ')';
        return text;
    }

    [[nodiscard]] std::string format_local_function_symbol(const FunctionUnit* function) const {
        if (function == nullptr || function->parent == nullptr) {
            return "@<local>";
        }

        return "@" + function->parent->file_stem().string() + "::" +
            std::string(function->name);
    }

    [[nodiscard]] std::string format_create_named_function_handle(
        const CodeUnit& unit,
        const CreateNamedFunctionHandleInst& inst) const {
        std::string text = format_value_result(unit, inst.result);
        text += " = create_named_func_handle ";
        text += format_symbol(inst.name);

        switch (inst.resolution_mode) {
            case CreateNamedFunctionHandleInst::RuntimeLookup:
                text += " lookup";
                break;
            case CreateNamedFunctionHandleInst::Prebound:
                text += " prebound ";
                text += function_handle_dispatch_name(inst.bound_dispatch_type);
                if (inst.bound_dispatch_type == MFunction) {
                    text += " ";
                    text += format_local_function_symbol(inst.m_function_target);
                }
                break;
        }

        return text;
    }

    [[nodiscard]] std::string format_create_anonymous_function_handle(
        const CodeUnit& unit,
        const CreateAnonymousFunctionHandleInst& inst) const {
        std::string text = format_value_result(unit, inst.result);
        text += " = create_anon_func ";
        text += format_anonymous_function_id(inst.function_id);
        if (inst.captures.empty()) {
            return text;
        }

        text += " captures { ";
        for (std::size_t i = 0; i < inst.captures.size(); ++i) {
            if (i != 0) {
                text += ", ";
            }

            const auto& capture = inst.captures[i];
            if (unit.is_script()) {
                text += format_symbol(capture.name);
            } else if (capture.source_slot.is_valid()) {
                text += format_slot_ref(capture.source_slot);
            } else {
                text += format_symbol(capture.name);
            }
        }
        text += " }";
        return text;
    }

    [[nodiscard]] std::string format_slot_decl(const Slot& slot) const {
        std::string text = format_slot_ref(slot.slot_id);
        text += " = ";
        text += slot_type_name(slot.type);

        if (slot.is_hidden() && slot.attrs.hidden_role != SlotAttrs::None) {
            text += '(';
            text += hidden_role_name(static_cast<SlotAttrs::HiddenRole>(slot.attrs.hidden_role));
            text += ')';
        }

        if (!slot.name.empty()) {
            text += ' ';
            text += format_symbol(slot.name);
        }

        const char* fixed_type =
            fixed_slot_type_name(static_cast<SlotAttrs::FixedType>(slot.attrs.fixed_type));
        if (fixed_type[0] != '\0') {
            text += " : ";
            text += fixed_type;
        }

        return text;
    }

    [[nodiscard]] std::string format_instruction(
        const CodeUnit& unit,
        const Instruction& instruction) const {
        switch (instruction.type()) {
            case Instruction::Const: {
                const auto& inst = static_cast<const ConstInst&>(instruction);
                return format_value_result(unit, inst.result) +
                    " = const " + format_constant(inst.value);
            }
            case Instruction::LoadSlot: {
                const auto& inst = static_cast<const LoadSlotInst&>(instruction);
                return format_value_result(unit, inst.result) +
                    " = load_slot " + format_slot_ref(inst.slot_id);
            }
            case Instruction::StoreSlot: {
                const auto& inst = static_cast<const StoreSlotInst&>(instruction);
                return "store_slot " + format_slot_ref(inst.slot_id) + ", " + format_operand(inst.value);
            }
            case Instruction::LoadWorkspace: {
                const auto& inst = static_cast<const LoadWorkspaceInst&>(instruction);
                return format_value_result(unit, inst.result) + " = load_env " +
                    format_slot_ref(inst.workspace_handle_slot) + ", " +
                    format_symbol(inst.symbol);
            }
            case Instruction::StoreWorkspace: {
                const auto& inst = static_cast<const StoreWorkspaceInst&>(instruction);
                return "store_env " + format_slot_ref(inst.workspace_handle_slot) +
                    ", " + format_symbol(inst.symbol) +
                    ", " + format_operand(inst.value);
            }
            case Instruction::CreateNamedFunctionHandle: {
                const auto& inst = static_cast<const CreateNamedFunctionHandleInst&>(instruction);
                return format_create_named_function_handle(unit, inst);
            }
            case Instruction::CreateAnonymousFunctionHandle: {
                const auto& inst =
                    static_cast<const CreateAnonymousFunctionHandleInst&>(instruction);
                return format_create_anonymous_function_handle(unit, inst);
            }
            case Instruction::Apply: {
                const auto& inst = static_cast<const ApplyInst&>(instruction);
                return format_call_like(
                    unit,
                    "apply",
                    inst.results,
                    inst.callee_or_base,
                    inst.arguments,
                    std::holds_alternative<InternedString>(inst.callee_or_base));
            }
            case Instruction::ValueApply: {
                const auto& inst = static_cast<const ValueApplyInst&>(instruction);
                return format_call_like(
                    unit,
                    "value_apply",
                    inst.results,
                    inst.base,
                    inst.arguments,
                    false);
            }
            case Instruction::Call: {
                const auto& inst = static_cast<const CallInst&>(instruction);
                return format_call_like(
                    unit,
                    call_mnemonic(inst),
                    inst.results,
                    inst.callee,
                    inst.arguments,
                    inst.callee_kind == CallInst::Direct,
                    inst.dispatch_type == Internal ? "internal." : "");
            }
            case Instruction::Copy: {
                const auto& inst = static_cast<const CopyInst&>(instruction);
                return format_value_result(unit, inst.result) + " = copy " + format_operand(inst.value);
            }
            case Instruction::Unary: {
                const auto& inst = static_cast<const UnaryInst&>(instruction);
                return format_value_result(unit, inst.result) + " = " +
                    std::string(unary_mnemonic(inst.op)) + ' ' +
                    format_operand(inst.operand);
            }
            case Instruction::Binary: {
                const auto& inst = static_cast<const BinaryInst&>(instruction);
                if (inst.dispatch_type == Internal) {
                    return format_value_result(unit, inst.result) + " = " +
                        std::string(internal_binary_mnemonic(inst.op)) + ' ' +
                        format_operand(inst.lhs) + ", " + format_operand(inst.rhs);
                }
                return format_value_result(unit, inst.result) + " = " +
                    std::string(binary_mnemonic(inst.op)) + ' ' +
                    format_operand(inst.lhs) + ", " + format_operand(inst.rhs);
            }
            case Instruction::Goto: {
                const auto& inst = static_cast<const GotoInst&>(instruction);
                return "br label " + format_block_ref(inst.target);
            }
            case Instruction::Branch: {
                const auto& inst = static_cast<const BranchInst&>(instruction);
                return "br " + format_operand(inst.condition) +
                    ", label " + format_block_ref(inst.true_target) +
                    ", label " + format_block_ref(inst.false_target);
            }
            case Instruction::Return: {
                const auto& inst = static_cast<const ReturnInst&>(instruction);
                if (inst.values.empty()) {
                    return "ret";
                }

                std::string text = "ret ";
                if (inst.values.size() == 1U) {
                    text += format_operand(inst.values.front());
                    return text;
                }

                text += '(';
                text += format_operand_list(inst.values);
                text += ')';
                return text;
            }
        }

        return "<unknown-inst>";
    }

    IRPrintOptions options_;
    std::string source_storage_;
    std::string_view source_text_;
    std::vector<std::size_t> source_line_offsets_;
    std::vector<IRRenderedLine> lines_;
    std::unordered_map<const BasicBlock*, std::string> block_labels_;
    std::unordered_map<SlotId, std::string> slot_refs_;
};

} // namespace

std::string format_ir(
    const MFileUnit& mfile,
    const IRPrintOptions& options) {
    IRPrinter printer(options, mfile);
    printer.render_file(mfile);
    return printer.str();
}

void print_ir(
    std::ostream& os,
    const MFileUnit& mfile,
    const IRPrintOptions& options) {
    IRPrinter printer(options, mfile);
    printer.render_file(mfile);
    printer.print(os);
}

} // namespace baltam
