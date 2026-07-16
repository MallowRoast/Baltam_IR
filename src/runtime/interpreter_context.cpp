#include "runtime/interpreter_context.h"

#include <utility>

namespace baltam {

ba_obj_ptr& BaseWorkspace::find(InternedString name) {
    return values[std::move(name)];
}

ba_obj_ptr BaseWorkspace::find(InternedString name) const {
    const auto it = values.find(name);
    return it != values.end() ? it->second : ba_obj_ptr{};
}

void BaseWorkspace::clear(InternedString name) {
    values[std::move(name)].reset();
}

ba_obj_ptr& GlobalRegistry::find(InternedString name) {
    return values[std::move(name)];
}

ba_obj_ptr GlobalRegistry::find(InternedString name) const {
    const auto it = values.find(name);
    return it != values.end() ? it->second : ba_obj_ptr{};
}

void GlobalRegistry::clear(InternedString name) {
    values[std::move(name)].reset();
}

} // namespace baltam
