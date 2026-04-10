#include "baltam_ir.h"

#include <stdexcept>
#include <string>
#include <utility>

#include "analysis/verifier.h"
#include "bt_ast_interface.h"
#include "lowering/lowering.h"
#include "optimizer/construct_untyped_ssa.h"

namespace baltam {

MFileIRPipeline build_mfile_ir_pipeline(const std::string& script_path) {
    std::string msg;
    const auto parsed_units =
        bt_ast_interface::parse_mfile(script_path, ParserOpts{ParserOpts::DEFAULT}, msg);

    if (parsed_units.empty()) {
        std::string error_message;
        if (!msg.empty()) {
            error_message += "解析失败: " + msg + "。";
        }
        error_message += "文件未生成 AST: " + script_path;
        throw std::runtime_error(error_message);
    }

    Module non_ssa_module = lower_parsed_units_to_ir(parsed_units);
    analysis::verify_module_or_throw(non_ssa_module);
    Module untyped_ssa_module = optimizer::construct_untyped_ssa_module(non_ssa_module);
    analysis::verify_module_or_throw(untyped_ssa_module);
    return {std::move(non_ssa_module), std::move(untyped_ssa_module)};
}

}  // namespace baltam
