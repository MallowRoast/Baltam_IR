#include "ir/ir_unit.h"

#include <functional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_invalid(const std::function<void()>& action, const char* message) {
    try {
        action();
    } catch (const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error(message);
}

} // 匿名命名空间

int main() {
    using namespace baltam;

    CommandUnit unit;
    BasicBlock& block = unit.add_block();
    const std::vector<Operand> operands{ValueId{1}, InternedString{"A"}};
    require(std::holds_alternative<ValueId>(operands[0]) &&
                std::holds_alternative<InternedString>(operands[1]) &&
                std::get<InternedString>(operands[1]) == "A",
            "操作数未区分已求值结果与未消歧名字");
    const std::vector<ValueId> indices{ValueId{1}, ValueId{2}};

    auto& apply = block.add_instruction<ApplyInst>(
        std::vector<ValueId>{ValueId{10}}, InternedString{"A"},
        std::vector<ValueId>{ValueId{1}, ValueId{2}});
    auto& value_apply = block.add_instruction<ValueApplyInst>(
        std::vector<ValueId>{ValueId{11}},
        std::vector<ValueId>{ValueId{1}, ValueId{2}}, ValueId{0});
    auto& call = block.add_instruction<CallInst>(
        std::vector<ValueId>{ValueId{12}}, InternedString{"f"},
        std::vector<ValueId>{ValueId{1}, ValueId{2}});
    auto& paren = block.add_instruction<ParenReferenceInst>(
        std::vector<ValueId>{ValueId{13}, ValueId{14}}, indices, ValueId{0});
    auto& brace = block.add_instruction<BraceReferenceInst>(
        std::vector<ValueId>{ValueId{15}, ValueId{16}}, indices, ValueId{0});
    auto& dot_access = block.add_instruction<DotAccessInst>(
        std::vector<ValueId>{ValueId{17}}, InternedString{"a"}, "b");
    auto& dot = block.add_instruction<DotReferenceInst>(
        std::vector<ValueId>{ValueId{18}, ValueId{19}}, "b", ValueId{0});
    auto& paren_assign = block.add_instruction<ParenAssignInst>(
        indices, ValueId{20}, ValueId{0}, ValueId{3});
    auto& paren_delete = block.add_instruction<ParenDeleteInst>(
        indices, ValueId{21}, ValueId{0});
    auto& brace_assign = block.add_instruction<BraceAssignInst>(
        indices, std::vector<ValueId>{ValueId{3}, ValueId{4}},
        ValueId{22}, ValueId{0});
    auto& dot_assign = block.add_instruction<DotAssignInst>(
        "b", ValueId{23}, ValueId{0}, ValueId{3});

    require(apply.type() == Instruction::Type::Apply &&
                std::get<InternedString>(apply.callee_or_base) == "A" &&
                apply.arguments == std::vector<ValueId>{ValueId{1}, ValueId{2}} &&
                value_apply.type() == Instruction::Type::ValueApply &&
                value_apply.base == ValueId{0} &&
                value_apply.arguments ==
                    std::vector<ValueId>{ValueId{1}, ValueId{2}} &&
                call.type() == Instruction::Type::Call &&
                call.arguments ==
                    std::vector<ValueId>{ValueId{1}, ValueId{2}} &&
                call.callee == "f" &&
                call.dispatch_type == DispatchType::Dynamic &&
                call.m_function_target == nullptr,
            "圆括号消歧或调用指令的字段不正确");
    require(paren.type() == Instruction::Type::ParenReference &&
                paren.results.size() == 2 &&
                paren.indices.front() == ValueId{1} &&
                brace.type() == Instruction::Type::BraceReference &&
                brace.results.size() == 2 &&
                brace.indices == indices &&
                dot_access.type() == Instruction::Type::DotAccess &&
                std::get<InternedString>(dot_access.base_or_name) == "a" &&
                dot.type() == Instruction::Type::DotReference &&
                dot.member == "b" && dot.results.size() == 2,
            "索引或点号取值指令的字段不正确");
    BraceReferenceInst pack_reference(
        std::vector<ValueId>{ValueId{0}},
        std::vector<ValueId>{ValueId{1}}, ValueId{2});
    LoadSlotInst scalar_reference(ValueId{1}, SlotId{0});
    ValueTable value_table;
    const ValueId first_result = value_table.reserve_results(2);
    value_table.bind(first_result, pack_reference, 0, ValueInfo::Form::MaybePack);
    value_table.bind(ValueId{1}, scalar_reference);
    require(value_table.find(first_result)->form == ValueInfo::Form::MaybePack &&
                value_table.find(ValueId{1})->form == ValueInfo::Form::Single,
            "值表未区分可能的值包与普通单值");
    const ValueId literal_result = value_table.reserve_results();
    ConstInst literal({}, literal_result);
    value_table.bind(literal_result, literal);
    const ValueId colon_result = value_table.reserve_results();
    MagicColonInst magic_colon(colon_result);
    value_table.bind(colon_result, magic_colon);
    require(value_table.find(literal_result)->constant_kind ==
                ValueInfo::ConstantKind::Object &&
                value_table.find(colon_result)->constant_kind ==
                    ValueInfo::ConstantKind::IndexColon &&
                value_table.find(ValueId{1})->constant_kind ==
                    ValueInfo::ConstantKind::Unknown,
            "值表未区分普通常量、索引冒号和未知值");
    require(paren_assign.type() == Instruction::Type::ParenAssign &&
                paren_assign.updated_base == ValueId{20} &&
                paren_assign.value == ValueId{3} &&
                paren_delete.type() == Instruction::Type::ParenDelete &&
                paren_delete.updated_base == ValueId{21} &&
                brace_assign.type() == Instruction::Type::BraceAssign &&
                brace_assign.values.size() == 2 &&
                brace_assign.updated_base == ValueId{22} &&
                dot_assign.type() == Instruction::Type::DotAssign &&
                dot_assign.member == "b" &&
                dot_assign.updated_base == ValueId{23} &&
                block.instructions().size() == 11 &&
                block.terminator() == nullptr,
            "索引或成员赋值指令的字段不正确");

    // 空数组右值仍由花括号赋值携带；是否删除由操作种类决定。
    auto& empty_cell_value = block.add_instruction<BraceAssignInst>(
        indices, std::vector<ValueId>{ValueId{5}}, ValueId{24}, ValueId{0});
    require(empty_cell_value.type() == Instruction::Type::BraceAssign &&
                empty_cell_value.values.front() == ValueId{5},
            "花括号空数组赋值不应变成圆括号删除");

    require_invalid([&] {
        block.add_instruction<DotAccessInst>(
            std::vector<ValueId>{ValueId{25}}, InternedString{"a"}, "");
    }, "点号访问未拒绝空成员名");
}
