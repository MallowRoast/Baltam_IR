#include "runtime_smoke_test_common.h"
#include "ir/ir_print.h"
#include "pass/cfg_simplification_pass.h"
#include "pass/constant_deduplication_pass.h"
#include "pass/dead_code_elimination_pass.h"
#include "pass/ir_pass_manager.h"
#include "pass/load_forwarding_pass.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string_view>

namespace baltam {
namespace {

IRPrintOptions compact_ir_print_options() {
    IRPrintOptions options;
    options.print_file_header = false;
    options.print_source_comments = false;
    return options;
}

void dump_ir(std::string_view title, const MFileUnit& mfile) {
    std::cerr << "==== " << title << " ====\n";
    std::cerr << format_ir(mfile, compact_ir_print_options()) << '\n';
}

IRPassManagerResult optimize_test11_ir(MFileUnit& mfile) {
    IRPassManagerOptions options;
    options.verify_after_pipeline = true;

    IRPassManager manager(options);
    manager.add_pass<ConstantDeduplicationPass>();
    manager.add_pass<LoadForwardingPass>();
    manager.add_pass<DeadCodeEliminationPass>();
    manager.add_pass<CFGSimplificationPass>();
    return manager.run(mfile);
}

void dump_pass_summary(const IRPassManagerResult& result) {
    std::cerr << "==== optimize summary ====\n";
    std::cerr << "ok=" << result.ok()
              << ", changed=" << result.changed
              << ", passes=" << result.pass_runs.size() << '\n';
    for (const IRPassRunSummary& run : result.pass_runs) {
        std::cerr << run.pass_name
                  << " [" << ir_pass_scope_name(run.scope) << ']'
                  << ": invocations=" << run.invocations
                  << ", changed=" << run.changed
                  << ", changed_invocations=" << run.changed_invocations
                  << '\n';
    }
}

void verify_test11_runtime() {
    IRBuildResult ir = smoke_test::build_ir(TEST11_MFILE_PATH);
    smoke_test::require_ir_is_complete(ir);
    smoke_test::require(ir.mfile->is_function_file(), "test11.m 应 lower 成函数文件");
    smoke_test::require(ir.mfile->entry_unit != nullptr, "test11 入口不能为空");
    smoke_test::require(ir.mfile->entry_unit->name == "test11", "入口函数名应为 test11");

    dump_ir("raw ir", *ir.mfile);

    const IRPassManagerResult optimize_result = optimize_test11_ir(*ir.mfile);
    dump_pass_summary(optimize_result);
    smoke_test::require(optimize_result.ok(), "test11 优化后 IR 不应报错");
    smoke_test::require(optimize_result.changed, "test11 优化 pipeline 应产生变更");
    dump_ir("optimized ir", *ir.mfile);

    InterpreterContext context;
    MFileUnit* mfile = smoke_test::install_mfile_ir(context, ir);
    smoke_test::require(mfile != nullptr, "Context 应持有 test11.m IR");

    smoke_test::ScopedBuiltinDefinitions builtins;
    (void)builtins;

    load_builtin_library();
    std::shared_ptr<CodeObject> code = smoke_test::make_code_object(context, *mfile);

    RuntimeFrame frame;
    frame.code = code.get();
    frame.actual_nargin = 0;
    frame.requested_nargout = 1;
    frame.initialize_storage();

    std::vector<ba_obj_ptr> outputs;
    {
        FrameScope scope(context, frame);
        outputs = execute_frame(frame);
    }

    smoke_test::require(outputs.size() == 1, "test11 应返回一个值");
    smoke_test::require(outputs[0] != nullptr, "test11 返回值不应为空");
    const SlotInfo* t_slot = smoke_test::find_slot_by_name(*mfile->entry_unit, "t");
    smoke_test::require(t_slot != nullptr, "test11 应存在 t slot");
    const ba_obj_ptr& t_value = frame.slot_value(t_slot->slot.id);
    smoke_test::require(t_value != nullptr, "t 不应为空");
    std::cerr << "==== runtime result ====\n";
    std::cerr << "type=" << outputs[0]->brief_type_str()
              << ", value=" << outputs[0]->as_double() << '\n';
    std::cerr << "t=" << t_value->as_double() << '\n';
}

} // namespace
} // namespace baltam

int main() {
    try {
        baltam::verify_test11_runtime();
    } catch (const std::exception& ex) {
        std::cerr << "test11_smoke 失败: " << ex.what() << '\n';
        return 1;
    }

    std::cerr << std::flush;
    std::quick_exit(0);
}
