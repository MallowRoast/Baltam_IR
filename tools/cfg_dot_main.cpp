#include "ir/ir_lowering.h"
#include "ir/ir_verify.h"
#include "tools/cfg_dot.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Options {
    std::filesystem::path input_mfile;
    std::filesystem::path dot_output;
    std::filesystem::path svg_output;
    std::filesystem::path png_output;
    bool help = false;
};

void print_usage(std::ostream& os) {
    os << "usage: ir_cfg_dot <input.m> [-o output.dot] [--svg output.svg] "
          "[--png output.png]\n"
       << '\n'
       << "options:\n"
       << "  -o, --output <path>  write Graphviz DOT to path; use '-' for stdout\n"
       << "  --dot <path>         alias for --output\n"
       << "  --svg <path>         render SVG with Graphviz dot\n"
       << "  --png <path>         render PNG with Graphviz dot\n"
       << "  -h, --help           show this help\n";
}

bool take_value(
    int& index,
    int argc,
    char** argv,
    std::filesystem::path& output,
    std::string_view option_name) {
    if (index + 1 >= argc) {
        std::cerr << "ir_cfg_dot: " << option_name << " 缺少参数\n";
        return false;
    }
    ++index;
    output = argv[index];
    return true;
}

bool parse_args(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg{argv[i]};
        if (arg == "-h" || arg == "--help") {
            options.help = true;
            return true;
        }
        if (arg == "-o" || arg == "--output" || arg == "--dot") {
            if (!take_value(i, argc, argv, options.dot_output, arg)) {
                return false;
            }
            continue;
        }
        if (arg == "--svg") {
            if (!take_value(i, argc, argv, options.svg_output, arg)) {
                return false;
            }
            continue;
        }
        if (arg == "--png") {
            if (!take_value(i, argc, argv, options.png_output, arg)) {
                return false;
            }
            continue;
        }
        if (!arg.empty() && arg.front() == '-') {
            std::cerr << "ir_cfg_dot: 未知选项 " << arg << '\n';
            return false;
        }
        if (!options.input_mfile.empty()) {
            std::cerr << "ir_cfg_dot: 只能指定一个输入 .m 文件\n";
            return false;
        }
        options.input_mfile = std::string(arg);
    }

    if (options.input_mfile.empty() && !options.help) {
        std::cerr << "ir_cfg_dot: 缺少输入 .m 文件\n";
        return false;
    }

    return true;
}

bool write_text_file(const std::filesystem::path& path, std::string_view text) {
    if (path == "-") {
        std::cout << text;
        return true;
    }

    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path());
    }

    std::ofstream output(path, std::ios::binary);
    if (!output) {
        std::cerr << "ir_cfg_dot: 无法写入 " << path << '\n';
        return false;
    }

    output << text;
    if (!output) {
        std::cerr << "ir_cfg_dot: 写入失败 " << path << '\n';
        return false;
    }

    return true;
}

std::string shell_quote(const std::filesystem::path& path) {
    const std::string text = path.string();
    std::string quoted = "'";
    for (char ch : text) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted.push_back(ch);
        }
    }
    quoted.push_back('\'');
    return quoted;
}

bool render_with_graphviz(
    const std::filesystem::path& dot_input,
    const std::filesystem::path& output,
    std::string_view format) {
    if (dot_input.empty() || dot_input == "-") {
        std::cerr << "ir_cfg_dot: 生成 " << format << " 需要文件形式的 DOT 输出\n";
        return false;
    }
    if (output.empty()) {
        return true;
    }

    if (!output.parent_path().empty()) {
        std::filesystem::create_directories(output.parent_path());
    }

    const std::string command = "dot -T" + std::string(format) + ' ' +
        shell_quote(dot_input) + " -o " + shell_quote(output);
    const int exit_code = std::system(command.c_str());
    if (exit_code != 0) {
        std::cerr << "ir_cfg_dot: Graphviz 渲染失败: " << command << '\n';
        return false;
    }
    return true;
}

bool verify_lowering_result(const baltam::IRBuildResult& result) {
    if (result.mfile == nullptr) {
        std::cerr << "ir_cfg_dot: IR 文件单元为空\n";
        return false;
    }

    for (const baltam::IRBuildDiagnostic& diagnostic : result.diagnostics) {
        if (diagnostic.severity == baltam::IRBuildDiagnostic::Error) {
            std::cerr << "ir_cfg_dot: lowering error: " << diagnostic.message << '\n';
            return false;
        }
    }

    const baltam::IRVerifyResult verify_result = baltam::verify_ir(*result.mfile);
    if (!verify_result.ok()) {
        for (const baltam::IRVerifyDiagnostic& diagnostic : verify_result.diagnostics) {
            if (diagnostic.severity == baltam::IRVerifyDiagnostic::Error) {
                std::cerr << "ir_cfg_dot: verify error: " << diagnostic.message << '\n';
                return false;
            }
        }
    }

    return true;
}

std::filesystem::path default_dot_output(const std::filesystem::path& input_mfile) {
    std::filesystem::path output = input_mfile.filename();
    output.replace_extension(".dot");
    return output;
}

int run(const Options& options) {
    baltam::IRBuildResult result =
        baltam::parse_and_lower_mfile_to_ir(options.input_mfile.string());
    if (!verify_lowering_result(result)) {
        return 1;
    }

    std::filesystem::path dot_output = options.dot_output;
    if (dot_output.empty()) {
        if (!options.svg_output.empty() || !options.png_output.empty()) {
            dot_output = default_dot_output(options.input_mfile);
        } else {
            dot_output = "-";
        }
    }

    const std::string dot = baltam::format_cfg_dot(*result.mfile);
    if (!write_text_file(dot_output, dot)) {
        return 1;
    }

    if (!render_with_graphviz(dot_output, options.svg_output, "svg")) {
        return 1;
    }
    if (!render_with_graphviz(dot_output, options.png_output, "png")) {
        return 1;
    }

    return 0;
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parse_args(argc, argv, options)) {
        print_usage(std::cerr);
        return 2;
    }
    if (options.help) {
        print_usage(std::cout);
        return 0;
    }

    return run(options);
}
