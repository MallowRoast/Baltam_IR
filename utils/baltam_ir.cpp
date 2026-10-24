#include "ir/ir_builder.h"

#include <filesystem>
#include <fstream>
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
        if (output) {
            std::error_code error;
            if (std::filesystem::equivalent(*input, *output, error))
                throw std::invalid_argument("输出文件不能与输入文件相同");
        }
        const auto ir = baltam::IRBuilder::build_file(*input)->to_string();
        if (output) {
            std::ofstream file(*output, std::ios::binary | std::ios::trunc);
            if (!file) throw std::runtime_error("无法打开输出文件: " + output->string());
            file << ir;
            if (!file) throw std::runtime_error("写入输出文件失败: " + output->string());
        } else {
            std::cout << ir;
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "baltam_ir: " << error.what() << '\n';
        return 1;
    }
}
