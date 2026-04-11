#include <sstream>
#include <string>

#include "analysis/verifier.h"
#include "bt_ast_interface.h"
#include "ir/ir_printer.h"
#include "lowering/lowering.h"
#include "m_script_test_support.h"

using namespace baltam;
using namespace baltam::test_support;

namespace {

Module lower_test_script_to_non_ssa(const std::string& script_name) {
    const std::string script_path = resolve_test_script_path(script_name).string();

    std::string msg;
    const auto parsed_units =
        bt_ast_interface::parse_mfile(script_path, ParserOpts{ParserOpts::DEFAULT}, msg);
    expect(!parsed_units.empty(), "failed to parse " + script_name + ".m: " + msg);

    Module non_ssa_module = lower_parsed_units_to_ir(parsed_units);
    analysis::verify_module_or_throw(non_ssa_module);
    return non_ssa_module;
}

std::string print_module(const Module& module) {
    std::ostringstream oss;
    print_ir(oss, module);
    return oss.str();
}

void test_test4_cell_index_lowering() {
    const Module non_ssa_module = lower_test_script_to_non_ssa("test4");
    const std::string text = print_module(non_ssa_module);

    expect(text.find("call @__ir_cell_get__(%my_args") != std::string::npos,
           "test4 should lower cell expansion/get to __ir_cell_get__.");
    expect(text.find("call @__ir_cell_set__(%varargin") != std::string::npos,
           "test4 should lower cell write-back to __ir_cell_set__.");
}

}  // namespace

int main() {
    return run_runtime_test("test_cell_index_lowering", [] {
        test_test4_cell_index_lowering();
    });
}
