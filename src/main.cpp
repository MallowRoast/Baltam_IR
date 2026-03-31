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

#include "ba_obj/ba_obj.h"
#include "bt_ast_interface.h"
#include "interpreter/interpreter.h"
#include "ir/ir.h"
#include "lowering/lowering.h"

using namespace baltam;

namespace {


std::string source_path_from_relative(std::string_view relative_path) {
    return std::string(BALTAM_IR_SOURCE_DIR) + std::string(relative_path);
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

void print_frame_state(const Frame& frame) {
    std::cout << "Interpreter outputs:" << std::endl;
    if (frame.function() != nullptr) {
        const auto& output_names = frame.function()->output_names();
        const auto& outputs = frame.outputs();
        for (std::size_t i = 0; i < output_names.size(); ++i) {
            std::cout << "  " << output_names[i] << " = ";
            if (i < outputs.size()) {
                std::cout << value_text(outputs[i]);
            } else {
                std::cout << "<missing>";
            }
            std::cout << std::endl;
        }
    }

    bool has_initialized_binding = false;
    for (const auto& [name, binding] : frame.symbols()) {
        if (binding.initialized) {
            if (!has_initialized_binding) {
                std::cout << "Interpreter bindings:" << std::endl;
                has_initialized_binding = true;
            }
            std::cout << "  " << name << " = " << value_text(binding.value) << std::endl;
        }
    }
}

int run_m_file(const std::string& script_path) {
    int exit_code = 0;
    std::string msg;
    const auto parsed_units = bt_ast_interface::parse_mfile(script_path, msg);

    if (!msg.empty()) {
        std::cout << "解析器消息: " << msg << std::endl;
    }

    if (parsed_units.empty()) {
        std::cerr << "文件未生成 AST: " << script_path << std::endl;
        return 1;
    }

    try {
        const Module module = lower_parsed_units_to_ir(parsed_units);
        std::cout << "Lowered IR for " << script_path << ":" << std::endl;
        print_ir(std::cout, module);
        std::cout << std::endl;

        if (module.entry_function() != nullptr) {
            std::vector<Value> args;
            // 直接调试函数文件时，main 没有额外的实参输入渠道。
            // 这里按函数签名补齐数值 1，便于像 test1 这样的用例直接以 test1(1, 1) 形式运行。
            args.reserve(module.entry_function()->input_names().size());
            for (std::size_t i = 0; i < module.entry_function()->input_names().size(); ++i) {
                args.push_back(std::make_shared<ba_obj>(1.0));
            }

            const Frame frame = execute_function(*module.entry_function(), args);
            print_frame_state(frame);
            std::cout << std::endl;
        }
    } catch (const std::exception& ex) {
        std::cerr << "文件的 IR lower 或解释执行失败: " << script_path << "，原因: "
                  << ex.what()
                  << std::endl;
        exit_code = 1;
    }

    return exit_code;
}

}  // namespace

int main(int argc, char** argv) {
    int exit_code = 0;
    const int init_ret = bt_ast_interface::initialize();
    if (init_ret != 0) {
        std::cerr << "bt_ast_interface::initialize 失败，返回码 = " << init_ret << std::endl;
        return 1;
    }

    std::vector<std::string> script_paths;
    script_paths.reserve(static_cast<std::size_t>(std::max(argc - 1, 0)));
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        script_paths.push_back(resolve_script_argument(arg));
    }

    for (std::size_t i = 0; i < script_paths.size(); ++i) {
        if (i != 0) {
            std::cout << std::string(72, '=') << std::endl;
        }
        exit_code = std::max(exit_code, run_m_file(script_paths[i]));
    }

    std::cout << std::flush;
    std::cerr << std::flush;

    bt_ast_interface::finalize();

    // The Baltam runtime leaves a joinable background thread behind.
    // Exit immediately after finalize to avoid hitting std::terminate()
    // during global thread-vector destruction.
    std::_Exit(exit_code);
}
