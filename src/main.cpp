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
#include "ir/ir_printer.h"
#include "lowering/lowering.h"
#include "optimizer/construct_untyped_ssa.h"

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

int run_m_file(const std::string& script_path) {
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
