#include "ir/ir_print.h"

#include "ir/ir_units.h"

#include "ba_obj/ba_obj.h"
#include "print/obj2str.h"

#include <algorithm>
#include <cctype>
#include <exception>
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

std::string pad_right(std::string text, std::size_t width) {
    if (text.size() < width) {
        text.append(width - text.size(), ' ');
    }
    return text;
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

std::string format_runtime_object_constant(const RuntimeObjectConstant& constant) {
    if (constant.value == nullptr) {
        return "<null>";
    }

    try {
        return internal::obj2str_one_line(*constant.value);
    } catch (const std::exception&) {
        try {
            return constant.value->brief_type_str();
        } catch (const std::exception&) {
            return "<unprintable>";
        }
    }
}

bool is_folded_runtime_constant(const Constant& constant) noexcept {
    const auto* value = std::get_if<RuntimeObjectConstant>(&constant);
    return value != nullptr && value->folded;
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
            } else if constexpr (std::is_same_v<T, RuntimeObjectConstant>) {
                return format_runtime_object_constant(value);
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

const char* slot_tag_name(SlotTag tag) noexcept {
    switch (tag) {
        case SlotTag::BaseVar:
            return "base";
        case SlotTag::ScriptVar:
            return "script";
        case SlotTag::Local:
            return "local";
        case SlotTag::Arg:
            return "arg";
        case SlotTag::Ret:
            return "ret";
        case SlotTag::Capture:
            return "capture";
        case SlotTag::InternalLocal:
            return "internal_local";
        case SlotTag::Global:
            return "global";
        case SlotTag::Persistent:
            return "persistent";
        case SlotTag::Nargin:
            return "nargin";
        case SlotTag::Nargout:
            return "nargout";
        case SlotTag::Varargin:
            return "varargin";
        case SlotTag::Varargout:
            return "varargout";
    }
    return "slot";
}

const char* slot_value_type_name(SlotValueType value_type) noexcept {
    switch (value_type) {
        case SlotValueType::Unknown:
            return "";
        case SlotValueType::Int64Scalar:
            return "int64";
        case SlotValueType::LogicalScalar:
            return "logical";
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

        const std::vector<const CodeUnit*> units = ordered_code_units(mfile);
        for (std::size_t i = 0; i < units.size(); ++i) {
            const CodeUnit* unit = units[i];
            if (unit != nullptr) {
                render_unit(*unit);
            }

            if (i + 1U < units.size()) {
                lines_.push_back({});
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
    [[nodiscard]] std::vector<const CodeUnit*> ordered_code_units(const MFileUnit& mfile) const {
        std::vector<const CodeUnit*> units;
        units.reserve(mfile.code_units.size());
        for (const auto& unit_ptr : mfile.code_units) {
            if (unit_ptr != nullptr) {
                units.push_back(unit_ptr.get());
            }
        }

        std::stable_sort(
            units.begin(),
            units.end(),
            [](const CodeUnit* lhs, const CodeUnit* rhs) {
                const bool lhs_valid = lhs != nullptr && lhs->source_span.is_valid();
                const bool rhs_valid = rhs != nullptr && rhs->source_span.is_valid();
                if (lhs_valid != rhs_valid) {
                    return lhs_valid;
                }
                if (!lhs_valid || lhs->source_span.begin_offset == rhs->source_span.begin_offset) {
                    return false;
                }
                return lhs->source_span.begin_offset < rhs->source_span.begin_offset;
            });

        return units;
    }

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
            std::string header = "anon " + format_symbol(function.name);
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
            emit_slot_table(unit.slot_table);
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

            emit_raw(format_block_label(*block));
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
        const std::vector<Slot>& slots) const {
        std::string text;
        for (std::size_t i = 0; i < slots.size(); ++i) {
            const Slot slot = slots[i];
            if (i != 0) {
                text += ", ";
            }

            text += format_slot_ref(slot);
            if (const SlotInfo* info = slot_table.find_slot(slot);
                info != nullptr && !info->name.empty()) {
                text += " " + format_symbol(info->name);
            }
        }
        return text;
    }

    void emit_raw(std::string text) {
        lines_.push_back({std::move(text), {}});
    }

    void emit_slot_table(const SlotTable& slot_table) {
        std::size_t slot_ref_width = 0;
        std::size_t slot_tag_width = 0;
        for (const SlotInfo& slot : slot_table.slots) {
            slot_ref_width = std::max(slot_ref_width, format_slot_ref(slot.slot).size());
            slot_tag_width = std::max(
                slot_tag_width,
                std::string_view(slot_tag_name(slot.slot.tag)).size());
        }

        emit_raw("  ; slots:");
        for (const SlotInfo& slot : slot_table.slots) {
            emit_raw(format_slot_decl(slot, slot_ref_width, slot_tag_width));
        }
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
                        enqueue_deferred_block(deferred_returns, successor);
                    } else {
                        enqueue_deferred_block(deferred, successor);
                    }
                    continue;
                }
                visit_ref(successor, visit_ref);
            }
        };

        visit(unit.entry_block, visit);

        while (!deferred.empty()) {
            const BasicBlock* block = deferred.front();
            deferred.erase(deferred.begin());
            visit(block, visit);
        }

        while (!deferred_returns.empty()) {
            const BasicBlock* block = deferred_returns.front();
            deferred_returns.erase(deferred_returns.begin());
            visit(block, visit);
        }

        for (const auto& block_ptr : unit.basic_blocks) {
            visit(block_ptr.get(), visit);
        }

        return ordered;
    }

    void enqueue_deferred_block(
        std::vector<const BasicBlock*>& deferred,
        const BasicBlock* block) const {
        if (block == nullptr ||
            std::find(deferred.begin(), deferred.end(), block) != deferred.end()) {
            return;
        }

        const auto position = std::find_if(
            deferred.begin(),
            deferred.end(),
            [block](const BasicBlock* existing) {
                return block_source_order_key(block) > block_source_order_key(existing);
            });
        deferred.insert(position, block);
    }

    [[nodiscard]] static SourceSpan::offset_type block_source_order_key(
        const BasicBlock* block) noexcept {
        if (block == nullptr || !block->source_span.is_valid()) {
            return 0;
        }
        return block->source_span.begin_offset;
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
            !is_loop_latch_block(&block) &&
            is_if_branch_block(&block)) {
            return true;
        }

        if (is_loop_exit_block(successor) &&
            !is_loop_header_natural_exit(block, *successor)) {
            return true;
        }

        if (is_switch_exit_block(successor)) {
            return true;
        }

        if (is_if_branch_block(&block) &&
            successor->label == "if.exit" &&
            successor->predecessors.size() > 1U) {
            return true;
        }

        return false;
    }

    [[nodiscard]] bool is_if_branch_block(const BasicBlock* block) const {
        return block != nullptr &&
            (block->label == "if.then" || block->label == "if.else");
    }

    [[nodiscard]] bool is_loop_header_natural_exit(
        const BasicBlock& block,
        const BasicBlock& successor) const {
        return (block.label == "for.header" && successor.label == "for.end") ||
            (block.label == "while.header" && successor.label == "while.end");
    }

    [[nodiscard]] bool is_switch_exit_block(const BasicBlock* block) const {
        return is_switch_block_kind(block, "end");
    }

    [[nodiscard]] bool is_switch_block_kind(
        const BasicBlock* block,
        std::string_view kind) const {
        if (block == nullptr) {
            return false;
        }

        const std::string& label = block->label;
        const std::string first_switch_label = "switch." + std::string(kind);
        if (label == first_switch_label) {
            return true;
        }

        constexpr std::string_view prefix = "switch.";
        const std::string suffix = "." + std::string(kind);
        return label.rfind(prefix, 0) == 0 &&
            label.size() > prefix.size() + suffix.size() &&
            label.compare(label.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    [[nodiscard]] bool is_loop_latch_block(const BasicBlock* block) const {
        return block != nullptr && block->label == "for.latch";
    }

    [[nodiscard]] bool is_loop_exit_block(const BasicBlock* block) const {
        return block != nullptr &&
            (block->label == "for.end" || block->label == "while.end");
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

        if (!options_.print_source_line_numbers) {
            if (!options_.print_source_excerpt) {
                return {};
            }

            std::string excerpt = collapse_source_excerpt(
                source_text_.substr(
                    source_span.begin_offset,
                    source_span.end_offset - source_span.begin_offset));
            if (excerpt.empty()) {
                return {};
            }
            return excerpt;
        }

        const std::string line_tag = format_source_line_tag(source_span);
        if (line_tag.empty()) {
            if (!options_.print_source_excerpt) {
                return {};
            }

            std::string excerpt = collapse_source_excerpt(
                source_text_.substr(
                    source_span.begin_offset,
                    source_span.end_offset - source_span.begin_offset));
            return excerpt;
        }

        if (!options_.print_source_excerpt) {
            return line_tag;
        }

        std::string excerpt = collapse_source_excerpt(
            source_text_.substr(
                source_span.begin_offset,
                source_span.end_offset - source_span.begin_offset));
        if (excerpt.empty()) {
            return line_tag;
        }

        return line_tag + ": " + excerpt;
    }

    [[nodiscard]] bool should_print_instruction_source_comment(
        const Instruction& instruction) const {
        switch (instruction.type()) {
            case Instruction::GlobalDecl:
            case Instruction::PersistentDecl:
            case Instruction::StoreSlot:
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

        std::size_t next_block_id = 0;
        for (std::size_t i = 0; i < unit.basic_blocks.size(); ++i) {
            const BasicBlock* block = unit.basic_blocks[i].get();
            if (block == nullptr) {
                continue;
            }

            block_labels_.emplace(block, "L" + std::to_string(next_block_id++));
        }
    }

    void assign_slot_refs(const SlotTable& slot_table) {
        slot_refs_.clear();

        for (const SlotInfo& info : slot_table.slots) {
            slot_refs_.emplace(info.slot.id, format_slot_id(info.slot.id));
        }
    }

    [[nodiscard]] std::string format_block_ref(const BasicBlock* block) const {
        if (block == nullptr) {
            return "<null-block>";
        }

        const auto it = block_labels_.find(block);
        if (it == block_labels_.end()) {
            return "<unknown-block>";
        }

        return it->second;
    }

    [[nodiscard]] std::string format_block_label_ref(const BasicBlock* block) const {
        if (block == nullptr) {
            return "<null-block>";
        }

        const auto it = block_labels_.find(block);
        if (it == block_labels_.end()) {
            return "<unknown-block>";
        }

        return it->second;
    }

    [[nodiscard]] std::string format_block_label(const BasicBlock& block) const {
        const auto it = block_labels_.find(&block);
        std::string text = (it == block_labels_.end() ? "<unknown-block>" : it->second);
        text += ": ; Type = ";
        text += block.label.empty() ? "<unnamed>" : block.label;

        if (!options_.print_block_predecessors) {
            return text;
        }

        text += ", preds = [";
        for (std::size_t i = 0; i < block.predecessors.size(); ++i) {
            if (i != 0) {
                text += ", ";
            }
            text += format_block_label_ref(block.predecessors[i]);
        }
        text += ']';
        return text;
    }

    [[nodiscard]] std::string format_slot_ref(Slot slot) const {
        const auto it = slot_refs_.find(slot.id);
        if (it != slot_refs_.end()) {
            return it->second;
        }

        return format_slot_id(slot.id);
    }

    [[nodiscard]] std::string format_operand(const Operand& operand) const {
        return std::visit(
            [this](const auto& value) -> std::string {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, ValueId>) {
                    return format_value_id(value);
                } else if constexpr (std::is_same_v<T, Slot>) {
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
            if (!results.front().is_valid()) {
                return "[] = ";
            }
            return format_value_result(unit, results.front()) + " = ";
        }

        std::string text = "(";
        for (std::size_t i = 0; i < results.size(); ++i) {
            if (i != 0) {
                text += ", ";
            }
            if (!results[i].is_valid()) {
                text += "[]";
                continue;
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

    [[nodiscard]] std::string format_value_list(const std::vector<ValueId>& values) const {
        std::string text;
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (i != 0) {
                text += ", ";
            }
            text += format_value_id(values[i]);
        }
        return text;
    }

    [[nodiscard]] std::string format_slot_list(const std::vector<Slot>& slots) const {
        std::string text;
        for (std::size_t i = 0; i < slots.size(); ++i) {
            if (i != 0) {
                text += ", ";
            }
            text += format_slot_ref(slots[i]);
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

    [[nodiscard]] std::string format_value_apply(
        const CodeUnit& unit,
        const ValueApplyInst& inst) const {
        std::string text = format_result_prefix(unit, inst.results);
        text += "value_apply(";
        text += format_value_id(inst.base);
        if (!inst.arguments.empty()) {
            text += ", ";
            text += format_operand_list(inst.arguments);
        }
        text += ')';
        return text;
    }

    [[nodiscard]] std::string format_magic_end_candidate(const Operand& candidate) const {
        return std::visit(
            [this](const auto& value) -> std::string {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, ValueId>) {
                    return format_value_id(value);
                } else if constexpr (std::is_same_v<T, Slot>) {
                    return format_slot_ref(value);
                } else {
                    return is_simple_identifier(value)
                        ? std::string(value)
                        : '"' + escape_text(value) + '"';
                }
            },
            candidate);
    }

    [[nodiscard]] std::string format_magic_end_context(const MagicEndInst::MagicEndContext& context) const {
        std::string text = "(";
        text += format_magic_end_candidate(context.callee_or_base);
        text += ", ";
        text += std::to_string(context.dim);
        text += ", ";
        text += std::to_string(context.nindices);
        text += ')';
        return text;
    }

    [[nodiscard]] std::string format_magic_end(
        const CodeUnit& unit,
        const MagicEndInst& inst) const {
        std::string text = format_value_result(unit, inst.result);
        text += " = magic_end([";
        for (std::size_t i = 0; i < inst.candidate_contexts.size(); ++i) {
            if (i != 0) {
                text += " -> ";
            }
            text += format_magic_end_context(inst.candidate_contexts[i]);
        }
        text += "])";
        return text;
    }

    [[nodiscard]] std::string format_local_function_symbol(const FunctionUnit* function) const {
        if (function == nullptr || function->file == nullptr) {
            return "@<local>";
        }

        return "@" + function->file->file_stem().string() + "::" +
            std::string(function->name);
    }

    [[nodiscard]] std::string format_create_named_function_handle(
        const CodeUnit& unit,
        const CreateNamedFunctionHandleInst& inst) const {
        std::string text = format_value_result(unit, inst.result);
        text += " = create_named_func_handle ";
        text += format_symbol(inst.name);

        switch (inst.resolution_mode) {
            case CreateNamedFunctionHandleInst::Runtime:
                text += " runtime";
                break;
            case CreateNamedFunctionHandleInst::Static:
                text += " static ";
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
        text += inst.target != nullptr
            ? format_symbol(inst.target->name)
            : std::string("<null>");
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

    [[nodiscard]] std::string format_slot_decl(
        const SlotInfo& slot,
        std::size_t slot_ref_width,
        std::size_t slot_tag_width) const {
        std::string text = "  ; ";
        text += pad_right(format_slot_ref(slot.slot), slot_ref_width);
        text += " = ";
        text += pad_right(slot_tag_name(slot.slot.tag), slot_tag_width);

        if (!slot.name.empty()) {
            text += ' ';
            text += format_symbol(slot.name);
        }

        const char* fixed_type = slot_value_type_name(slot.value_type);
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
                std::string text = format_value_result(unit, inst.result) +
                    " = const " + format_constant(inst.value);
                if (is_folded_runtime_constant(inst.value)) {
                    text += " ; folded";
                }
                return text;
            }
            case Instruction::LoadSlot: {
                const auto& inst = static_cast<const LoadSlotInst&>(instruction);
                return format_value_result(unit, inst.result) +
                    " = load " + format_slot_ref(inst.slot);
            }
            case Instruction::StoreSlot: {
                const auto& inst = static_cast<const StoreSlotInst&>(instruction);
                return "store " + format_slot_ref(inst.slot) + ", " +
                    format_value_id(inst.value);
            }
            case Instruction::GlobalDecl: {
                const auto& inst = static_cast<const GlobalDeclInst&>(instruction);
                return "global " + format_slot_list(inst.slots);
            }
            case Instruction::PersistentDecl: {
                const auto& inst = static_cast<const PersistentDeclInst&>(instruction);
                return "persistent " + format_slot_list(inst.slots);
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
                return format_value_apply(unit, inst);
            }
            case Instruction::MagicEnd: {
                const auto& inst = static_cast<const MagicEndInst&>(instruction);
                return format_magic_end(unit, inst);
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
                    text += format_value_id(inst.values.front());
                    return text;
                }

                text += '(';
                text += format_value_list(inst.values);
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
