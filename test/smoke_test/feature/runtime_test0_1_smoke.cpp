#include "runtime/ir_executor.h"
#include "runtime/interpreter_context.h"

#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace baltam {
namespace {

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

void require(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

Slot add_slot(FunctionUnit& unit, SlotTag tag, InternedString name) {
    Slot slot{SlotId(static_cast<SlotId::underlying_type>(unit.slot_table.slots.size())), tag};
    unit.slot_table.slots.push_back(
        SlotInfo{slot, std::move(name), SourceSpan::invalid(), SlotValueType::Unknown});
    return slot;
}

ValueId add_value(FunctionUnit& unit) {
    ValueId value(static_cast<ValueId::underlying_type>(unit.value_table.values.size()));
    unit.value_table.values.push_back(ValueInfo{value, 0, TypeFact{}, nullptr});
    return value;
}

template <typename Inst>
Inst& append(BasicBlock& block, std::unique_ptr<Inst> inst) {
    inst->parent = &block;
    Inst& ref = *inst;
    block.instructions.push_back(std::move(inst));
    return ref;
}

std::shared_ptr<CodeObject> build_test0_1_code() {
    auto module = std::make_shared<IRModule>();
    auto file = std::make_unique<MFileUnit>();
    auto function = std::make_unique<FunctionUnit>();

    file->module = module.get();
    function->file = file.get();
    function->name = "test0_1";

    FunctionUnit* unit = function.get();
    CodeUnit* entry_unit = unit;

    const Slot c = add_slot(*unit, SlotTag::Ret, "c");
    const Slot a = add_slot(*unit, SlotTag::Local, "a");
    const Slot b = add_slot(*unit, SlotTag::Local, "b");
    unit->return_slots.push_back(c);

    ValueId v0 = add_value(*unit);
    ValueId v1 = add_value(*unit);
    ValueId v2 = add_value(*unit);
    ValueId v3 = add_value(*unit);
    ValueId v4 = add_value(*unit);
    ValueId v5 = add_value(*unit);
    ValueId v6 = add_value(*unit);
    ValueId v7 = add_value(*unit);
    ValueId v8 = add_value(*unit);
    ValueId v9 = add_value(*unit);
    ValueId v10 = add_value(*unit);
    ValueId v11 = add_value(*unit);
    ValueId v12 = add_value(*unit);

    BasicBlock* entry = unit->create_block("entry", SourceSpan::invalid());
    BasicBlock* then_block = unit->create_block("if.then", SourceSpan::invalid());
    BasicBlock* else_block = unit->create_block("if.else", SourceSpan::invalid());
    BasicBlock* exit_block = unit->create_block("if.exit", SourceSpan::invalid());
    require(unit->set_entry_block(entry), "entry block must belong to test0_1 unit");

    {
        auto inst = std::make_unique<ConstInst>();
        inst->result = v0;
        inst->value = Float64Constant{1.0};
        append(*entry, std::move(inst));
    }
    {
        auto inst = std::make_unique<ConstInst>();
        inst->result = v1;
        inst->value = Float64Constant{2.0};
        append(*entry, std::move(inst));
    }
    {
        auto inst = std::make_unique<BinaryInst>();
        inst->result = v2;
        inst->op = Add;
        inst->lhs = v0;
        inst->rhs = v1;
        append(*entry, std::move(inst));
    }
    {
        auto inst = std::make_unique<StoreSlotInst>();
        inst->slot = a;
        inst->value = v2;
        append(*entry, std::move(inst));
    }
    {
        auto inst = std::make_unique<LoadSlotInst>();
        inst->result = v3;
        inst->slot = a;
        append(*entry, std::move(inst));
    }
    {
        auto inst = std::make_unique<CallInst>();
        inst->results.push_back(v4);
        inst->callee_kind = CallInst::Direct;
        inst->callee = InternedString("sin");
        inst->arguments.push_back(v3);
        append(*entry, std::move(inst));
    }
    {
        auto inst = std::make_unique<StoreSlotInst>();
        inst->slot = b;
        inst->value = v4;
        append(*entry, std::move(inst));
    }
    {
        auto inst = std::make_unique<LoadSlotInst>();
        inst->result = v5;
        inst->slot = b;
        append(*entry, std::move(inst));
    }
    {
        auto inst = std::make_unique<ConstInst>();
        inst->result = v6;
        inst->value = Float64Constant{0.0};
        append(*entry, std::move(inst));
    }
    {
        auto inst = std::make_unique<BinaryInst>();
        inst->result = v7;
        inst->op = Gt;
        inst->lhs = v5;
        inst->rhs = v6;
        append(*entry, std::move(inst));
    }
    {
        auto inst = std::make_unique<BranchInst>();
        inst->condition = v7;
        inst->true_target = then_block;
        inst->false_target = else_block;
        append(*entry, std::move(inst));
    }

    {
        auto inst = std::make_unique<LoadSlotInst>();
        inst->result = v8;
        inst->slot = b;
        append(*then_block, std::move(inst));
    }
    {
        auto inst = std::make_unique<ConstInst>();
        inst->result = v9;
        inst->value = Float64Constant{2.0};
        append(*then_block, std::move(inst));
    }
    {
        auto inst = std::make_unique<BinaryInst>();
        inst->result = v10;
        inst->op = Mul;
        inst->lhs = v8;
        inst->rhs = v9;
        append(*then_block, std::move(inst));
    }
    {
        auto inst = std::make_unique<StoreSlotInst>();
        inst->slot = c;
        inst->value = v10;
        append(*then_block, std::move(inst));
    }
    {
        auto inst = std::make_unique<GotoInst>();
        inst->target = exit_block;
        append(*then_block, std::move(inst));
    }

    {
        auto inst = std::make_unique<ConstInst>();
        inst->result = v11;
        inst->value = Float64Constant{0.0};
        append(*else_block, std::move(inst));
    }
    {
        auto inst = std::make_unique<StoreSlotInst>();
        inst->slot = c;
        inst->value = v11;
        append(*else_block, std::move(inst));
    }
    {
        auto inst = std::make_unique<GotoInst>();
        inst->target = exit_block;
        append(*else_block, std::move(inst));
    }

    {
        auto inst = std::make_unique<LoadSlotInst>();
        inst->result = v12;
        inst->slot = c;
        append(*exit_block, std::move(inst));
    }
    {
        auto inst = std::make_unique<ReturnInst>();
        inst->values.push_back(v12);
        append(*exit_block, std::move(inst));
    }

    file->entry_unit = entry_unit;
    file->code_units.push_back(std::move(function));
    module->files.push_back(std::move(file));

    auto code = std::make_shared<CodeObject>();
    code->ir_owner = std::move(module);
    code->unit = entry_unit;
    return code;
}

} // namespace
} // namespace baltam

int main() {
    try {
        baltam::InterpreterContext context;
        std::shared_ptr<baltam::CodeObject> code = baltam::build_test0_1_code();

        baltam::RuntimeFrame frame;
        frame.context = &context;
        frame.code = code.get();
        frame.requested_nargout = 1;
        frame.initialize_storage();

        baltam::FrameScope scope(context, frame);
        const std::vector<baltam::ba_obj_ptr> outputs = baltam::execute_frame(frame);
        baltam::require(outputs.size() == 1, "test0_1 runtime should return one output");
        baltam::require(outputs.front() != nullptr, "test0_1 output must be bound");

        const double actual = outputs.front()->as_double();
        const double expected = std::sin(3.0) * 2.0;
        baltam::require(
            std::abs(actual - expected) < 1e-12,
            "test0_1 runtime output mismatch");
    } catch (const std::exception& ex) {
        std::cerr << "runtime_test0_1_smoke failed: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
