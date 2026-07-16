#include "runtime/lookup.h"

#include "runtime/interpreter_context.h"

#include "baltam_worker/builtin_manager.h"
#include "extension/extension.h"

#include <mutex>
#include <utility>

namespace baltam {
namespace {

[[nodiscard]] MFileUnit* owning_file(CodeUnit* unit) noexcept {
    if (unit == nullptr) {
        return nullptr;
    }

    switch (unit->type()) {
        case CodeUnit::Script:
            return static_cast<ScriptUnit*>(unit)->file;
        case CodeUnit::Function:
            return static_cast<FunctionUnit*>(unit)->file;
        case CodeUnit::AnonymousFunction:
            return owning_file(static_cast<AnonymousFunctionUnit*>(unit)->lexical_parent);
        case CodeUnit::Command:
            return nullptr;
    }

    return nullptr;
}

[[nodiscard]] RuntimeFunctionLookup make_function_lookup(
    RuntimeFunctionKind kind,
    DispatchType dispatch_type,
    InternedString name) {
    RuntimeFunctionLookup result;
    result.kind = kind;
    result.dispatch_type = dispatch_type;
    result.name = std::move(name);
    return result;
}

void ensure_builtin_library_loaded() {
    static std::once_flag once;
    std::call_once(once, [] {
        load_builtin_library();
    });
}

[[nodiscard]] RuntimeFunctionLookup lookup_local_m_function(
    const RuntimeFrame& frame,
    const InternedString& name) {
    MFileUnit* file = owning_file(frame.code->unit);
    if (file == nullptr) {
        return {};
    }

    FunctionUnit* function = file->find_local_function(name);
    if (function == nullptr) {
        return {};
    }

    RuntimeFunctionLookup result =
        make_function_lookup(RuntimeFunctionKind::LocalMFunction, MFunction, name);
    result.m_function_target = function;
    result.source_file = file->path;
    return result;
}

[[nodiscard]] baFunPtr lookup_plugin_call_entry_point() {
    ensure_builtin_library_loaded();

    baFunPtr entry_point = nullptr;
    (void)lookup_builtin_function("plugin_call", entry_point);
    return entry_point;
}

[[nodiscard]] RuntimeFunctionLookup lookup_plugin_function(const InternedString& name) {
    if (Plugin::get_function_by_name(name) == nullptr) {
        return {};
    }

    RuntimeFunctionLookup result =
        make_function_lookup(RuntimeFunctionKind::PluginFunction, Builtin, name);
    result.entry_point = lookup_plugin_call_entry_point();
    return result;
}

[[nodiscard]] RuntimeFunctionLookup lookup_toolbox_function(const InternedString& name) {
    if (Toolbox::get_function_by_name(name) == nullptr) {
        return {};
    }

    RuntimeFunctionLookup result =
        make_function_lookup(RuntimeFunctionKind::ToolboxFunction, Builtin, name);
    result.entry_point = lookup_plugin_call_entry_point();
    return result;
}

[[nodiscard]] RuntimeFunctionLookup lookup_internal_registered_function(
    const InternedString& name) {
    baFunPtr entry_point = lookup_internal_function(name.c_str());
    if (entry_point == nullptr) {
        return {};
    }

    RuntimeFunctionLookup result =
        make_function_lookup(RuntimeFunctionKind::InternalFunction, Internal, name);
    result.entry_point = entry_point;
    return result;
}

[[nodiscard]] RuntimeFunctionLookup lookup_builtin_registered_function(
    const InternedString& name) {
    ensure_builtin_library_loaded();

    baFunPtr entry_point = nullptr;
    if (!lookup_builtin_function(name, entry_point)) {
        return {};
    }

    RuntimeFunctionLookup result =
        make_function_lookup(RuntimeFunctionKind::BuiltinFunction, Builtin, name);
    result.entry_point = entry_point;
    return result;
}

[[nodiscard]] RuntimeFunctionLookup lookup_dynamic_function(
    const RuntimeFrame& frame,
    const InternedString& name) {
    // MATLAB 函数优先级第 1 项是变量。变量/slot 已由 executor 在调用本函数前处理。
    // 第 2 项：显式 import，当前 IR/runtime 尚未记录 import 表，暂不支持。
    // 第 3 项：nested function，当前 IR 尚未建模嵌套函数表，暂不支持。
    // 第 4 项：local function。
    if (RuntimeFunctionLookup result = lookup_local_m_function(frame, name); result.found()) {
        return result;
    }

    // 第 5 项：wildcard import，当前 IR/runtime 尚未记录 import * 表，暂不支持。
    // 第 6 项：private function，当前 runtime 尚未实现 private 目录查找，暂不支持。
    // 第 7 项：object function / constructor，需要结合实参类型分派，暂不支持。
    // 第 8 项：已加载的 Simulink model，暂不支持。
    // 第 9/10 项：current folder / path function。该层需要 worker PathLoader 初始化；
    // standalone runtime 当前不启用，避免在未初始化 worker runtime 时触发不稳定依赖。

    // 运行时注册函数不属于 MATLAB 文档里的独立优先级层级；当前作为 path 查找
    // 尚未接入前的内建/扩展函数兜底。
    if (RuntimeFunctionLookup result = lookup_plugin_function(name); result.found()) {
        return result;
    }
    if (RuntimeFunctionLookup result = lookup_toolbox_function(name); result.found()) {
        return result;
    }
    if (RuntimeFunctionLookup result = lookup_internal_registered_function(name);
        result.found()) {
        return result;
    }
    if (RuntimeFunctionLookup result = lookup_builtin_registered_function(name); result.found()) {
        return result;
    }

    return {};
}

} // namespace

RuntimeFunctionLookup lookup_function(
    const RuntimeFrame& frame,
    const InternedString& name) {
    if (name.empty()) {
        return {};
    }
    return lookup_dynamic_function(frame, name);
}

RuntimeFunctionLookup lookup_function(
    const RuntimeFrame& frame,
    const InternedString& name,
    DispatchType dispatch_type) {
    if (name.empty()) {
        return {};
    }

    switch (dispatch_type) {
        case Dynamic:
            return lookup_dynamic_function(frame, name);
        case Builtin:
            return lookup_builtin_registered_function(name);
        case Internal:
            return lookup_internal_registered_function(name);
        case MFunction:
            // 当前 MFunction 显式分派只支持本文件 local function；PathLoader
            // 查找需要 worker runtime 初始化，暂不在这里启用。
            return lookup_local_m_function(frame, name);
    }

    return {};
}

} // namespace baltam
