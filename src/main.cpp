//
// Created by zj on 25-7-3.
//

#include <cstdlib>
#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "analysis/verifier.h"
#include "bt_ast_interface.h"
#include "interpreter/interpreter.h"
#include "ir/ir_printer.h"
#include "lowering/lowering.h"
#include "optimizer/construct_untyped_ssa.h"
#include "print/obj2str.h"

using namespace baltam;

namespace {

struct RunRequest {
    std::vector<std::string> script_paths;
    std::vector<std::string> entry_args;
};

std::string source_path_from_relative(std::string_view relative_path) {
    return std::string(BALTAM_IR_SOURCE_DIR) + std::string(relative_path);
}

void configure_runtime_library_path() {
    const std::string required_prefix =
        std::string("/opt/Baltamatica/lib:") + source_path_from_relative("/deps/core/lib");
    const char* existing = std::getenv("LD_LIBRARY_PATH");
    if (existing == nullptr || std::string(existing).empty()) {
        setenv("LD_LIBRARY_PATH", required_prefix.c_str(), 1);
        return;
    }

    const std::string current = existing;
    if (current.find(required_prefix) == 0) {
        return;
    }
    setenv("LD_LIBRARY_PATH", (required_prefix + ":" + current).c_str(), 1);
}

std::string resolve_script_argument(const std::string& arg) {
    if (arg == "simple_demo" || arg == "simple_demo.m") {
        return source_path_from_relative("/test/simple_demo.m");
    }
    else if (arg == "test1" || arg == "test1.m") {
        return source_path_from_relative("/test/test1/test1.m");
    }
    else if (arg == "test1_2" || arg == "test1_2.m") {
        return source_path_from_relative("/test/test1_2/test1_2.m");
    }
    else if (arg == "test1_3" || arg == "test1_3.m") {
        return source_path_from_relative("/test/test1_3/test1_3.m");
    }
    else if (arg == "test1_4" || arg == "test1_4.m") {
        return source_path_from_relative("/test/test1_4/test1_4.m");
    }
    else if (arg == "test1_5" || arg == "test1_5.m") {
        return source_path_from_relative("/test/test1_5/test1_5.m");
    }
    return arg;
}

std::string format_value(const interpreter::Value& value) {
    if (value.type == interpreter::Value::Undef) {
        return "undef";
    }
    if (value.object == nullptr) {
        return "<null>";
    }
    return internal::obj2str_one_line(*value.object);
}

std::vector<interpreter::Value::Object> make_entry_arguments(
    const std::vector<std::string>& entry_arg_texts) {
    std::vector<interpreter::Value::Object> args;
    args.reserve(entry_arg_texts.size());
    for (const std::string& text : entry_arg_texts) {
        args.push_back(std::make_shared<ba_obj>(text.c_str(), ba_char_mat));
    }
    return args;
}

void execute_and_print_untyped_ssa(std::ostream& os, Module& untyped_ssa_module,
                                   const std::vector<std::string>& entry_arg_texts = {}) {
    Function* entry_function = untyped_ssa_module.entry_function();
    if (entry_function == nullptr) {
        throw std::runtime_error("SSA 模块缺少入口函数，无法执行。");
    }

    const std::size_t expected_arg_count = entry_function->inputs().size();
    if (entry_arg_texts.empty() && expected_arg_count != 0) {
        os << "; 跳过执行：入口函数 `" << entry_function->name() << "` 需要 "
           << expected_arg_count << " 个参数；请使用 `--` 后追加 "
           << expected_arg_count << " 个字符串实参。" << std::endl;
        return;
    }
    if (entry_arg_texts.size() != expected_arg_count) {
        throw std::runtime_error("入口函数 `" + entry_function->name() + "` 需要 " +
                                 std::to_string(expected_arg_count) + " 个参数，但 main 收到 " +
                                 std::to_string(entry_arg_texts.size()) + " 个。");
    }

    const interpreter::ExecResult exec_result =
        interpreter::execute_function(*entry_function, make_entry_arguments(entry_arg_texts));
    os << "; untyped SSA execution result for `" << entry_function->name() << '`' << std::endl;
    if (exec_result.outputs.empty()) {
        os << "; <no outputs>" << std::endl;
        return;
    }

    const auto& output_names = entry_function->outputs();
    for (std::size_t i = 0; i < exec_result.outputs.size(); ++i) {
        std::string label = "out" + std::to_string(i + 1);
        if (i < output_names.size() && !output_names[i].name.empty()) {
            label = output_names[i].name;
        }
        os << label << " = " << format_value(exec_result.outputs[i]) << std::endl;
    }
}

RunRequest parse_run_request(int argc, char** argv) {
    RunRequest request;
    int separator_index = -1;
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--") {
            separator_index = i;
            break;
        }
    }

    if (separator_index < 0) {
        request.script_paths.reserve(static_cast<std::size_t>(std::max(argc - 1, 0)));
        for (int i = 1; i < argc; ++i) {
            request.script_paths.push_back(resolve_script_argument(argv[i]));
        }
        return request;
    }

    if (separator_index == 1) {
        throw std::runtime_error("`--` 前缺少脚本参数。");
    }
    if (separator_index != 2) {
        throw std::runtime_error("带入口参数执行时，main 当前只支持单个脚本。");
    }

    request.script_paths.push_back(resolve_script_argument(argv[1]));
    request.entry_args.reserve(static_cast<std::size_t>(argc - separator_index - 1));
    for (int i = separator_index + 1; i < argc; ++i) {
        request.entry_args.push_back(argv[i]);
    }
    return request;
}

int run_m_file(const std::string& script_path, const std::vector<std::string>& entry_arg_texts = {}) {
    int exit_code = 0;
    std::string msg;
    const auto parsed_units =
        bt_ast_interface::parse_mfile(script_path, ParserOpts{ParserOpts::DEFAULT}, msg);

    if (parsed_units.empty()) {
        if (!msg.empty()) {
            std::cerr << "解析失败: " << msg << std::endl;
        }
        std::cerr << "文件未生成 AST: " << script_path << std::endl;
        return 1;
    }

    try {
        Module non_ssa_module = lower_parsed_units_to_ir(parsed_units);
        analysis::verify_module_or_throw(non_ssa_module);
        Module untyped_ssa_module = optimizer::construct_untyped_ssa_module(non_ssa_module);
        analysis::verify_module_or_throw(untyped_ssa_module);
        print_ir(std::cout, non_ssa_module);
        print_ir(std::cout, untyped_ssa_module);
        execute_and_print_untyped_ssa(std::cout, untyped_ssa_module, entry_arg_texts);
        std::cout << std::endl;
    } catch (const std::exception& ex) {
        std::cerr << "文件的 IR 处理失败: " << script_path << "，原因: "
                  << ex.what()
                  << std::endl;
        exit_code = 1;
    }

    return exit_code;
}

}  // namespace

int main(int argc, char** argv) {
    int exit_code = 0;
    configure_runtime_library_path();
    const int init_ret = bt_ast_interface::initialize();
    if (init_ret != 0) {
        std::cerr << "bt_ast_interface::initialize 失败，返回码 = " << init_ret << std::endl;
        return 1;
    }

    RunRequest run_request;
    try {
        run_request = parse_run_request(argc, argv);
    } catch (const std::exception& ex) {
        std::cerr << "命令行参数错误: " << ex.what() << std::endl;
        bt_ast_interface::finalize();
        std::_Exit(1);
    }

    for (std::size_t i = 0; i < run_request.script_paths.size(); ++i) {
        if (i != 0) {
            std::cout << std::string(72, '=') << std::endl;
        }
        const std::vector<std::string>& entry_args =
            i == 0 ? run_request.entry_args : std::vector<std::string>{};
        exit_code = std::max(exit_code, run_m_file(run_request.script_paths[i], entry_args));
    }

    std::cout << std::flush;
    std::cerr << std::flush;

    bt_ast_interface::finalize();

    // The Baltam runtime leaves a joinable background thread behind.
    // Exit immediately after finalize to avoid hitting std::terminate()
    // during global thread-vector destruction.
    std::_Exit(exit_code);
}
