//
// Created by zj on 25-7-3.
//

#include <cstdlib>
#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "bt_ast_interface.h"
#include "interpreter/interpreter.h"
#include "ir/ir.h"
#include "lowering/lowering.h"

using namespace baltam;

namespace {

constexpr const char* kScriptRelativePath = "/test/simple_demo.m";

void print_parsed_ast(const std::shared_ptr<pcdata>& parsed_unit, std::size_t index) {
    if (parsed_unit == nullptr) {
        std::cout << "pcdata[" << index << "] is null" << std::endl;
        return;
    }

    std::cout << "pcdata[" << index << "]" << std::endl;
    std::cout << "  filename: " << parsed_unit->filename << std::endl;
    std::cout << "  is_mscript: " << std::boolalpha << parsed_unit->is_mscript() << std::endl;
    std::cout << "  is_mfun: " << std::boolalpha << parsed_unit->is_mfun() << std::endl;

    if (parsed_unit->ast == nullptr) {
        std::cout << "  ast: null" << std::endl;
        return;
    }

    std::cout << "  ast2str:" << std::endl;
    std::cout << ast2str(parsed_unit->ast) << std::endl;

    std::cout << "  ast tree:" << std::endl;
    printAst(parsed_unit->ast, nullptr, false);
    std::cout << std::endl;
}

void print_frame_symbols(const Frame& frame) {
    std::cout << "Interpreter frame:" << std::endl;
    for (const auto& [name, binding] : frame.symbols()) {
        std::cout << "  " << name << " = ";
        if (!binding.initialized) {
            std::cout << "<uninitialized>";
        } else {
            std::cout << value_text(binding.value);
        }
        std::cout << std::endl;
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

    std::cout << "Parsed file: " << script_path << std::endl;
    for (std::size_t i = 0; i < parsed_units.size(); ++i) {
        print_parsed_ast(parsed_units[i], i);
    }

    try {
        const Module module = lower_parsed_units_to_ir(parsed_units);
        std::cout << "Lowered IR for " << script_path << ":" << std::endl;
        print_ir(std::cout, module);
        std::cout << std::endl;

        if (module.entry_function() != nullptr) {
            const Frame frame = execute_function(*module.entry_function());
            print_frame_symbols(frame);
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
    if (argc <= 1) {
        script_paths.push_back(std::string(BALTAM_IR_SOURCE_DIR) + kScriptRelativePath);
    } else {
        script_paths.reserve(static_cast<std::size_t>(argc - 1));
        for (int i = 1; i < argc; ++i) {
            script_paths.emplace_back(argv[i]);
        }
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
