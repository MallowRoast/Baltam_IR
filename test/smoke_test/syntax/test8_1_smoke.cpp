#include "smoke_test_common.h"

#include <cstdint>
#include <iostream>
#include <string_view>
#include <variant>
#include <vector>

namespace baltam {
namespace {

std::vector<const MagicEndInst*> find_magic_ends(const CodeUnit& unit) {
    std::vector<const MagicEndInst*> magic_ends;
    for (const auto& block_ptr : unit.basic_blocks) {
        for (const auto& inst_ptr : block_ptr->instructions) {
            if (inst_ptr != nullptr && inst_ptr->type() == Instruction::MagicEnd) {
                magic_ends.push_back(static_cast<const MagicEndInst*>(inst_ptr.get()));
            }
        }
    }
    return magic_ends;
}

std::vector<const CallInst*> find_internal_calls(
    const CodeUnit& unit,
    std::string_view name) {
    std::vector<const CallInst*> calls;
    for (const auto& block_ptr : unit.basic_blocks) {
        for (const auto& inst_ptr : block_ptr->instructions) {
            if (inst_ptr == nullptr || inst_ptr->type() != Instruction::Call) {
                continue;
            }

            const auto& call = static_cast<const CallInst&>(*inst_ptr);
            if (call.dispatch_type == Internal &&
                call.callee_kind == CallInst::Direct &&
                std::holds_alternative<InternedString>(call.callee) &&
                std::get<InternedString>(call.callee) == name) {
                calls.push_back(&call);
            }
        }
    }
    return calls;
}

void require_named_context(
    const MagicEndInst::MagicEndContext& context,
    std::string_view name,
    std::uint32_t dim,
    std::uint32_t nindices,
    const char* message) {
    smoke_test::require(
        std::holds_alternative<InternedString>(context.callee_or_base) &&
            std::get<InternedString>(context.callee_or_base) == name,
        message);
    smoke_test::require(context.dim == dim, message);
    smoke_test::require(context.nindices == nindices, message);
}

void require_magic_end_result_type(const CodeUnit& unit, const MagicEndInst& inst) {
    const ValueInfo* result_info = unit.value_table.find(inst.result);
    smoke_test::require(result_info != nullptr, "magic_end 结果应进入 value_table");
    smoke_test::require(result_info->type_fact.is_unknown,
                        "magic_end 结果类型应保持 unknown");
}

void verify_script_magic_end_context(const IRBuildResult& result) {
    smoke_test::require_ir_is_complete(result);
    smoke_test::require(result.mfile->is_script_file(), "test8_1 应为脚本文件");

    const CodeUnit* unit = result.mfile->entry_unit;
    smoke_test::require(unit != nullptr, "test8_1 入口单元不能为空");
    smoke_test::require(
        smoke_test::count_instructions(*unit, Instruction::Apply) == 3,
        "脚本中应保留 A(end) 和 A(fun(end)) 的三条 unresolved apply");
    smoke_test::require(
        find_internal_calls(*unit, "end_index").empty(),
        "基础 lowering 不应直接生成 internal.end_index");
    smoke_test::require(
        smoke_test::count_instructions(*unit, Instruction::MagicEnd) == 2,
        "脚本中的两个 magic end 都应 lower 成 MagicEndInst");

    const std::vector<const MagicEndInst*> magic_ends = find_magic_ends(*unit);
    std::size_t single_count = 0;
    std::size_t nested_count = 0;
    for (const MagicEndInst* magic_end : magic_ends) {
        require_magic_end_result_type(*unit, *magic_end);
        if (magic_end->candidate_contexts.size() == 1U) {
            ++single_count;
            require_named_context(
                magic_end->candidate_contexts[0],
                "A",
                1,
                1,
                "A(end) 应保留 A 的单层候选上下文");
            continue;
        }

        smoke_test::require(magic_end->candidate_contexts.size() == 2U,
                            "A(fun(end)) 应保留 fun -> A 两层候选上下文");
        ++nested_count;
        require_named_context(
            magic_end->candidate_contexts[0],
            "fun",
            1,
            1,
            "magic_end 第一候选应是内层 fun(end)");
        require_named_context(
            magic_end->candidate_contexts[1],
            "A",
            1,
            1,
            "magic_end 第二候选应是外层 A(...)");
    }

    smoke_test::require(single_count == 1, "A(end) 应生成一次单候选 magic_end");
    smoke_test::require(nested_count == 1, "A(fun(end)) 应生成一次双候选 magic_end");
}

} // namespace
} // namespace baltam

int main() {
    try {
        const baltam::IRBuildResult ir = baltam::smoke_test::build_ir(TEST8_1_MFILE_PATH);
        baltam::verify_script_magic_end_context(ir);
    } catch (const std::exception& ex) {
        std::cerr << "test8_1_smoke 失败: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
