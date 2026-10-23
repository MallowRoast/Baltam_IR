#include "ir/ir_unit.h"

#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_throws(const std::function<void()>& action, const char* message) {
    try {
        action();
    } catch (const std::logic_error&) {
        return;
    }
    throw std::runtime_error(message);
}

} // 匿名命名空间

int main() {
    using namespace baltam;

    FunctionUnit function("f", {}, {});
    FunctionUnit other("g", {}, {});
    const SlotId local = function.slot_table().add_slot("x", SlotTag::Local);
    const SlotId global = function.slot_table().add_slot("x", SlotTag::Global);
    const SlotId other_local = other.slot_table().add_slot("x", SlotTag::Local);
    const SlotId largest_slot_id{std::numeric_limits<std::uint32_t>::max()};
    require(local == SlotId{0} && global == SlotId{1} &&
                other_local == SlotId{0} &&
                function.slot_table().find(global)->tag == SlotTag::Global &&
                function.slot_table().find(SlotId{2}) == nullptr &&
                function.slot_table().find(largest_slot_id) == nullptr,
            "槽位编号或代码单元归属不正确");

    auto ssa = function.stage_version(IRStage::SSA);
    auto& block = function.add_block(IRStage::SSA);
    const ValueId first = ssa->values().reserve_results(2);
    auto& call = block.add_instruction<CallInst>(
        std::vector<ValueId>{first, ValueId{first.index + 1}},
        InternedString{"target"}, std::vector<ValueId>{});
    ssa->values().bind(first, call, 0);
    ssa->values().bind(ValueId{first.index + 1}, call, 1);
    require(ssa->values().all_defined() &&
                ssa->values().find(first)->definition == &call &&
                ssa->values().find(ValueId{first.index + 1})->result_index == 1 &&
                ssa->values().find(ValueId{2}) == nullptr &&
                ssa->values().find(ValueId{std::numeric_limits<std::uint32_t>::max()}) == nullptr,
            "多结果指令的值表绑定不正确");
    require_throws([&] { ssa->values().bind(first, call); },
                   "值编号重复绑定未报错");
    require_throws([&] {
        ssa->values().bind(ValueId{std::numeric_limits<std::uint32_t>::max()}, call);
    }, "越界值编号未报错");

    const ValueId unfinished = ssa->values().reserve_results();
    require(!ssa->values().all_defined() &&
                ssa->values().find(unfinished) == nullptr,
            "未绑定的预留编号不应返回定义");
    require_throws([&] { ssa->values().discard_last(first, 2); },
                   "已经绑定的编号不应被撤销");
    ssa->values().discard_last(unfinished);
    require(ssa->values().all_defined() && ssa->values().size() == 2,
            "撤销预留编号后值表状态错误");
    const ValueId failed = ssa->values().reserve_results();
    require_throws([&] {
        block.add_instruction<MagicEndInst>(
            std::vector<MagicEndInst::MagicEndContext>{}, failed);
    }, "无效指令构造未报错");
    ssa->values().discard_last(failed);
    require(ssa->values().size() == 2 && ssa->values().all_defined(),
            "指令构造失败后预留编号未正确撤销");

    auto replacement = std::make_shared<IRStageVersion>(IRStage::SSA);
    const ValueId new_id = replacement->values().reserve_results();
    require_throws([&] { function.replace_stage_version(IRStage::SSA, replacement); },
                   "未绑定的阶段版本不应发布");
    auto& new_block = replacement->add_block();
    auto& new_load = new_block.add_instruction<LoadSlotInst>(new_id, local);
    replacement->values().bind(new_id, new_load);
    function.replace_stage_version(IRStage::SSA, replacement);
    require(function.stage_version(IRStage::SSA).get() == replacement.get() &&
                function.stage_version(IRStage::SSA)->values().find(new_id)->definition ==
                    &new_load &&
                ssa->values().find(first)->definition == &call &&
                ssa->blocks().front().get() == &block &&
                function.slot_table().find(new_load.slot)->tag == SlotTag::Local,
            "替换阶段版本后旧基本块和值表未共同存活");
    require_throws([&] {
        function.replace_stage_version(IRStage::NonSSA,
                                       std::make_shared<IRStageVersion>(IRStage::NonSSA));
    }, "Non-SSA 阶段不应替换");
    return 0;
}
