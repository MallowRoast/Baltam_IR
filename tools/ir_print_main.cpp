#include "ir/ir_lowering.h"
#include "ir/ir_print.h"
#include "ir/ir_verify.h"
#include "pass/cfg_simplification_pass.h"
#include "pass/constant_deduplication_pass.h"
#include "pass/constant_folding_pass.h"
#include "pass/dead_branch_elimination_pass.h"
#include "pass/ir_pass_manager.h"
#include "pass/load_forwarding_pass.h"

#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>

#include <fcntl.h>
#include <unistd.h>

namespace {

struct Options {
    std::filesystem::path input_mfile;
    std::filesystem::path output;
    baltam::IRPrintOptions print_options;
    bool run_passes = false;
    bool help = false;
};

class ScopedStdoutSilencer final {
public:
    ScopedStdoutSilencer() {
        std::cout.flush();
        std::fflush(stdout);

        saved_stdout_fd_ = dup(STDOUT_FILENO);
        if (saved_stdout_fd_ < 0) {
            return;
        }

        null_fd_ = open("/dev/null", O_WRONLY);
        if (null_fd_ < 0) {
            return;
        }

        redirected_ = dup2(null_fd_, STDOUT_FILENO) >= 0;
    }

    ScopedStdoutSilencer(const ScopedStdoutSilencer&) = delete;
    ScopedStdoutSilencer& operator=(const ScopedStdoutSilencer&) = delete;

    ~ScopedStdoutSilencer() {
        std::cout.flush();
        std::fflush(stdout);

        if (redirected_ && saved_stdout_fd_ >= 0) {
            dup2(saved_stdout_fd_, STDOUT_FILENO);
        }
        if (null_fd_ >= 0) {
            close(null_fd_);
        }
        if (saved_stdout_fd_ >= 0) {
            close(saved_stdout_fd_);
        }
    }

private:
    int saved_stdout_fd_ = -1;
    int null_fd_ = -1;
    bool redirected_ = false;
};

void print_usage(std::ostream& os) {
    os << "usage: ir_print <input.m> [-o output.ir] [options]\n"
       << '\n'
       << "options:\n"
       << "  -o, --output <path>       write text IR to path; use '-' for stdout\n"
       << "  --no-source               do not append source comments\n"
       << "  --source-full             include collapsed source excerpts in comments\n"
       << "  --no-line-numbers         omit source line numbers in comments\n"
       << "  --no-slots                do not print the slot table\n"
       << "  --show-cfg                print basic block predecessor lists (default)\n"
       << "  --no-cfg                  do not print basic block predecessor lists\n"
       << "  --no-types                do not print ValueTable type facts\n"
       << "  --no-file-header          do not print the file header comment\n"
       << "  --comment-column <n>      minimum source comment column\n"
       << "  --run-passes              run the default IR cleanup pass pipeline before printing\n"
       << "  -h, --help                show this help\n";
}

bool take_path(
    int& index,
    int argc,
    char** argv,
    std::filesystem::path& output,
    std::string_view option_name) {
    if (index + 1 >= argc) {
        std::cerr << "ir_print: " << option_name << " 缺少参数\n";
        return false;
    }
    ++index;
    output = argv[index];
    return true;
}

bool take_size(
    int& index,
    int argc,
    char** argv,
    std::size_t& output,
    std::string_view option_name) {
    if (index + 1 >= argc) {
        std::cerr << "ir_print: " << option_name << " 缺少参数\n";
        return false;
    }

    ++index;
    const std::string_view text{argv[index]};
    std::size_t value = 0;
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const std::from_chars_result parsed = std::from_chars(begin, end, value);
    if (parsed.ec != std::errc{} || parsed.ptr != end) {
        std::cerr << "ir_print: " << option_name << " 需要非负整数参数\n";
        return false;
    }

    output = value;
    return true;
}

bool parse_args(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg{argv[i]};
        if (arg == "-h" || arg == "--help") {
            options.help = true;
            return true;
        }
        if (arg == "-o" || arg == "--output") {
            if (!take_path(i, argc, argv, options.output, arg)) {
                return false;
            }
            continue;
        }
        if (arg == "--no-source") {
            options.print_options.print_source_comments = false;
            continue;
        }
        if (arg == "--source-full") {
            options.print_options.print_source_excerpt = true;
            continue;
        }
        if (arg == "--no-line-numbers") {
            options.print_options.print_source_line_numbers = false;
            continue;
        }
        if (arg == "--no-slots") {
            options.print_options.print_slot_table = false;
            continue;
        }
        if (arg == "--show-cfg") {
            options.print_options.print_block_predecessors = true;
            continue;
        }
        if (arg == "--no-cfg") {
            options.print_options.print_block_predecessors = false;
            continue;
        }
        if (arg == "--no-types") {
            options.print_options.print_type_facts = false;
            continue;
        }
        if (arg == "--no-file-header") {
            options.print_options.print_file_header = false;
            continue;
        }
        if (arg == "--comment-column") {
            if (!take_size(
                    i,
                    argc,
                    argv,
                    options.print_options.min_comment_column,
                    arg)) {
                return false;
            }
            continue;
        }
        if (arg == "--run-passes") {
            options.run_passes = true;
            continue;
        }
        if (!arg.empty() && arg.front() == '-') {
            std::cerr << "ir_print: 未知选项 " << arg << '\n';
            return false;
        }
        if (!options.input_mfile.empty()) {
            std::cerr << "ir_print: 只能指定一个输入 .m 文件\n";
            return false;
        }
        options.input_mfile = std::string(arg);
    }

    if (options.input_mfile.empty() && !options.help) {
        std::cerr << "ir_print: 缺少输入 .m 文件\n";
        return false;
    }

    return true;
}

bool report_pass_result(const baltam::IRPassManagerResult& result) {
    for (const baltam::IRPassDiagnostic& diagnostic : result.diagnostics) {
        if (diagnostic.severity == baltam::IRPassDiagnostic::Error) {
            std::cerr << "ir_print: pass error";
        } else if (diagnostic.severity == baltam::IRPassDiagnostic::Warning) {
            std::cerr << "ir_print: pass warning";
        } else {
            std::cerr << "ir_print: pass log";
        }

        if (!diagnostic.pass_name.empty()) {
            std::cerr << " [" << diagnostic.pass_name << ']';
        }
        std::cerr << ": " << diagnostic.message << '\n';
    }

    return result.ok();
}

bool run_default_pass_pipeline(baltam::IRModule& module) {
    baltam::IRPassManagerOptions pass_options;
    pass_options.verify_after_pipeline = true;

    baltam::IRPassManager pass_manager(pass_options);
    pass_manager.add_pass<baltam::ConstantDeduplicationPass>();
    pass_manager.add_pass<baltam::LoadForwardingPass>();
    pass_manager.add_pass<baltam::ConstantFoldingPass>();
    pass_manager.add_pass<baltam::DeadBranchEliminationPass>();
    pass_manager.add_pass<baltam::ConstantDeduplicationPass>();
    pass_manager.add_pass<baltam::CFGSimplificationPass>();

    return report_pass_result(pass_manager.run(module));
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
        std::cerr << "ir_print: 无法写入 " << path << '\n';
        return false;
    }

    output << text;
    if (!output) {
        std::cerr << "ir_print: 写入失败 " << path << '\n';
        return false;
    }

    return true;
}

bool verify_lowering_result(const baltam::IRBuildResult& result) {
    if (result.mfile == nullptr) {
        std::cerr << "ir_print: IR 文件单元为空\n";
        return false;
    }

    for (const baltam::IRBuildDiagnostic& diagnostic : result.diagnostics) {
        if (diagnostic.severity == baltam::IRBuildDiagnostic::Error) {
            std::cerr << "ir_print: lowering error: " << diagnostic.message << '\n';
            return false;
        }
    }

    const baltam::IRVerifyResult verify_result = result.module != nullptr
        ? baltam::verify_ir(*result.module)
        : baltam::verify_ir(*result.mfile);
    if (!verify_result.ok()) {
        for (const baltam::IRVerifyDiagnostic& diagnostic : verify_result.diagnostics) {
            if (diagnostic.severity == baltam::IRVerifyDiagnostic::Error) {
                std::cerr << "ir_print: verify error: " << diagnostic.message << '\n';
                return false;
            }
        }
    }

    return true;
}

int run(const Options& options) {
    baltam::IRBuildResult result;
    {
        ScopedStdoutSilencer silence_parser_debug_output;
        result = baltam::parse_and_lower_mfile_to_ir(options.input_mfile.string());
    }
    if (!verify_lowering_result(result)) {
        return 1;
    }

    if (options.run_passes) {
        if (result.module == nullptr) {
            std::cerr << "ir_print: pass pipeline requires an IR module\n";
            return 1;
        }
        if (!run_default_pass_pipeline(*result.module)) {
            return 1;
        }
    }

    std::filesystem::path output = options.output;
    if (output.empty()) {
        output = "-";
    }

    const std::string ir = result.module != nullptr
        ? baltam::format_ir(*result.module, options.print_options)
        : baltam::format_ir(*result.mfile, options.print_options);
    if (!write_text_file(output, ir)) {
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

    const int exit_code = run(options);
    std::cout.flush();
    std::cerr.flush();
    std::_Exit(exit_code);
}
