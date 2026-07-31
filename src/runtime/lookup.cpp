#include "runtime/lookup.h"

#include "runtime/interpreter_context.h"

#include "baltam_worker/path.h"
#include "baltam_worker/builtin_manager.h"
#include "extension/extension.h"

#include <filesystem>
#include <mutex>
#include <optional>
#include <system_error>
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

[[nodiscard]] NormalizedPath normalize_lookup_path(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::path normalized = std::filesystem::weakly_canonical(path, ec);
    if (!ec) {
        return normalized.lexically_normal();
    }

    normalized = std::filesystem::absolute(path, ec);
    if (!ec) {
        return normalized.lexically_normal();
    }

    return path.lexically_normal();
}

[[nodiscard]] bool same_path(
    const std::filesystem::path& lhs,
    const std::filesystem::path& rhs) {
    if (lhs == rhs || lhs.lexically_normal() == rhs.lexically_normal()) {
        return true;
    }

    std::error_code ec;
    return std::filesystem::equivalent(lhs, rhs, ec) && !ec;
}

[[nodiscard]] std::optional<NormalizedPath> existing_m_file_path(
    const std::filesystem::path& path) {
    std::error_code ec;
    if (std::filesystem::is_regular_file(path, ec) && path.extension() == ".m") {
        return normalize_lookup_path(path);
    }
    return std::nullopt;
}

[[nodiscard]] RuntimeFunctionKind classify_cached_m_file(const MFileUnit* file) noexcept {
    if (file == nullptr) {
        return RuntimeFunctionKind::MFunctionFile;
    }
    if (file->is_script_file()) {
        return RuntimeFunctionKind::ScriptFile;
    }
    if (file->is_function_file()) {
        return RuntimeFunctionKind::MFunctionFile;
    }
    return RuntimeFunctionKind::MFunctionFile;
}

[[nodiscard]] MFileUnit* find_cached_m_file(
    const InterpreterContext* context,
    const NormalizedPath& path) {
    if (context == nullptr) {
        return nullptr;
    }

    if (const auto it = context->mfiles.find(path); it != context->mfiles.end()) {
        return it->second.file.get();
    }

    for (const auto& entry : context->mfiles) {
        if (same_path(entry.second.path, path)) {
            return entry.second.file.get();
        }
    }

    return nullptr;
}

[[nodiscard]] RuntimeFunctionLookup make_m_file_lookup(
    const RuntimeFrame& frame,
    const InternedString& name,
    NormalizedPath path) {
    MFileUnit* cached_file = find_cached_m_file(frame.context, path);
    RuntimeFunctionLookup result =
        make_function_lookup(classify_cached_m_file(cached_file), MFunction, name);
    result.source_file = std::move(path);
    if (cached_file != nullptr &&
        cached_file->is_function_file() &&
        cached_file->entry_unit != nullptr &&
        cached_file->entry_unit->name == name) {
        result.m_function_target = static_cast<FunctionUnit*>(cached_file->entry_unit);
    }
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
    if (frame.code == nullptr || frame.code->unit == nullptr) {
        return {};
    }

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

[[nodiscard]] RuntimeFunctionLookup lookup_m_file_in_current_directory(
    const RuntimeFrame& frame,
    const InternedString& name) {
    std::string pwd;
    if (!lookup_path("PWD", pwd) || pwd.empty()) {
        return {};
    }

    const std::filesystem::path candidate = std::filesystem::path(pwd) / (name + ".m");
    std::optional<NormalizedPath> path = existing_m_file_path(candidate);
    if (!path.has_value()) {
        return {};
    }
    return make_m_file_lookup(frame, name, std::move(*path));
}

[[nodiscard]] RuntimeFunctionLookup lookup_m_file_on_path(
    const RuntimeFrame& frame,
    const InternedString& name) {
    std::string resolved_path;
    if (!lookup_path(name, resolved_path) || resolved_path.empty()) {
        return {};
    }

    std::optional<NormalizedPath> path = existing_m_file_path(resolved_path);
    if (!path.has_value()) {
        path = existing_m_file_path(std::filesystem::path(resolved_path) / (name + ".m"));
    }
    if (!path.has_value()) {
        return {};
    }

    return make_m_file_lookup(frame, name, std::move(*path));
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
    baFunPtr entry_point = nullptr;
    if (!lookup_builtin_function(name, entry_point)) {
        ensure_builtin_library_loaded();
        if (!lookup_builtin_function(name, entry_point)) {
            return {};
        }
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
    // 第 9/10 项：current folder / path function。当前先解析到 `.m` 文件路径，
    // 由上层加载流程决定是否解析成 Script/Function 静态 IR 并解释执行。
    if (RuntimeFunctionLookup result =
            lookup_m_file_in_current_directory(frame, name);
        result.found()) {
        return result;
    }
    if (RuntimeFunctionLookup result = lookup_m_file_on_path(frame, name); result.found()) {
        return result;
    }

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
            if (RuntimeFunctionLookup result = lookup_local_m_function(frame, name);
                result.found()) {
                return result;
            }
            if (RuntimeFunctionLookup result =
                    lookup_m_file_in_current_directory(frame, name);
                result.found()) {
                return result;
            }
            return lookup_m_file_on_path(frame, name);
    }

    return {};
}

} // namespace baltam
