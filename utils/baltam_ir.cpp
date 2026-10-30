#include "ir/ir_builder.h"
#include "ir/ir_io.h"

#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

int main(int argc, char* argv[]) {
    std::optional<std::filesystem::path> input;
    std::optional<std::filesystem::path> output;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "-o" && !output && i + 1 < argc) {
            output = argv[++i];
        } else if (!input && !argument.empty() && argument.front() != '-') {
            input = argument;
        } else {
            std::cerr << "用法: baltam_ir [-o 输出文件] <文件.m>\n";
            return 2;
        }
    }
    if (!input) {
        std::cerr << "用法: baltam_ir [-o 输出文件] <文件.m>\n";
        return 2;
    }
    try {
        if (input->extension() != ".m")
            throw std::invalid_argument("baltam_ir 只接受 M 文件；处理 IR 文件请使用 opt");
        if (output) {
            std::error_code error;
            if (std::filesystem::equivalent(*input, *output, error))
                throw std::invalid_argument("输出文件不能与输入文件相同");
        }
        const auto module = baltam::IRBuilder::build_file(*input);
        if (output) {
            baltam::write_ir_file(*module, *output);
        } else {
            // 大常量需要资源目录；标准输出模式不创建配套文件。
            std::cout << module->to_string();
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "baltam_ir: " << error.what() << '\n';
        return 1;
    }
}
