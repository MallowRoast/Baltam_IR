#include "tools/cfg_dot.h"

#include "ir/ir_inst.h"
#include "ir/ir_units.h"

#include <cstddef>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>

namespace baltam {
namespace {

std::string escape_dot(std::string_view text) {
    std::string escaped;
    escaped.reserve(text.size());

    for (char ch : text) {
        switch (ch) {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
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
                escaped.push_back(ch);
                break;
        }
    }

    return escaped;
}

std::string make_graph_id(std::string_view prefix, std::size_t index) {
    return std::string(prefix) + '_' + std::to_string(index);
}

std::string make_unit_graph_name(const CodeUnit& unit, std::size_t index) {
    if (!unit.name.empty()) {
        return "cfg_" + unit.name;
    }
    return make_graph_id("cfg_unit", index);
}

std::string base_block_label(const BasicBlock& block) {
    return block.label.empty() ? "bb" : std::string(block.label);
}

const char* node_color_for_label(std::string_view label) noexcept {
    if (label.rfind("for.", 0) == 0) {
        return "#e7f0ff";
    }
    if (label.rfind("while.", 0) == 0) {
        return "#eaf7ea";
    }
    if (label.rfind("if.", 0) == 0) {
        return "#fff4dc";
    }
    if (label == "entry") {
        return "#eeeeee";
    }
    return "#ffffff";
}

const char* edge_color_for_label(std::string_view label) noexcept {
    if (label == "true") {
        return "#267f26";
    }
    if (label == "false") {
        return "#b00020";
    }
    if (label == "break") {
        return "#b00020";
    }
    if (label == "continue") {
        return "#1f5fbf";
    }
    return "#555555";
}

std::string edge_source_label(const GotoInst& inst, const CFGDotOptions& options) {
    if (!options.label_break_continue_edges || inst.attrs.is_synthetic != 0) {
        return {};
    }

    // 当前 lowering 中非 synthetic goto 只由 break / continue 生成。根据目标块类别
    // 标注边，避免依赖 SourceSpan 是否包含分号或空白。
    if (inst.target != nullptr &&
        (inst.target->label == "for.end" || inst.target->label == "while.end")) {
        return "break";
    }
    if (inst.target != nullptr &&
        (inst.target->label == "for.latch" || inst.target->label == "while.latch")) {
        return "continue";
    }

    return {};
}

std::size_t count_non_terminator_instructions(const BasicBlock& block) {
    std::size_t count = block.instructions.size();
    if (block.terminator() != nullptr && count > 0) {
        --count;
    }
    return count;
}

class CFGDotPrinter final {
public:
    explicit CFGDotPrinter(const CFGDotOptions& options)
        : options_(options) {}

    void print_file(std::ostream& os, const MFileUnit& mfile) {
        os << "digraph \"" << escape_dot(mfile.file_stem().string()) << "\" {\n";
        print_graph_defaults(os);
        os << "  compound=true;\n";

        for (std::size_t i = 0; i < mfile.code_units.size(); ++i) {
            const CodeUnit* unit = mfile.code_units[i].get();
            if (unit != nullptr) {
                print_unit_cluster(os, *unit, i);
            }
        }

        os << "}\n";
    }

    void print_unit(std::ostream& os, const CodeUnit& unit) {
        os << "digraph \"" << escape_dot(make_unit_graph_name(unit, 0)) << "\" {\n";
        print_graph_defaults(os);
        build_block_ids(unit, {});
        print_unit_body(os, unit, "  ");
        os << "}\n";
    }

private:
    void print_graph_defaults(std::ostream& os) const {
        os << "  rankdir=TB;\n";
        os << "  node [shape=box, style=\"rounded,filled\", fontname=\"monospace\", "
              "fontsize=10];\n";
        os << "  edge [fontname=\"monospace\", fontsize=9, color=\"#555555\"];\n";
    }

    void print_unit_cluster(std::ostream& os, const CodeUnit& unit, std::size_t index) {
        const std::string cluster_id = make_graph_id("cluster_unit", index);
        os << "  subgraph " << cluster_id << " {\n";
        os << "    label=\"" << escape_dot(unit.name) << "\";\n";
        os << "    color=\"#cccccc\";\n";
        build_block_ids(unit, "u" + std::to_string(index) + "_");
        print_unit_body(os, unit, "    ");
        os << "  }\n";
    }

    void build_block_ids(const CodeUnit& unit, std::string_view prefix) {
        block_ids_.clear();
        block_labels_.clear();
        std::unordered_map<std::string, std::size_t> label_counts;

        for (std::size_t i = 0; i < unit.basic_blocks.size(); ++i) {
            const BasicBlock* block = unit.basic_blocks[i].get();
            if (block != nullptr) {
                block_ids_.emplace(block, std::string(prefix) + make_graph_id("bb", i));

                std::string base = base_block_label(*block);
                if (block->label.empty()) {
                    base += std::to_string(i);
                }

                std::size_t& count = label_counts[base];
                std::string label = count == 0
                    ? base
                    : (base + "." + std::to_string(count));
                ++count;

                block_labels_.emplace(block, std::move(label));
            }
        }
    }

    void print_unit_body(std::ostream& os, const CodeUnit& unit, std::string_view indent) const {
        for (const auto& block_ptr : unit.basic_blocks) {
            if (block_ptr != nullptr) {
                print_block_node(os, *block_ptr, indent);
            }
        }

        for (const auto& block_ptr : unit.basic_blocks) {
            if (block_ptr != nullptr) {
                print_block_edges(os, *block_ptr, indent);
            }
        }
    }

    void print_block_node(
        std::ostream& os,
        const BasicBlock& block,
        std::string_view indent) const {
        const std::string* id = find_block_id(block);
        const std::string* display_label = find_block_label(block);
        if (id == nullptr || display_label == nullptr) {
            return;
        }

        std::string label = *display_label;
        if (options_.print_instruction_counts) {
            label += "\ninsts: " +
                std::to_string(count_non_terminator_instructions(block));
        }

        os << indent << *id << " [label=\"" << escape_dot(label) << "\", fillcolor=\""
           << node_color_for_label(block.label) << "\"];\n";
    }

    void print_block_edges(
        std::ostream& os,
        const BasicBlock& block,
        std::string_view indent) const {
        const Instruction* terminator = block.terminator();
        if (terminator == nullptr) {
            return;
        }

        switch (terminator->type()) {
            case Instruction::Goto: {
                const auto& go = static_cast<const GotoInst&>(*terminator);
                print_edge(os, block, go.target, edge_source_label(go, options_), indent);
                return;
            }
            case Instruction::Branch: {
                const auto& branch = static_cast<const BranchInst&>(*terminator);
                print_edge(os, block, branch.true_target, "true", indent);
                print_edge(os, block, branch.false_target, "false", indent);
                return;
            }
            case Instruction::Return:
                return;
            case Instruction::Const:
            case Instruction::LoadSlot:
            case Instruction::StoreSlot:
            case Instruction::LoadWorkspace:
            case Instruction::StoreWorkspace:
            case Instruction::Apply:
            case Instruction::Call:
            case Instruction::Copy:
            case Instruction::Unary:
            case Instruction::Binary:
                return;
        }
    }

    void print_edge(
        std::ostream& os,
        const BasicBlock& source,
        const BasicBlock* target,
        std::string_view label,
        std::string_view indent) const {
        const std::string* source_id = find_block_id(source);
        const std::string* target_id = find_block_id(target);
        if (source_id == nullptr || target_id == nullptr) {
            return;
        }

        os << indent << *source_id << " -> " << *target_id;
        if (!label.empty()) {
            os << " [label=\"" << escape_dot(label) << "\", color=\""
               << edge_color_for_label(label) << "\"]";
        }
        os << ";\n";
    }

    const std::string* find_block_id(const BasicBlock& block) const {
        return find_block_id(&block);
    }

    const std::string* find_block_id(const BasicBlock* block) const {
        const auto it = block_ids_.find(block);
        return it != block_ids_.end() ? &it->second : nullptr;
    }

    const std::string* find_block_label(const BasicBlock& block) const {
        const auto it = block_labels_.find(&block);
        return it != block_labels_.end() ? &it->second : nullptr;
    }

    const CFGDotOptions& options_;
    std::unordered_map<const BasicBlock*, std::string> block_ids_;
    std::unordered_map<const BasicBlock*, std::string> block_labels_;
};

} // namespace

std::string format_cfg_dot(const CodeUnit& unit, const CFGDotOptions& options) {
    std::ostringstream os;
    print_cfg_dot(os, unit, options);
    return os.str();
}

std::string format_cfg_dot(const MFileUnit& mfile, const CFGDotOptions& options) {
    std::ostringstream os;
    print_cfg_dot(os, mfile, options);
    return os.str();
}

void print_cfg_dot(std::ostream& os, const CodeUnit& unit, const CFGDotOptions& options) {
    CFGDotPrinter printer(options);
    printer.print_unit(os, unit);
}

void print_cfg_dot(std::ostream& os, const MFileUnit& mfile, const CFGDotOptions& options) {
    CFGDotPrinter printer(options);
    printer.print_file(os, mfile);
}

} // namespace baltam
