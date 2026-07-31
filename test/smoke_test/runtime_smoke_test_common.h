#pragma once

#include "ir/ir_lowering.h"
#include "runtime/code_object.h"
#include "runtime/interpreter_context.h"
#include "runtime/ir_executor.h"
#include "runtime/lookup.h"
#include "smoke_test_common.h"

#include "baltam_worker/builtin_manager.h"
#include "baltam_worker/path.h"

#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace baltam::smoke_test {

struct BuiltinCallCounts {
    int plus = 0;
    int sin = 0;
    int gt = 0;
    int mtimes = 0;
};

inline BuiltinCallCounts& builtin_call_counts() {
    static BuiltinCallCounts counts;
    return counts;
}

inline double require_double_arg(
    const std::vector<const_ba_obj_ptr>& inputs,
    std::size_t index,
    const char* name) {
    require(index < inputs.size(), name);
    require(inputs[index] != nullptr, name);
    return inputs[index]->as_double();
}

inline void store_double_output(
    std::vector<ba_obj_ptr>& outputs,
    double value,
    const char* name) {
    require(!outputs.empty(), name);
    outputs[0] = std::make_shared<ba_obj>(value);
}

inline void store_bool_output(
    std::vector<ba_obj_ptr>& outputs,
    bool value,
    const char* name) {
    require(!outputs.empty(), name);
    outputs[0] = std::make_shared<ba_obj>(value);
}

inline void builtin_plus(
    std::vector<const_ba_obj_ptr>& inputs,
    std::vector<ba_obj_ptr>& outputs) {
    ++builtin_call_counts().plus;
    store_double_output(
        outputs,
        require_double_arg(inputs, 0, "plus 缺少左参数") +
            require_double_arg(inputs, 1, "plus 缺少右参数"),
        "plus 缺少输出");
}

inline void builtin_sin(
    std::vector<const_ba_obj_ptr>& inputs,
    std::vector<ba_obj_ptr>& outputs) {
    ++builtin_call_counts().sin;
    store_double_output(
        outputs,
        std::sin(require_double_arg(inputs, 0, "sin 缺少参数")),
        "sin 缺少输出");
}

inline void builtin_gt(
    std::vector<const_ba_obj_ptr>& inputs,
    std::vector<ba_obj_ptr>& outputs) {
    ++builtin_call_counts().gt;
    store_bool_output(
        outputs,
        require_double_arg(inputs, 0, "gt 缺少左参数") >
            require_double_arg(inputs, 1, "gt 缺少右参数"),
        "gt 缺少输出");
}

inline void builtin_mtimes(
    std::vector<const_ba_obj_ptr>& inputs,
    std::vector<ba_obj_ptr>& outputs) {
    ++builtin_call_counts().mtimes;
    store_double_output(
        outputs,
        require_double_arg(inputs, 0, "mtimes 缺少左参数") *
            require_double_arg(inputs, 1, "mtimes 缺少右参数"),
        "mtimes 缺少输出");
}

class ScopedBuiltinDefinitions final {
public:
    ScopedBuiltinDefinitions() {
        builtin_call_counts() = {};
        register_builtin_function("plus", builtin_plus, "", 2, 1);
        register_builtin_function("sin", builtin_sin, "", 1, 1);
        register_builtin_function("gt", builtin_gt, "", 2, 1);
        register_builtin_function("mtimes", builtin_mtimes, "", 2, 1);

        baFunPtr plus_ptr = nullptr;
        require(lookup_builtin_function("plus", plus_ptr),
                "注册后应能立即 lookup plus builtin stub");
        require(plus_ptr == builtin_plus,
                "plus builtin registry 应指向 smoke stub");
    }

    ~ScopedBuiltinDefinitions() {
        unregister_builtin_function("plus");
        unregister_builtin_function("sin");
        unregister_builtin_function("gt");
        unregister_builtin_function("mtimes");
    }

    ScopedBuiltinDefinitions(const ScopedBuiltinDefinitions&) = delete;
    ScopedBuiltinDefinitions& operator=(const ScopedBuiltinDefinitions&) = delete;
};

inline NormalizedPath normalize_test_path(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::path normalized = std::filesystem::weakly_canonical(path, ec);
    if (!ec) {
        return normalized.lexically_normal();
    }
    return path.lexically_normal();
}

class ScopedWorkerPwd final {
public:
    explicit ScopedWorkerPwd(const std::filesystem::path& path) {
        had_previous_ = lookup_path("PWD", previous_);
        set_path("PWD", path.string());
    }

    ~ScopedWorkerPwd() {
        if (had_previous_) {
            set_path("PWD", previous_);
        }
    }

    ScopedWorkerPwd(const ScopedWorkerPwd&) = delete;
    ScopedWorkerPwd& operator=(const ScopedWorkerPwd&) = delete;

private:
    bool had_previous_ = false;
    std::string previous_;
};

inline RuntimeFunctionLookup lookup_from_command(
    InterpreterContext& context,
    std::string_view name) {
    CodeObject command_code;
    command_code.unit = context.command.get();

    RuntimeFrame command_frame;
    command_frame.context = &context;
    command_frame.code = &command_code;

    return lookup_function(command_frame, std::string(name));
}

inline MFileUnit* install_mfile_ir(InterpreterContext& context, IRBuildResult& result) {
    require(result.mfile != nullptr, "lowering 后 MFile IR 不能为空");
    require(result.mfile->entry_unit != nullptr, "MFile IR 需要入口单元");

    MFileCache cache;
    cache.path = normalize_test_path(result.mfile->path);
    result.mfile->path = cache.path;

    std::error_code ec;
    cache.mtime = std::filesystem::last_write_time(cache.path, ec);
    cache.file = std::move(result.mfile);

    const NormalizedPath key = cache.path;
    auto [it, inserted] = context.mfiles.insert_or_assign(key, std::move(cache));
    (void)inserted;

    for (auto& function : result.anonymous_functions) {
        if (function != nullptr) {
            context.anonymous_functions.push_back(std::move(function));
        }
    }
    result.anonymous_functions.clear();

    return it->second.file.get();
}

inline std::shared_ptr<CodeObject> make_code_object(
    InterpreterContext& context,
    MFileUnit& mfile) {
    require(mfile.entry_unit != nullptr, "CodeObject 需要入口单元");

    auto code = std::make_shared<CodeObject>();
    code->unit = mfile.entry_unit;
    context.code_cache.insert({mfile.path, code->unit->name}, code);
    return code;
}

inline void require_lookup_is_builtin(
    InterpreterContext& context,
    std::string_view name) {
    const RuntimeFunctionLookup lookup = lookup_from_command(context, name);
    require(lookup.found(), "应能找到 builtin 符号");
    require(lookup.kind == RuntimeFunctionKind::BuiltinFunction,
            "符号应解析为 builtin 定义");
    require(lookup.callable(), "builtin 定义应有可调用入口");
}

inline void require_call_count(std::string_view name, int actual, int expected) {
    if (actual != expected) {
        fail(
            std::string(name) + " 应通过 lookup 调用 " +
            std::to_string(expected) + " 次，实际 " + std::to_string(actual) + " 次");
    }
}

inline void require_base_workspace_double(
    const InterpreterContext& context,
    std::string_view name,
    double expected) {
    const auto it = context.base_workspace.values.find(std::string(name));
    require(it != context.base_workspace.values.end(),
            "命令执行后 base workspace 应包含目标变量");
    require(it->second != nullptr, "base workspace 变量不应为空");

    const double actual = it->second->as_double();
    require(
        std::abs(actual - expected) < 1e-12,
        "base workspace 变量值不符合预期");
}

inline void require_base_workspace_missing(
    const InterpreterContext& context,
    std::string_view name) {
    require(
        context.base_workspace.values.find(std::string(name)) ==
            context.base_workspace.values.end(),
        "目标变量不应写入 base workspace");
}

inline void execute_script(InterpreterContext& context, MFileUnit& mfile) {
    std::shared_ptr<CodeObject> code = make_code_object(context, mfile);

    RuntimeFrame base_frame;
    base_frame.context = &context;

    {
        FrameScope base_scope(context, base_frame);

        RuntimeFrame script_frame;
        script_frame.code = code.get();
        script_frame.requested_nargout = 0;
        script_frame.dynamic_bindings = std::make_unique<DynamicBindings>();
        script_frame.dynamic_bindings->values = context.base_workspace.values;
        script_frame.initialize_storage();

        {
            FrameScope script_scope(context, script_frame);
            const std::vector<ba_obj_ptr> outputs = execute_frame(script_frame);
            require(outputs.empty(), "脚本执行不应产生函数返回值");
        }

        context.base_workspace.values = std::move(script_frame.dynamic_bindings->values);
    }
}

inline void execute_command(InterpreterContext& context) {
    require(context.command != nullptr, "命令执行需要 CommandUnit");

    auto command_code = std::make_shared<CodeObject>();
    command_code->unit = context.command.get();

    RuntimeFrame base_frame;
    base_frame.context = &context;

    {
        FrameScope base_scope(context, base_frame);

        RuntimeFrame command_frame;
        command_frame.code = command_code.get();
        command_frame.requested_nargout = 0;
        command_frame.initialize_storage();

        {
            FrameScope command_scope(context, command_frame);
            const std::vector<ba_obj_ptr> outputs = execute_frame(command_frame);
            require(outputs.empty(), "CommandUnit 执行不应直接返回输出");
        }
    }
}

inline void require_default_runtime_builtin_counts() {
    const BuiltinCallCounts& counts = builtin_call_counts();
    require_call_count("plus", counts.plus, 1);
    require_call_count("sin", counts.sin, 1);
    require_call_count("gt", counts.gt, 1);
    require_call_count("mtimes", counts.mtimes, 1);
}

inline void require_default_runtime_builtins(InterpreterContext& context) {
    require_lookup_is_builtin(context, "plus");
    require_lookup_is_builtin(context, "sin");
    require_lookup_is_builtin(context, "gt");
    require_lookup_is_builtin(context, "mtimes");
}

} // namespace baltam::smoke_test
