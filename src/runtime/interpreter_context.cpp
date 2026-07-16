#include "runtime/interpreter_context.h"

namespace baltam {

ba_obj_ptr BaseWorkspace::find(InternedString name) const {
    const auto it = values.find(name);
    return it != values.end() ? it->second : ba_obj_ptr{};
}

void BaseWorkspace::clear(InternedString name) {
    const auto it = values.find(name);
    if (it != values.end()) {
        it->second.reset();
    }
}

ba_obj_ptr GlobalRegistry::find(InternedString name) const {
    const auto it = values.find(name);
    return it != values.end() ? it->second : ba_obj_ptr{};
}

void GlobalRegistry::clear(InternedString name) {
    const auto it = values.find(name);
    if (it != values.end()) {
        it->second.reset();
    }
}

} // namespace baltam
