#include "tools/cfg_dot.h"
#include "smoke_test_common.h"

#include <iostream>
#include <string>

namespace baltam {
namespace {

void verify_cfg_dot_for_test3_3() {
    const IRBuildResult ir = smoke_test::build_ir(TEST3_3_MFILE_PATH);
    smoke_test::require_ir_is_complete(ir);

    const std::string dot = format_cfg_dot(*ir.mfile);

    smoke_test::require(
        dot.find("digraph \"test3_3\"") != std::string::npos,
        "DOT 应使用 mfile stem 作为图名");
    smoke_test::require(
        dot.find("label=\"test3_3\"") != std::string::npos,
        "DOT 应打印 code unit 名称");
    smoke_test::require(
        dot.find("label=\"for.preheader\\ninsts:") != std::string::npos,
        "DOT 应打印 for block 节点");
    smoke_test::require(
        dot.find("label=\"while.header.1\\ninsts:") != std::string::npos,
        "DOT 应打印带后缀的 while block 节点");
    smoke_test::require(
        dot.find("label=\"true\"") != std::string::npos &&
            dot.find("label=\"false\"") != std::string::npos,
        "DOT 应给条件分支边标注 true / false");
    smoke_test::require(
        dot.find("label=\"break\"") != std::string::npos,
        "DOT 应给 break 跳转边标注 break");
    smoke_test::require(
        dot.find("label=\"continue\"") != std::string::npos,
        "DOT 应给 continue 跳转边标注 continue");

    std::cout << dot << '\n';
}

} // namespace
} // namespace baltam

int main() {
    try {
        baltam::verify_cfg_dot_for_test3_3();
    } catch (const std::exception& ex) {
        std::cerr << "cfg_dot_smoke 失败: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
