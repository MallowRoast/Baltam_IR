//
// Created by zj on 25-7-3.
//

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "bt_ast_interface.h"

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

}  // namespace

int main() {
    const std::string script_path = std::string(BALTAM_IR_SOURCE_DIR) + kScriptRelativePath;

    int exit_code = 0;
    const int init_ret = bt_ast_interface::initialize();
    if (init_ret != 0) {
        std::cerr << "bt_ast_interface::initialize failed, code = " << init_ret << std::endl;
        return 1;
    }

    std::string msg;
    const auto parsed_units = bt_ast_interface::parse_mfile(script_path, msg);

    if (!msg.empty()) {
        std::cout << "parser message: " << msg << std::endl;
    }

    if (parsed_units.empty()) {
        std::cerr << "No AST generated for file: " << script_path << std::endl;
        exit_code = 1;
    } else {
        std::cout << "Parsed file: " << script_path << std::endl;
        for (std::size_t i = 0; i < parsed_units.size(); ++i) {
            print_parsed_ast(parsed_units[i], i);
        }
    }

    std::cout << std::flush;
    std::cerr << std::flush;

    bt_ast_interface::finalize();

    // The Baltam runtime leaves a joinable background thread behind.
    // Exit immediately after finalize to avoid hitting std::terminate()
    // during global thread-vector destruction.
    std::_Exit(exit_code);
}
