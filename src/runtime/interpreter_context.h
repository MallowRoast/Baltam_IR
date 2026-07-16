#pragma once

#include "runtime/code_object.h"

#include "ba_obj/ba_obj.h"

#include <atomic>
#include <unordered_map>

namespace baltam {

struct RuntimeFrame;

struct BaseWorkspace {
    std::unordered_map<InternedString, ba_obj_ptr> values;

    [[nodiscard]] ba_obj_ptr find(InternedString name) const;
    void clear(InternedString name);
};

struct GlobalRegistry {
    std::unordered_map<InternedString, ba_obj_ptr> values;

    [[nodiscard]] ba_obj_ptr find(InternedString name) const;
    void clear(InternedString name);
};

class InterpreterContext final {
public:
    BaseWorkspace base_workspace;
    GlobalRegistry globals;

    CodeObjectCache code_cache;
    AnonymousCodeTable anonymous_codes;

    RuntimeFrame* current_frame = nullptr;

    std::atomic_bool interrupt_requested{false};
};

} // namespace baltam
