#include "ir/ir_unit.h"

#include <functional>
#include <memory>
#include <stdexcept>
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

    FunctionUnit parent("outer", {}, {});
    FunctionUnit shared_slot("shared", {"x", "y"}, {"x", "z"});
    require(shared_slot.parameter_slots() ==
                    std::vector<SlotId>{SlotId{0}, SlotId{1}} &&
                shared_slot.return_slots() ==
                    std::vector<SlotId>{SlotId{0}, SlotId{2}} &&
                shared_slot.slot_table().size() == 3 &&
                shared_slot.slot_table().find(SlotId{0})->name == "x" &&
                shared_slot.slot_table().find(SlotId{0})->tag == SlotTag::Local,
            "同名入参与出参未复用槽位");
    BasicBlock& block = parent.add_block(IRStage::NonSSA);
    auto& runtime = block.add_instruction<CreateNamedFunctionHandleInst>(
        "sin", ValueId{0});
    auto& builtin = block.add_instruction<CreateNamedFunctionHandleInst>(
        "sin", ValueId{1},
        CreateNamedFunctionHandleInst::ResolutionMode::Static,
        DispatchType::Builtin);
    auto& nested = block.add_instruction<CreateNamedFunctionHandleInst>(
        "outer", ValueId{2},
        CreateNamedFunctionHandleInst::ResolutionMode::Static,
        DispatchType::MFunction, &parent);
    require(runtime.type() == Instruction::Type::CreateNamedFunctionHandle &&
                runtime.name == "sin" &&
                runtime.resolution_mode ==
                    CreateNamedFunctionHandleInst::ResolutionMode::Runtime &&
                runtime.bound_dispatch_type == DispatchType::Dynamic &&
                runtime.m_function_target == nullptr &&
                builtin.bound_dispatch_type == DispatchType::Builtin &&
                builtin.m_function_target == nullptr &&
                nested.bound_dispatch_type == DispatchType::MFunction &&
                nested.m_function_target == &parent,
            "具名函数句柄没有保留解析模式或目标");

    const Slot param{SlotId{0}, SlotTag::Local};
    const Slot capture_slot{SlotId{1}, SlotTag::Capture};
    auto target = std::make_shared<AnonymousFunctionUnit>(
        &parent, std::vector<Slot>{param}, std::vector<Slot>{capture_slot});
    BasicBlock& body = target->add_block(IRStage::NonSSA);
    body.add_instruction<ReturnInst>(std::vector<ValueId>{ValueId{4}});
    auto& handle = block.add_instruction<CreateAnonymousFunctionHandleInst>(
        target,
        std::vector<CreateAnonymousFunctionHandleInst::CaptureValue>{
            {"y", Slot{SlotId{2}, SlotTag::Local}, ValueId{3}}},
        ValueId{5});
    std::weak_ptr<AnonymousFunctionUnit> weak_target = target;
    target.reset();
    require(handle.type() == Instruction::Type::CreateAnonymousFunctionHandle &&
                handle.target == weak_target.lock() &&
                handle.target->lexical_parent() == &parent &&
                handle.target->param_slots().front().id == SlotId{0} &&
                handle.target->capture_slots().front().tag == SlotTag::Capture &&
                handle.target->non_ssa_blocks().front().get() == &body &&
                body.terminator()->type() == Instruction::Type::Return &&
                handle.captures.front().source_slot.id == SlotId{2} &&
                handle.captures.front().captured_value == ValueId{3} &&
                handle.result == ValueId{5},
            "匿名函数体、捕获值或结果没有正确保存");

    require_invalid([&] {
        block.add_instruction<CreateNamedFunctionHandleInst>(
            "sin", ValueId{6},
            CreateNamedFunctionHandleInst::ResolutionMode::Static,
            DispatchType::MFunction);
    }, "静态 M 函数句柄未拒绝缺少目标");
    require_invalid([&] {
        block.add_instruction<CreateNamedFunctionHandleInst>(
            "sin", ValueId{6},
            CreateNamedFunctionHandleInst::ResolutionMode::Runtime,
            DispatchType::Builtin);
    }, "运行时解析句柄未拒绝预先绑定的内置目标");
    require_invalid([&] {
        block.add_instruction<CreateAnonymousFunctionHandleInst>(
            weak_target.lock(),
            std::vector<CreateAnonymousFunctionHandleInst::CaptureValue>{},
            ValueId{6});
    }, "匿名函数句柄未拒绝捕获值数量不匹配");
    require_invalid([&] {
        AnonymousFunctionUnit invalid(nullptr, {}, {});
    }, "匿名函数体未拒绝空词法父单元");
}
