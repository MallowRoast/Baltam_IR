#pragma once

#include "runtime/code_object.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace baltam {

inline constexpr std::size_t kMaxCallDepth = 1024;

class InterpreterContext;

struct DynamicBindings {
    std::unordered_map<InternedString, ba_obj_ptr> values;
};

struct ClosureEnvironment {
    std::unordered_map<SlotId, ba_obj_ptr> values;

    [[nodiscard]] ba_obj_ptr find(SlotId slot) const;
};

struct ClosureObject {
    std::shared_ptr<CodeObject> code;
    std::shared_ptr<ClosureEnvironment> environment;
};

struct RuntimeFrame {
    InterpreterContext* context = nullptr;
    CodeObject* code = nullptr;
    RuntimeFrame* caller = nullptr;

    BasicBlock* block = nullptr;
    std::uint32_t instruction_index = 0;

    std::vector<ba_obj_ptr> slot_values;
    std::vector<ba_obj_ptr> temporaries;

    std::uint32_t actual_nargin = 0;
    std::uint32_t requested_nargout = 0;

    std::shared_ptr<ClosureEnvironment> closure;
    std::unique_ptr<DynamicBindings> dynamic_bindings;

    void initialize_storage();
    [[nodiscard]] ba_obj_ptr& slot_value(SlotId slot);
    [[nodiscard]] const ba_obj_ptr& slot_value(SlotId slot) const;
    [[nodiscard]] ba_obj_ptr& temporary(ValueId value);
    [[nodiscard]] const ba_obj_ptr& temporary(ValueId value) const;
};

class FrameScope final {
public:
    FrameScope(InterpreterContext& context, RuntimeFrame& frame) noexcept;
    ~FrameScope();

    FrameScope(const FrameScope&) = delete;
    FrameScope& operator=(const FrameScope&) = delete;

private:
    InterpreterContext* context_ = nullptr;
    RuntimeFrame* previous_ = nullptr;
};

[[nodiscard]] std::size_t call_depth(const RuntimeFrame* frame) noexcept;
[[nodiscard]] bool exceeds_max_call_depth(const RuntimeFrame* caller) noexcept;
[[nodiscard]] bool frame_chain_contains(
    const RuntimeFrame* frame,
    const CodeObject* code) noexcept;

} // namespace baltam
