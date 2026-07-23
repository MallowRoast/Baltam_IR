#include "ir/ir_print.h"
#include "ba_obj/ba_type.h"
#include "pass/cfg_simplification_pass.h"
#include "pass/constant_deduplication_pass.h"
#include "pass/constant_folding_pass.h"
#include "pass/dead_branch_elimination_pass.h"
#include "pass/dead_code_elimination_pass.h"
#include "pass/ir_pass_manager.h"
#include "pass/load_forwarding_pass.h"
#include "print/obj2str.h"
#include "runtime/ir_executor.h"
#include "runtime/interpreter_context.h"
#include "smoke_test_common.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace baltam {
namespace {

void verify_complete_ir(const IRBuildResult& result) {
    smoke_test::require_ir_is_complete(result);
    smoke_test::require(result.mfile->is_function_file(), "test0_1 应构造成函数文件");
    smoke_test::require(result.mfile->code_units.size() == 1, "应只生成一个代码单元");
    smoke_test::require(result.mfile->file_stem() == "test0_1", "文件 stem 应为 test0_1");
}

void verify_core_focus(const IRBuildResult& result) {
    const CodeUnit* unit = result.mfile->entry_unit;
    smoke_test::require(unit != nullptr, "入口代码单元不能为空");
    smoke_test::require(unit->slot_table.slots.size() == 3, "函数 lowering 应创建 c/a/b 三个槽位");

    const auto* function = static_cast<const FunctionUnit*>(unit);
    smoke_test::require(function->param_slots.empty(), "test0_1 不应声明输入参数");
    smoke_test::require(function->return_slots.size() == 1, "test0_1 应声明一个返回槽位");
    smoke_test::require(
        unit->slot_table.slots[0].slot.tag == SlotTag::Ret && unit->slot_table.slots[0].name == "c",
        "第一个槽位应为返回槽位 c");
    smoke_test::require(
        unit->slot_table.slots[1].slot.tag == SlotTag::Local && unit->slot_table.slots[1].name == "a",
        "第二个槽位应为局部槽位 a");
    smoke_test::require(
        unit->slot_table.slots[2].slot.tag == SlotTag::Local && unit->slot_table.slots[2].name == "b",
        "第三个槽位应为局部槽位 b");

    smoke_test::require(
        smoke_test::count_instructions(*unit, Instruction::LoadSlot) == 4 &&
            smoke_test::count_instructions(*unit, Instruction::StoreSlot) == 4,
        "函数 lowering 应生成 slot 读写");
    smoke_test::require(
        smoke_test::find_slot_by_tag(*unit, SlotTag::ScriptVar) == nullptr,
        "函数 lowering 不应创建脚本变量槽位");
    smoke_test::require(
        smoke_test::count_instructions(*unit, Instruction::Apply) == 0,
        "函数 lowering 不应生成 apply");
    smoke_test::require(
        smoke_test::count_instructions(*unit, Instruction::Call) == 1,
        "函数 lowering 应生成一条 call");

    const Instruction* call_inst =
        smoke_test::find_first_instruction(*unit, Instruction::Call);
    smoke_test::require(call_inst != nullptr, "函数中应存在 call 指令");

    const auto* call = static_cast<const CallInst*>(call_inst);
    smoke_test::require(call->results.size() == 1, "函数中的 call 应产生一个结果");
    smoke_test::require(call->arguments.size() == 1, "函数中的 call 应只有一个参数");
    smoke_test::require(
        call->callee_kind == CallInst::Direct &&
            std::holds_alternative<InternedString>(call->callee) &&
            std::get<InternedString>(call->callee) == "sin",
        "函数中的 sin(a) 应 lowering 成 direct call");
}

void verify_other_important_checks(const IRBuildResult& result) {
    const CodeUnit* unit = result.mfile->entry_unit;
    smoke_test::require(unit->basic_blocks.size() == 4, "if/else 函数应生成 4 个基本块");
    smoke_test::require(
        unit->basic_blocks[0]->terminator()->type() == Instruction::Branch,
        "入口基本块应以条件分支结束");
    smoke_test::require(
        unit->basic_blocks[1]->terminator()->type() == Instruction::Goto &&
            unit->basic_blocks[2]->terminator()->type() == Instruction::Goto,
        "then/else 基本块应以跳转结束");
    smoke_test::require(
        unit->basic_blocks[3]->terminator()->type() == Instruction::Return,
        "exit 基本块应以返回结束");

    const auto* ret =
        static_cast<const ReturnInst*>(unit->basic_blocks[3]->terminator());
    smoke_test::require(ret->values.size() == 1, "函数 ret 应返回一个值");
    smoke_test::require(ret->values[0].is_valid(), "函数隐式 ret 应返回读取返回槽位后的 ValueId");
}

const char* slot_tag_name(SlotTag tag) noexcept {
    switch (tag) {
        case SlotTag::BaseVar:
            return "BaseVar";
        case SlotTag::ScriptVar:
            return "ScriptVar";
        case SlotTag::Local:
            return "Local";
        case SlotTag::Arg:
            return "Arg";
        case SlotTag::Ret:
            return "Ret";
        case SlotTag::Capture:
            return "Capture";
        case SlotTag::InternalLocal:
            return "InternalLocal";
        case SlotTag::Global:
            return "Global";
        case SlotTag::Persistent:
            return "Persistent";
        case SlotTag::Nargin:
            return "Nargin";
        case SlotTag::Nargout:
            return "Nargout";
        case SlotTag::Varargin:
            return "Varargin";
        case SlotTag::Varargout:
            return "Varargout";
    }

    return "Unknown";
}

std::string format_runtime_value(const ba_obj_ptr& value) {
    if (value == nullptr) {
        return "<unbound>";
    }

    try {
        return internal::obj2str_one_line(*value);
    } catch (const std::exception&) {
        try {
            return value->brief_type_str();
        } catch (const std::exception&) {
            return "<unprintable>";
        }
    }
}

void require_pass_manager_ok(const IRPassManagerResult& result) {
    if (result.ok()) {
        return;
    }

    std::string message = "优化 pass pipeline 不应产生 Error 诊断";
    for (const IRPassDiagnostic& diagnostic : result.diagnostics) {
        if (diagnostic.severity == IRPassDiagnostic::Error) {
            message += ": ";
            message += diagnostic.pass_name;
            message += ": ";
            message += diagnostic.message;
            break;
        }
    }
    smoke_test::fail(std::move(message));
}

void optimize_ir(IRModule& module) {
    IRPassManagerOptions options;
    options.verify_after_each_pass = true;
    options.verify_after_pipeline = true;

    IRPassManager manager(options);
    manager.add_pass<LoadForwardingPass>();
    manager.add_pass<ConstantFoldingPass>();
    manager.add_pass<DeadBranchEliminationPass>();
    manager.add_pass<CFGSimplificationPass>();
    manager.add_pass<LoadForwardingPass>();
    manager.add_pass<ConstantFoldingPass>();
    manager.add_pass<ConstantDeduplicationPass>();
    manager.add_pass<DeadCodeEliminationPass>();

    const IRPassManagerResult result = manager.run(module);
    require_pass_manager_ok(result);
}

void mark_value_used(ValueId value, std::unordered_set<ValueId>& values) {
    if (value.is_valid()) {
        values.insert(value);
    }
}

void collect_operand_uses(const Operand& operand, std::unordered_set<ValueId>& values) {
    if (const auto* value = std::get_if<ValueId>(&operand)) {
        mark_value_used(*value, values);
    }
}

void collect_operand_uses(
    const std::vector<Operand>& operands,
    std::unordered_set<ValueId>& values) {
    for (const Operand& operand : operands) {
        collect_operand_uses(operand, values);
    }
}

void collect_instruction_uses(
    const Instruction& instruction,
    std::unordered_set<ValueId>& values) {
    switch (instruction.type()) {
        case Instruction::StoreSlot:
            mark_value_used(static_cast<const StoreSlotInst&>(instruction).value, values);
            break;
        case Instruction::CreateAnonymousFunctionHandle:
            for (const auto& capture :
                 static_cast<const CreateAnonymousFunctionHandleInst&>(instruction).captures) {
                mark_value_used(capture.captured_value, values);
            }
            break;
        case Instruction::Apply: {
            const auto& inst = static_cast<const ApplyInst&>(instruction);
            collect_operand_uses(inst.callee_or_base, values);
            collect_operand_uses(inst.arguments, values);
            break;
        }
        case Instruction::ValueApply: {
            const auto& inst = static_cast<const ValueApplyInst&>(instruction);
            mark_value_used(inst.base, values);
            collect_operand_uses(inst.arguments, values);
            break;
        }
        case Instruction::MagicEnd:
            for (const auto& context :
                 static_cast<const MagicEndInst&>(instruction).candidate_contexts) {
                collect_operand_uses(context.callee_or_base, values);
            }
            break;
        case Instruction::Call: {
            const auto& inst = static_cast<const CallInst&>(instruction);
            collect_operand_uses(inst.callee, values);
            collect_operand_uses(inst.arguments, values);
            break;
        }
        case Instruction::Unary:
            collect_operand_uses(static_cast<const UnaryInst&>(instruction).operand, values);
            break;
        case Instruction::Binary: {
            const auto& inst = static_cast<const BinaryInst&>(instruction);
            collect_operand_uses(inst.lhs, values);
            collect_operand_uses(inst.rhs, values);
            break;
        }
        case Instruction::Branch:
            collect_operand_uses(static_cast<const BranchInst&>(instruction).condition, values);
            break;
        case Instruction::Return:
            for (ValueId value : static_cast<const ReturnInst&>(instruction).values) {
                mark_value_used(value, values);
            }
            break;
        case Instruction::Const:
        case Instruction::LoadSlot:
        case Instruction::GlobalDecl:
        case Instruction::PersistentDecl:
        case Instruction::CreateNamedFunctionHandle:
        case Instruction::Goto:
            break;
    }
}

std::unordered_set<ValueId> collect_used_values(const CodeUnit& unit) {
    std::unordered_set<ValueId> values;
    for (const auto& block_ptr : unit.basic_blocks) {
        if (block_ptr == nullptr) {
            continue;
        }

        for (const auto& inst_ptr : block_ptr->instructions) {
            if (inst_ptr != nullptr) {
                collect_instruction_uses(*inst_ptr, values);
            }
        }
    }
    return values;
}

void verify_constant_folding_result(const IRBuildResult& result) {
    const CodeUnit* unit = result.mfile->entry_unit;
    smoke_test::require(unit != nullptr, "常量折叠检查需要入口代码单元");

    const std::unordered_set<ValueId> used_values = collect_used_values(*unit);
    bool found_folded_three = false;
    bool found_folded_sin = false;
    bool found_folded_product = false;
    for (const auto& block_ptr : unit->basic_blocks) {
        if (block_ptr == nullptr) {
            continue;
        }

        for (const auto& inst_ptr : block_ptr->instructions) {
            if (inst_ptr == nullptr || inst_ptr->type() != Instruction::Const) {
                continue;
            }

            const auto& inst = static_cast<const ConstInst&>(*inst_ptr);
            smoke_test::require(
                used_values.find(inst.result) != used_values.end(),
                "test0_1 优化后不应保留未使用的 const");

            const auto* runtime_constant =
                std::get_if<RuntimeObjectConstant>(&inst.value);
            if (runtime_constant == nullptr ||
                !runtime_constant->folded ||
                runtime_constant->value == nullptr) {
                continue;
            }

            const double value = runtime_constant->value->as_double();
            if (runtime_constant->value->type() == ba_double_mat &&
                std::abs(value - 3.0) < 1e-12) {
                found_folded_three = true;
            }
            if (runtime_constant->value->type() == ba_double_mat &&
                std::abs(value - std::sin(3.0)) < 1e-12) {
                found_folded_sin = true;
            }
            if (runtime_constant->value->type() == ba_double_mat &&
                std::abs(value - (std::sin(3.0) * 2.0)) < 1e-12) {
                found_folded_product = true;
            }
        }
    }

    smoke_test::require(
        found_folded_three,
        "test0_1 优化后应包含由 1 + 2 折叠出的常量 3");
    smoke_test::require(
        found_folded_sin,
        "test0_1 优化后应包含由 sin(3) 折叠出的常量");
    smoke_test::require(
        found_folded_product,
        "test0_1 优化后应包含由 sin(3) * 2 折叠出的常量");
    smoke_test::require(
        smoke_test::count_instructions(*unit, Instruction::Call) == 0,
        "test0_1 优化后 sin(a) direct call 应被常量折叠删除");
    smoke_test::require(
        smoke_test::count_instructions(*unit, Instruction::Binary) == 0,
        "test0_1 优化后 mul 应被常量折叠删除");
    smoke_test::require(
        smoke_test::count_instructions(*unit, Instruction::LoadSlot) == 0,
        "test0_1 优化后不应残留 load");
    smoke_test::require(
        smoke_test::count_instructions(*unit, Instruction::Branch) == 0,
        "test0_1 优化后常量分支应被死分支消除删除");
}

void print_optimized_ir(const MFileUnit& mfile) {
    IRPrintOptions options;
    options.print_source_line_numbers = false;

    std::cout << "===== optimized test0_1 IR =====\n"
              << format_ir(mfile, options)
              << "===== end optimized test0_1 IR =====\n";
}

void print_frame_symbols(const RuntimeFrame& frame, std::string_view label) {
    std::cout << "===== frame exit symbols: " << label << " =====\n";
    if (frame.code == nullptr || frame.code->unit == nullptr) {
        std::cout << "  <no code unit>\n";
        return;
    }

    const CodeUnit& unit = *frame.code->unit;
    if (unit.slot_table.slots.empty()) {
        std::cout << "  <empty>\n";
        return;
    }

    for (const SlotInfo& slot : unit.slot_table.slots) {
        const std::size_t index = static_cast<std::size_t>(slot.slot.id.value());
        const ba_obj_ptr value = index < frame.slot_values.size()
            ? frame.slot_values[index]
            : ba_obj_ptr{};

        std::cout << "  " << slot.name
                  << " [" << slot_tag_name(slot.slot.tag) << "] = "
                  << format_runtime_value(value) << '\n';
    }
}

void print_base_workspace_symbols(const BaseWorkspace& workspace) {
    std::cout << "===== base workspace symbols =====\n";
    if (workspace.values.empty()) {
        std::cout << "  <empty>\n";
        return;
    }

    std::vector<std::pair<InternedString, ba_obj_ptr>> values;
    values.reserve(workspace.values.size());
    for (const auto& entry : workspace.values) {
        values.push_back(entry);
    }

    std::sort(
        values.begin(),
        values.end(),
        [](const auto& lhs, const auto& rhs) {
            return lhs.first < rhs.first;
        });

    for (const auto& entry : values) {
        std::cout << "  " << entry.first << " = "
                  << format_runtime_value(entry.second) << '\n';
    }
}

std::shared_ptr<CodeObject> make_code_object(IRBuildResult& result) {
    smoke_test::require(result.module != nullptr, "运行时 CodeObject 需要 IR module");
    smoke_test::require(result.mfile != nullptr, "运行时 CodeObject 需要 mfile");
    smoke_test::require(result.mfile->entry_unit != nullptr, "运行时 CodeObject 需要入口代码单元");

    auto code = std::make_shared<CodeObject>();
    code->unit = result.mfile->entry_unit;
    code->ir_owner = std::shared_ptr<IRModule>(std::move(result.module));
    return code;
}

void execute_optimized_ir(IRBuildResult& result) {
    InterpreterContext context;
    std::shared_ptr<CodeObject> code = make_code_object(result);

    RuntimeFrame base_frame;
    base_frame.context = &context;

    std::vector<ba_obj_ptr> outputs;
    {
        FrameScope base_scope(context, base_frame);

        RuntimeFrame frame;
        frame.code = code.get();
        frame.requested_nargout = 1;
        frame.initialize_storage();

        {
            FrameScope function_scope(context, frame);
            outputs = execute_frame(frame);
            print_frame_symbols(frame, code->unit->name);
        }
    }

    print_base_workspace_symbols(context.base_workspace);

    smoke_test::require(outputs.size() == 1, "test0_1 runtime should return one output");
    smoke_test::require(outputs.front() != nullptr, "test0_1 output must be bound");

    const double actual = outputs.front()->as_double();
    const double expected = std::sin(3.0) * 2.0;
    smoke_test::require(
        std::abs(actual - expected) < 1e-12,
        "test0_1 runtime output mismatch");
}

} // namespace
} // namespace baltam

int main() {
    int exit_code = 0;
    try {
        baltam::smoke_test::SmokeArtifacts artifacts =
            baltam::smoke_test::build_ir(TEST0_1_MFILE_PATH);
        baltam::verify_complete_ir(artifacts.result);
        baltam::verify_core_focus(artifacts.result);
        baltam::verify_other_important_checks(artifacts.result);
        baltam::optimize_ir(*artifacts.result.module);
        baltam::verify_constant_folding_result(artifacts.result);
        baltam::print_optimized_ir(*artifacts.result.mfile);
        baltam::execute_optimized_ir(artifacts.result);
    } catch (const std::exception& ex) {
        std::cerr << "test0_1_smoke 失败: " << ex.what() << '\n';
        exit_code = 1;
    }

    std::cout.flush();
    std::cerr.flush();
    std::_Exit(exit_code);
}
