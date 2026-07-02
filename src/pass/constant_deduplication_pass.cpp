#include "pass/constant_deduplication_pass.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iterator>
#include <memory>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace baltam {
namespace {

using ValueRewriteMap = std::unordered_map<ValueId::underlying_type, ValueId>;

[[nodiscard]] std::uint64_t double_bits(double value) noexcept {
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "double and uint64_t size mismatch");
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

void hash_combine(std::size_t& seed, std::size_t value) noexcept {
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
}

[[nodiscard]] bool constant_equal(LogicalConstant lhs, LogicalConstant rhs) noexcept {
    return lhs.value == rhs.value;
}

[[nodiscard]] bool constant_equal(Int64Constant lhs, Int64Constant rhs) noexcept {
    return lhs.value == rhs.value;
}

[[nodiscard]] bool constant_equal(UInt64Constant lhs, UInt64Constant rhs) noexcept {
    return lhs.value == rhs.value;
}

[[nodiscard]] bool constant_equal(Float64Constant lhs, Float64Constant rhs) noexcept {
    return double_bits(lhs.value) == double_bits(rhs.value);
}

[[nodiscard]] bool constant_equal(Complex128Constant lhs, Complex128Constant rhs) noexcept {
    return double_bits(lhs.re) == double_bits(rhs.re) &&
        double_bits(lhs.im) == double_bits(rhs.im);
}

[[nodiscard]] bool constant_equal(
    const CharLiteralConstant& lhs,
    const CharLiteralConstant& rhs) {
    return lhs.value == rhs.value;
}

[[nodiscard]] bool constant_equal(
    const StringLiteralConstant& lhs,
    const StringLiteralConstant& rhs) {
    return lhs.value == rhs.value;
}

[[nodiscard]] bool constant_equal(
    EmptyDoubleMatrixConstant,
    EmptyDoubleMatrixConstant) noexcept {
    return true;
}

[[nodiscard]] std::size_t constant_hash(LogicalConstant value) {
    return std::hash<bool>{}(value.value);
}

[[nodiscard]] std::size_t constant_hash(Int64Constant value) {
    return std::hash<std::int64_t>{}(value.value);
}

[[nodiscard]] std::size_t constant_hash(UInt64Constant value) {
    return std::hash<std::uint64_t>{}(value.value);
}

[[nodiscard]] std::size_t constant_hash(Float64Constant value) {
    return std::hash<std::uint64_t>{}(double_bits(value.value));
}

[[nodiscard]] std::size_t constant_hash(Complex128Constant value) {
    std::size_t seed = std::hash<std::uint64_t>{}(double_bits(value.re));
    hash_combine(seed, std::hash<std::uint64_t>{}(double_bits(value.im)));
    return seed;
}

[[nodiscard]] std::size_t constant_hash(const CharLiteralConstant& value) {
    return std::hash<InternedString>{}(value.value);
}

[[nodiscard]] std::size_t constant_hash(const StringLiteralConstant& value) {
    return std::hash<InternedString>{}(value.value);
}

[[nodiscard]] std::size_t constant_hash(EmptyDoubleMatrixConstant) {
    return 0x517cc1b727220a95ULL;
}

struct ConstantHash {
    [[nodiscard]] std::size_t operator()(const Constant& constant) const {
        std::size_t seed = std::hash<std::size_t>{}(constant.index());
        std::visit(
            [&seed](const auto& value) {
                hash_combine(seed, constant_hash(value));
            },
            constant);
        return seed;
    }
};

struct ConstantEqual {
    [[nodiscard]] bool operator()(const Constant& lhs, const Constant& rhs) const {
        if (lhs.index() != rhs.index()) {
            return false;
        }

        return std::visit(
            [](const auto& lhs_value, const auto& rhs_value) {
                using LhsT = std::decay_t<decltype(lhs_value)>;
                using RhsT = std::decay_t<decltype(rhs_value)>;
                if constexpr (std::is_same_v<LhsT, RhsT>) {
                    return constant_equal(lhs_value, rhs_value);
                } else {
                    return false;
                }
            },
            lhs,
            rhs);
    }
};

struct DuplicateConst {
    ConstInst* canonical = nullptr;
    ConstInst* duplicate = nullptr;
};

[[nodiscard]] bool contains_block(const CodeUnit& unit, const BasicBlock* block) {
    return block != nullptr &&
        std::any_of(
            unit.basic_blocks.begin(),
            unit.basic_blocks.end(),
            [block](const std::unique_ptr<BasicBlock>& candidate) {
                return candidate.get() == block;
            });
}

[[nodiscard]] bool move_entry_block_to_front(CodeUnit& unit) {
    if (unit.entry_block == nullptr || unit.basic_blocks.empty()) {
        return false;
    }

    auto entry_it = std::find_if(
        unit.basic_blocks.begin(),
        unit.basic_blocks.end(),
        [&unit](const std::unique_ptr<BasicBlock>& candidate) {
            return candidate.get() == unit.entry_block;
        });

    if (entry_it == unit.basic_blocks.end() || entry_it == unit.basic_blocks.begin()) {
        return false;
    }

    std::rotate(unit.basic_blocks.begin(), entry_it, std::next(entry_it));
    return true;
}

void rewrite_value(ValueId& value, const ValueRewriteMap& replacements) {
    if (!value.is_valid()) {
        return;
    }

    const auto replacement = replacements.find(value.value());
    if (replacement != replacements.end()) {
        value = replacement->second;
    }
}

void rewrite_operand(Operand& operand, const ValueRewriteMap& replacements) {
    if (auto* value = std::get_if<ValueId>(&operand)) {
        rewrite_value(*value, replacements);
    }
}

void rewrite_operands(std::vector<Operand>& operands, const ValueRewriteMap& replacements) {
    for (Operand& operand : operands) {
        rewrite_operand(operand, replacements);
    }
}

void rewrite_values(std::vector<ValueId>& values, const ValueRewriteMap& replacements) {
    for (ValueId& value : values) {
        rewrite_value(value, replacements);
    }
}

void rewrite_instruction_uses(Instruction& instruction, const ValueRewriteMap& replacements) {
    switch (instruction.type()) {
        case Instruction::StoreSlot:
            rewrite_operand(static_cast<StoreSlotInst&>(instruction).value, replacements);
            break;
        case Instruction::CreateAnonymousFunctionHandle:
            for (auto& capture :
                 static_cast<CreateAnonymousFunctionHandleInst&>(instruction).captures) {
                rewrite_value(capture.captured_value, replacements);
            }
            break;
        case Instruction::Apply: {
            auto& inst = static_cast<ApplyInst&>(instruction);
            rewrite_operand(inst.callee_or_base, replacements);
            rewrite_operands(inst.arguments, replacements);
            break;
        }
        case Instruction::ValueApply: {
            auto& inst = static_cast<ValueApplyInst&>(instruction);
            rewrite_value(inst.base, replacements);
            rewrite_operands(inst.arguments, replacements);
            break;
        }
        case Instruction::MagicEnd:
            for (auto& context : static_cast<MagicEndInst&>(instruction).candidate_contexts) {
                rewrite_operand(context.callee_or_base, replacements);
            }
            break;
        case Instruction::Call: {
            auto& inst = static_cast<CallInst&>(instruction);
            rewrite_operand(inst.callee, replacements);
            rewrite_operands(inst.arguments, replacements);
            break;
        }
        case Instruction::Copy:
            rewrite_operand(static_cast<CopyInst&>(instruction).value, replacements);
            break;
        case Instruction::Unary:
            rewrite_operand(static_cast<UnaryInst&>(instruction).operand, replacements);
            break;
        case Instruction::Binary: {
            auto& inst = static_cast<BinaryInst&>(instruction);
            rewrite_operand(inst.lhs, replacements);
            rewrite_operand(inst.rhs, replacements);
            break;
        }
        case Instruction::Branch:
            rewrite_operand(static_cast<BranchInst&>(instruction).condition, replacements);
            break;
        case Instruction::Return:
            rewrite_values(static_cast<ReturnInst&>(instruction).values, replacements);
            break;
        case Instruction::Const:
        case Instruction::LoadSlot:
        case Instruction::GlobalDecl:
        case Instruction::PersistentDecl:
        case Instruction::CreateNamedFunctionHandle:
        case Instruction::Goto:
            break;
    }
}

void rewrite_uses(CodeUnit& unit, const ValueRewriteMap& replacements) {
    if (replacements.empty()) {
        return;
    }

    for (const auto& block_ptr : unit.basic_blocks) {
        if (block_ptr == nullptr) {
            continue;
        }
        for (const auto& inst_ptr : block_ptr->instructions) {
            if (inst_ptr != nullptr) {
                rewrite_instruction_uses(*inst_ptr, replacements);
            }
        }
    }
}

[[nodiscard]] std::vector<DuplicateConst> collect_duplicate_constants(CodeUnit& unit) {
    std::unordered_map<Constant, ConstInst*, ConstantHash, ConstantEqual> canonical_by_constant;
    std::vector<DuplicateConst> duplicates;

    for (const auto& block_ptr : unit.basic_blocks) {
        if (block_ptr == nullptr) {
            continue;
        }

        for (const auto& inst_ptr : block_ptr->instructions) {
            if (inst_ptr == nullptr || inst_ptr->type() != Instruction::Const) {
                continue;
            }

            auto& inst = static_cast<ConstInst&>(*inst_ptr);
            if (!inst.result.is_valid()) {
                continue;
            }

            const auto [canonical_it, inserted] =
                canonical_by_constant.emplace(inst.value, &inst);
            if (inserted) {
                continue;
            }

            duplicates.push_back({canonical_it->second, &inst});
        }
    }

    return duplicates;
}

[[nodiscard]] std::vector<ConstInst*> canonical_hoist_order(
    const std::vector<DuplicateConst>& duplicates) {
    std::vector<ConstInst*> order;
    std::unordered_set<const ConstInst*> seen;
    order.reserve(duplicates.size());

    for (const DuplicateConst& duplicate : duplicates) {
        if (duplicate.canonical != nullptr && seen.insert(duplicate.canonical).second) {
            order.push_back(duplicate.canonical);
        }
    }

    return order;
}

void update_value_table(
    CodeUnit& unit,
    const std::vector<DuplicateConst>& duplicates,
    const std::vector<ConstInst*>& canonical_constants) {
    for (const DuplicateConst& duplicate : duplicates) {
        if (duplicate.duplicate == nullptr ||
            duplicate.canonical == nullptr ||
            duplicate.duplicate->result == duplicate.canonical->result) {
            continue;
        }

        if (ValueInfo* value_info = unit.value_table.find(duplicate.duplicate->result)) {
            value_info->def = nullptr;
        }
    }

    for (ConstInst* canonical : canonical_constants) {
        if (canonical == nullptr) {
            continue;
        }

        if (ValueInfo* value_info = unit.value_table.find(canonical->result)) {
            value_info->result_index = 0;
            value_info->def = canonical;
        }
    }
}

void rewrite_const_layout(
    CodeUnit& unit,
    const std::vector<DuplicateConst>& duplicates,
    const std::vector<ConstInst*>& canonical_constants) {
    std::unordered_set<const Instruction*> duplicate_instructions;
    std::unordered_set<const Instruction*> canonical_instructions;

    for (const DuplicateConst& duplicate : duplicates) {
        if (duplicate.duplicate != nullptr) {
            duplicate_instructions.insert(duplicate.duplicate);
        }
    }
    for (ConstInst* canonical : canonical_constants) {
        if (canonical != nullptr) {
            canonical_instructions.insert(canonical);
        }
    }

    std::vector<std::unique_ptr<Instruction>> hoisted_constants;
    hoisted_constants.reserve(canonical_constants.size());

    for (const auto& block_ptr : unit.basic_blocks) {
        if (block_ptr == nullptr) {
            continue;
        }

        std::vector<std::unique_ptr<Instruction>> kept_instructions;
        kept_instructions.reserve(block_ptr->instructions.size());

        for (auto& inst_ptr : block_ptr->instructions) {
            Instruction* instruction = inst_ptr.get();
            if (instruction == nullptr) {
                kept_instructions.push_back(std::move(inst_ptr));
                continue;
            }

            if (duplicate_instructions.find(instruction) != duplicate_instructions.end()) {
                continue;
            }

            if (canonical_instructions.find(instruction) != canonical_instructions.end()) {
                instruction->parent = unit.entry_block;
                hoisted_constants.push_back(std::move(inst_ptr));
                continue;
            }

            kept_instructions.push_back(std::move(inst_ptr));
        }

        block_ptr->instructions = std::move(kept_instructions);
    }

    if (unit.entry_block == nullptr || hoisted_constants.empty()) {
        return;
    }

    unit.entry_block->instructions.insert(
        unit.entry_block->instructions.begin(),
        std::make_move_iterator(hoisted_constants.begin()),
        std::make_move_iterator(hoisted_constants.end()));
}

} // namespace

IRPassResult ConstantDeduplicationPass::run(CodeUnit& unit, IRPassContext& context) {
    (void)context;

    IRPassResult result;
    if (unit.entry_block == nullptr) {
        result.report(
            IRPassDiagnostic::Warning,
            "skip constant deduplication: entry block is null",
            unit.source_span);
        return result;
    }

    if (!contains_block(unit, unit.entry_block)) {
        result.report(
            IRPassDiagnostic::Warning,
            "skip constant deduplication: entry block does not belong to the code unit",
            unit.source_span);
        return result;
    }

    std::vector<DuplicateConst> duplicates = collect_duplicate_constants(unit);
    if (duplicates.empty()) {
        return result;
    }

    ValueRewriteMap replacements;
    replacements.reserve(duplicates.size());
    for (const DuplicateConst& duplicate : duplicates) {
        if (duplicate.duplicate == nullptr ||
            duplicate.canonical == nullptr ||
            duplicate.duplicate->result == duplicate.canonical->result) {
            continue;
        }

        replacements[duplicate.duplicate->result.value()] = duplicate.canonical->result;
    }

    std::vector<ConstInst*> canonical_constants = canonical_hoist_order(duplicates);
    rewrite_uses(unit, replacements);
    update_value_table(unit, duplicates, canonical_constants);
    (void)move_entry_block_to_front(unit);
    rewrite_const_layout(unit, duplicates, canonical_constants);

    result.changed = true;
    return result;
}

} // namespace baltam
