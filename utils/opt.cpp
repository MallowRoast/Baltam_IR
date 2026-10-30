#include "ir/ir_io.h"

#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
void help() {
    std::cout << R"(OVERVIEW: Baltam IR optimizer

USAGE: opt [options] [input.ir|-]

OPTIONS:
  -help, --help          Show this help
  -version, --version    Show version
  -S                    Emit textual IR (currently the only output format)
  -o <filename|->       Output file; '-' or omitted means stdout
  -passes=<pipeline>    Pass pipeline; currently only an empty pipeline is supported
  -disable-output       Read and validate IR without writing output
  -print-passes         List available passes and exit
  -resource-dir=<path>  Base directory for relative MAT references on stdin
                        Also enables MAT resource output when writing to stdout
  --                    Treat remaining arguments as filenames

An omitted input filename means stdin. File input resolves MAT references relative
to the input file. For modules with MAT constants, prefer -o to export the IR and
 its companion files together. No optimization passes are registered yet.
)";
}
struct Options {
    std::optional<std::string> input, output, pipeline;
    std::optional<std::filesystem::path> resources;
    bool disable_output = false;
    bool show_help = false, version = false, print_passes = false;
};
Options parse(int argc, char **argv) {
    Options options;
    bool positional = false;
    auto set = [](auto &target, auto value, const char *name) {
        if (target)
            throw std::invalid_argument(std::string("duplicate option: ") + name);
        target = std::move(value);
    };
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 == argc)
                throw std::invalid_argument("missing value for " + arg);
            return argv[++i];
        };
        if (!positional && arg == "--") {
            positional = true;
            continue;
        }
        if (!positional && (arg == "-help" || arg == "--help" || arg == "-h"))
            options.show_help = true;
        else if (!positional && (arg == "-version" || arg == "--version"))
            options.version = true;
        else if (!positional && arg == "-S") {
        } // 当前仅支持文本格式。
        else if (!positional && (arg == "-disable-output" || arg == "--disable-output"))
            options.disable_output = true;
        else if (!positional && (arg == "-print-passes" || arg == "--print-passes"))
            options.print_passes = true;
        else if (!positional && arg == "-o")
            set(options.output, next(), "-o");
        else if (!positional && arg.rfind("-o=", 0) == 0)
            set(options.output, arg.substr(3), "-o");
        else if (!positional && (arg == "-passes" || arg == "--passes"))
            set(options.pipeline, next(), "-passes");
        else if (!positional && arg.rfind("-passes=", 0) == 0)
            set(options.pipeline, arg.substr(8), "-passes");
        else if (!positional && arg.rfind("--passes=", 0) == 0)
            set(options.pipeline, arg.substr(9), "-passes");
        else if (!positional && arg == "-resource-dir")
            set(options.resources, std::filesystem::path(next()), "-resource-dir");
        else if (!positional && arg.rfind("-resource-dir=", 0) == 0)
            set(options.resources, std::filesystem::path(arg.substr(14)), "-resource-dir");
        else if (!positional && arg != "-" && !arg.empty() && arg.front() == '-')
            throw std::invalid_argument("unknown option: " + arg);
        else
            set(options.input, arg, "input filename");
    }
    if (options.output && options.output->empty())
        throw std::invalid_argument("empty output filename");
    if (options.input && options.input->empty())
        throw std::invalid_argument("empty input filename");
    return options;
}
}

int main(int argc, char **argv) {
    try {
        auto options = parse(argc, argv);
        if (options.show_help) {
            help();
            return 0;
        }
        if (options.version) {
            std::cout << "Baltam IR opt 0.1.2\n";
            return 0;
        }
        if (options.print_passes) {
            std::cout
                << "No optimization passes registered. Use -passes='' for an empty pipeline.\n";
            return 0;
        }
        if (options.pipeline && options.pipeline->find_first_not_of(" \t\r\n") != std::string::npos)
            throw std::invalid_argument("unsupported pass pipeline: " + *options.pipeline +
                                        "; no optimization passes registered");
        std::unique_ptr<baltam::IRModule> module;
        if (!options.input || *options.input == "-") {
            std::ostringstream buffer;
            buffer << std::cin.rdbuf();
            if (std::cin.bad())
                throw std::runtime_error("failed to read stdin");
            module =
                baltam::read_ir(buffer.str(), options.resources.value_or(std::filesystem::path{}));
        } else {
            module = baltam::read_ir_file(*options.input);
        }
        if (options.disable_output)
            return 0;
        if (options.output && *options.output != "-") {
            baltam::write_ir_file(*module, *options.output);
        } else {
            // 先生成完整文本，避免失败时向标准输出写入不完整的 IR。
            const auto text =
                module->to_string(options.resources.value_or(std::filesystem::path{}));
            std::cout << text;
            if (!std::cout)
                throw std::runtime_error("failed to write stdout");
        }
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "opt: error: " << error.what() << '\n';
        return 1;
    }
}
