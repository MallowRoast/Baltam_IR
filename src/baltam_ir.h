#ifndef BALTAM_IR_H
#define BALTAM_IR_H

#include <string>

#include "ir/ir.h"

namespace baltam {

struct MFileIRPipeline {
    Module non_ssa_module;
    Module untyped_ssa_module;
};

// Callers must initialize bt_ast_interface before using this helper.
MFileIRPipeline build_mfile_ir_pipeline(const std::string& script_path);

}  // namespace baltam

#endif
