#include "smoke_test_common.h"

#include <iostream>

namespace baltam {
namespace {

const FunctionUnit* find_function_unit_by_name(const MFileUnit& mfile, std::string_view name) {
    for (const auto& unit_ptr : mfile.code_units) {
        if (unit_ptr != nullptr &&
            unit_ptr->is_function() &&
            unit_ptr->name == name) {
            return static_cast<const FunctionUnit*>(unit_ptr.get());
        }
    }
    return nullptr;
}

std::size_t count_calls_to(
    const CodeUnit& unit,
    std::string_view callee_name,
    DispatchType dispatch_type) {
    std::size_t count = 0;
    for (const auto& block_ptr : unit.basic_blocks) {
        if (block_ptr == nullptr) {
            continue;
        }

        for (const auto& inst_ptr : block_ptr->instructions) {
            if (inst_ptr == nullptr || inst_ptr->type() != Instruction::Call) {
                continue;
            }

            const auto* call = static_cast<const CallInst*>(inst_ptr.get());
            if (call->dispatch_type != dispatch_type) {
                continue;
            }

            if (std::holds_alternative<InternedString>(call->callee) &&
                std::get<InternedString>(call->callee) == callee_name) {
                ++count;
            }
        }
    }
    return count;
}

void verify_complete_ir(const IRBuildResult& result) {
    smoke_test::require_ir_is_complete(result);
    smoke_test::require(result.mfile->is_function_file(), "test1_1 应构造成函数文件");
    smoke_test::require(result.mfile->code_units.size() == 5, "test1_1 应生成主函数和四个 local 函数单元");
    smoke_test::require(result.mfile->file_stem() == "test1_1", "文件 stem 应为 test1_1");
    smoke_test::require(
        result.mfile->entry_unit != nullptr && result.mfile->entry_unit->name == "test1_1",
        "入口应为主函数 test1_1");
}

void verify_core_focus(const IRBuildResult& result) {
    const CodeUnit* entry = result.mfile->entry_unit;
    smoke_test::require(entry != nullptr && entry->is_function(), "入口应为函数单元");

    const FunctionUnit* local_sin = find_function_unit_by_name(*result.mfile, "sin");
    smoke_test::require(local_sin != nullptr, "应生成名为 sin 的 local 函数单元");
    smoke_test::require(
        result.mfile->find_local_function("sin") == local_sin,
        "mfile 上的 local 函数索引应指向 sin 单元");
    const FunctionUnit* local_cos = find_function_unit_by_name(*result.mfile, "cos");
    smoke_test::require(local_cos != nullptr, "应生成名为 cos 的 local 函数单元");
    smoke_test::require(
        result.mfile->find_local_function("cos") == local_cos,
        "mfile 上的 local 函数索引应指向 cos 单元");
    const FunctionUnit* local_plus = find_function_unit_by_name(*result.mfile, "plus");
    smoke_test::require(local_plus != nullptr, "应生成名为 plus 的 local 函数单元");
    smoke_test::require(
        result.mfile->find_local_function("plus") == local_plus,
        "mfile 上的 local 函数索引应指向 plus 单元");
    const FunctionUnit* local_uminus = find_function_unit_by_name(*result.mfile, "uminus");
    smoke_test::require(local_uminus != nullptr, "应生成名为 uminus 的 local 函数单元");
    smoke_test::require(
        result.mfile->find_local_function("uminus") == local_uminus,
        "mfile 上的 local 函数索引应指向 uminus 单元");

    smoke_test::require(
        smoke_test::count_instructions(*entry, Instruction::Call) == 1,
        "主函数中只应为 sin(a) 生成一条动态 direct call");
    smoke_test::require(
        smoke_test::count_instructions(*entry, Instruction::Apply) == 0,
        "主函数中不应再用 apply 表达已绑定变量 cos(a)");
    smoke_test::require(
        smoke_test::count_instructions(*entry, Instruction::ValueApply) == 1,
        "主函数中应有一条 value_apply 指令用于已绑定变量 cos(a)");
    smoke_test::require(
        count_calls_to(*entry, "plus", MFunction) == 0,
        "plus 被局部变量遮蔽后，a = 1 + 2 不应命中 local plus");
    smoke_test::require(
        count_calls_to(*entry, "sin", Dynamic) == 1,
        "b = sin(a) 应保留为动态 direct call");
    smoke_test::require(
        count_calls_to(*entry, "sin", MFunction) == 0,
        "b = sin(a) 不应在 lowering 阶段静态命中 local sin");
    smoke_test::require(
        count_calls_to(*entry, "uminus", MFunction) == 0,
        "e = -a 不应在 lowering 阶段静态命中 local uminus");
    smoke_test::require(
        smoke_test::count_instructions(*entry, Instruction::Unary) == 1,
        "e = -a 应保留为普通 unary neg");
    smoke_test::require(
        smoke_test::find_slot_by_name(*entry, "cos") != nullptr,
        "主函数中应存在名为 cos 的局部变量槽位");
    smoke_test::require(
        smoke_test::find_slot_by_name(*entry, "plus") != nullptr,
        "主函数中应存在名为 plus 的局部变量槽位");
}

} // namespace
} // namespace baltam

int main() {
    try {
        const baltam::IRBuildResult ir = baltam::smoke_test::build_ir(TEST1_1_MFILE_PATH);
        baltam::verify_complete_ir(ir);
        baltam::verify_core_focus(ir);
    } catch (const std::exception& ex) {
        std::cerr << "test1_1_smoke 失败: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
