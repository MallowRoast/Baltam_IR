#include "runtime/frame.h"

#include "runtime/interpreter_context.h"

#include <algorithm>
#include <stdexcept>
namespace baltam {
namespace {

template <typename Id>
std::size_t checked_index(Id id, std::size_t size, const char* kind) {
    if (!id.is_valid() || id.value() >= size) {
        throw std::out_of_range(kind);
    }
    return static_cast<std::size_t>(id.value());
}

} // namespace

ba_obj_ptr ClosureEnvironment::find(SlotId slot) const {
    const auto it = values.find(slot);
    return it != values.end() ? it->second : ba_obj_ptr{};
}

void RuntimeFrame::initialize_storage() {
    if (code == nullptr || code->unit == nullptr) {
        slot_values.clear();
        temporaries.clear();
        block = nullptr;
        instruction_index = 0;
        return;
    }

    slot_values.assign(code->unit->slot_table.slots.size(), nullptr);
    temporaries.assign(code->unit->value_table.values.size(), nullptr);
    block = code->unit->entry_block;
    instruction_index = 0;
}

ba_obj_ptr& RuntimeFrame::slot_value(SlotId slot) {
    return slot_values[checked_index(slot, slot_values.size(), "invalid slot id")];
}

const ba_obj_ptr& RuntimeFrame::slot_value(SlotId slot) const {
    return slot_values[checked_index(slot, slot_values.size(), "invalid slot id")];
}

ba_obj_ptr& RuntimeFrame::temporary(ValueId value) {
    return temporaries[checked_index(value, temporaries.size(), "invalid value id")];
}

const ba_obj_ptr& RuntimeFrame::temporary(ValueId value) const {
    return temporaries[checked_index(value, temporaries.size(), "invalid value id")];
}

FrameScope::FrameScope(InterpreterContext& context, RuntimeFrame& frame) noexcept
    : context_(&context), previous_(context.current_frame) {
    frame.context = &context;
    frame.caller = previous_;
    context.current_frame = &frame;
}

FrameScope::~FrameScope() {
    if (context_ != nullptr) {
        context_->current_frame = previous_;
        context_->code_cache.collect_retired(context_->current_frame);
        context_->anonymous_codes.collect_retired(context_->current_frame);
    }
}

std::size_t call_depth(const RuntimeFrame* frame) noexcept {
    std::size_t depth = 0;
    for (const RuntimeFrame* current = frame; current != nullptr; current = current->caller) {
        ++depth;
    }
    return depth;
}

bool exceeds_max_call_depth(const RuntimeFrame* caller) noexcept {
    return call_depth(caller) + 1U > kMaxCallDepth;
}

bool frame_chain_contains(const RuntimeFrame* frame, const CodeObject* code) noexcept {
    for (const RuntimeFrame* current = frame; current != nullptr; current = current->caller) {
        if (current->code == code) {
            return true;
        }
    }
    return false;
}

} // namespace baltam
