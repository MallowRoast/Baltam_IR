#pragma once

#include "runtime/frame.h"

#include "core.h"

#include <cstdint>

namespace baltam {

enum class RuntimeFunctionKind : std::uint8_t {
    NotFound,
    LocalMFunction,
    MFunctionFile,
    ScriptFile,
    ClassFile,
    InternalFunction,
    BuiltinFunction,
    PluginFunction,
    ToolboxFunction,
};

struct RuntimeFunctionLookup {
    RuntimeFunctionKind kind = RuntimeFunctionKind::NotFound;
    DispatchType dispatch_type = Dynamic;
    InternedString name;
    baFunPtr entry_point = nullptr;
    FunctionUnit* m_function_target = nullptr;
    NormalizedPath source_file;

    [[nodiscard]] bool found() const noexcept {
        return kind != RuntimeFunctionKind::NotFound;
    }

    [[nodiscard]] bool callable() const noexcept {
        return entry_point != nullptr || m_function_target != nullptr;
    }
};

[[nodiscard]] RuntimeFunctionLookup lookup_function(
    const RuntimeFrame& frame,
    const InternedString& name);

[[nodiscard]] RuntimeFunctionLookup lookup_function(
    const RuntimeFrame& frame,
    const InternedString& name,
    DispatchType dispatch_type);

} // namespace baltam
