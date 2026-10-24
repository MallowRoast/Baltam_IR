#include "ir/ir_builder.h"
#include "bt_ast_interface/bt_ast_interface.h"
#include "ir/ir_unit.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

using namespace baltam;

std::size_t count_type(const IRStageVersion& version, Instruction::Type type) {
    std::size_t count = 0;
    for (const auto& block : version.blocks())
        for (const auto& instruction : block->instructions())
            count += instruction->type() == type;
    return count;
}

BasicBlock* find_block(const IRStageVersion& version, const std::string& label) {
    for (const auto& block : version.blocks())
        if (block->label() == label) return block.get();
    return nullptr;
}

bool check_script(const char* filename, bool with_function) {
    FunctionUnit stages("stage_names", {}, {});
    stages.set_current_stage(IRStage::SSA);
    if (stages.to_string().find("; stage: Based-SSA\n") == std::string::npos)
        return false;
    stages.set_current_stage(IRStage::OptimizedSSA);
    if (stages.to_string().find("; stage: Optimized-SSA\n") == std::string::npos)
        return false;

    const auto module = IRBuilder::build_file(filename);
    const auto* command = module->command_unit();
    if (module->module_type() != ModuleType::Script || !command ||
        command->parent() != module.get() ||
        command->slot_table().size() != 2 ||
        !command->stage_version()->entry_block() ||
        command->stage_version()->entry_block()->label() != "bb0" ||
        command->stage_version()->entry_block()->to_string().find("bb0:\n") != 0 ||
        module->to_string().find("script {\n") == std::string::npos ||
        command->stage_version()->entry_block()->terminator()->type() !=
            Instruction::Type::Return)
        return false;

    if (!with_function) return module->functions().empty();
    const auto* local = module->find_function("twice");
    if (module->entry_function() || module->functions().size() != 1 ||
        !local || local != module->functions().front().get() ||
        local->parent() != module.get() ||
        local->parameter_slots().size() != 1 ||
        local->return_slots().size() != 1 ||
        !local->current_version()->entry_block() ||
        local->to_string().find("; stage: Non-SSA\n") == std::string::npos ||
        module->to_string().find("define @twice(") == std::string::npos)
        return false;

    bool has_apply = false;
    for (const auto& block : command->stage_version()->blocks())
        for (const auto& instruction : block->instructions())
            has_apply |= instruction->type() == Instruction::Type::Apply;
    return has_apply;
}

bool check_nested_function(const char* filename) {
    const auto module = IRBuilder::build_file(filename);
    const auto* outer = module->entry_function();
    const auto* local = module->find_function("local_helper");
    if (module->module_type() != ModuleType::FunctionFile ||
        module->functions().size() != 2 || !outer || !local ||
        outer->name() != "nested_function" ||
        module->find_function("inner") || module->find_function("leaf") ||
        outer->nested_functions().size() != 1 ||
        !local->nested_functions().empty()) return false;

    const auto* inner = outer->nested_functions().front().get();
    if (inner->name() != "inner" || inner->parent() != module.get() ||
        inner->nested_functions().size() != 1 ||
        !inner->current_version()->entry_block()) return false;
    const auto* leaf = inner->nested_functions().front().get();
    if (leaf->name() != "leaf" || leaf->parent() != module.get() ||
        !leaf->nested_functions().empty() ||
        !leaf->current_version()->entry_block()) return false;

    const auto output = module->to_string();
    const auto outer_at = output.find("define @nested_function(");
    const auto inner_at = output.find("; nested in @nested_function\ndefine @inner(");
    const auto leaf_at = output.find("; nested in @inner\ndefine @leaf(");
    const auto local_at = output.find("define @local_helper(");
    return outer_at < inner_at && inner_at < leaf_at && leaf_at < local_at &&
           local_at != std::string::npos;
}

bool check(const char* filename, const std::string& syntax) {
    if (syntax == "script_only") return check_script(filename, false);
    if (syntax == "script_with_function") return check_script(filename, true);
    if (syntax == "nested_function") return check_nested_function(filename);
    std::string message;
    auto workspaces = bt_ast_interface::parse_mfile(filename, ParserOpts{}, message);
    if (!message.empty() || workspaces.size() != 1 ||
        !workspaces[0] || !workspaces[0]->ast || !workspaces[0]->is_mfun()) return false;
    FunctionUnit function(workspaces[0]->funname,
                          syntax == "parameter_slot" ? std::vector<std::string>{"x"}
                                                     : std::vector<std::string>{},
                          syntax == "parameter_slot" ? std::vector<std::string>{"y"}
                                                     : std::vector<std::string>{});
    IRBuilder(function).build(*workspaces[0]->ast);
    const auto version = function.current_version();
    if (!version->entry_block() ||
        version->entry_block() != version->blocks().front().get() ||
        count_type(*version, Instruction::Type::Return) != 1) return false;
    for (std::size_t i = 0; i < version->blocks().size(); ++i) {
        const auto& block = version->blocks()[i];
        if (block->label() != "bb" + std::to_string(i)) return false;
        if (!block->terminator()) return false;
        std::cout << block->label() << ":\n";
        for (const auto& instruction : block->instructions()) {
            if (instruction->parent() != block.get()) return false;
            std::cout << "  " << instruction->to_string() << '\n';
        }
    }
    for (std::uint32_t i = 0; i < version->values().size(); ++i) {
        const auto& info = version->values().find(ValueId{i});
        if (!info.def || !info.def->parent()) return false;
    }

    if (syntax == "constant") {
        const auto& instructions = version->entry_block()->instructions();
        return instructions.size() == 7 &&
            instructions[0]->to_string() == "%0 = const 2 : 1x1 double" &&
            instructions[2]->to_string() == "%1 = const 'aaa' : 1x3 char" &&
            instructions[4]->to_string() == "%2 = const \"ccc\" : 1x1 string" &&
            count_type(*version, Instruction::Type::Const) == 3;
    }
    if (syntax == "parameter_slot")
        return function.slot_table().size() == 2 &&
               count_type(*version, Instruction::Type::LoadSlot) == 1 &&
               count_type(*version, Instruction::Type::StoreSlot) == 1;
    if (syntax == "concat")
        return count_type(*version, Instruction::Type::Concat) == 4 &&
               count_type(*version, Instruction::Type::Const) >= 6;
    if (syntax == "cell")
        return count_type(*version, Instruction::Type::CellConstruct) == 3 &&
               count_type(*version, Instruction::Type::Const) >= 4;
    if (syntax == "brace_index") {
        const auto& instructions = version->entry_block()->instructions();
        if (count_type(*version, Instruction::Type::BraceReference) != 2 ||
            count_type(*version, Instruction::Type::BraceAssign) != 1 ||
            function.slot_table().size() != 3) return false;
        const BraceReferenceInst* first = nullptr;
        const BraceReferenceInst* second = nullptr;
        const BraceAssignInst* assign = nullptr;
        const StoreSlotInst* writeback = nullptr;
        for (std::size_t i = 0; i < instructions.size(); ++i) {
            if (instructions[i]->type() == Instruction::Type::BraceReference) {
                const auto* ref = static_cast<const BraceReferenceInst*>(instructions[i].get());
                if (!first) first = ref;
                else second = ref;
            }
            if (instructions[i]->type() == Instruction::Type::BraceAssign) {
                assign = static_cast<const BraceAssignInst*>(instructions[i].get());
                if (i + 1 >= instructions.size() ||
                    instructions[i + 1]->type() != Instruction::Type::StoreSlot)
                    return false;
                writeback = static_cast<const StoreSlotInst*>(instructions[i + 1].get());
            }
        }
        if (!first || !second || !assign || !writeback ||
            first->indices.size() != 1 || second->indices.size() != 1 ||
            assign->indices.size() != 1 ||
            version->values().find(first->result).form != ValueInfo::Form::MaybePack ||
            version->values().find(second->result).form != ValueInfo::Form::MaybePack ||
            writeback->slot != SlotId{0} || writeback->value != assign->result ||
            version->values().find(assign->result).def != assign ||
            version->values().find(assign->base).def->type() != Instruction::Type::LoadSlot ||
            static_cast<const LoadSlotInst&>(*version->values().find(assign->base).def).slot != SlotId{0} ||
            version->values().find(second->base).def->type() != Instruction::Type::LoadSlot ||
            static_cast<const LoadSlotInst&>(*version->values().find(second->base).def).slot != SlotId{0})
            return false;
        return first->to_string().find(" = brace_ref %") != std::string::npos &&
               assign->to_string().find(" = brace_assign %") != std::string::npos;
    }
    if (syntax == "paren_index") {
        const auto& instructions = version->entry_block()->instructions();
        if (count_type(*version, Instruction::Type::Apply) != 1 ||
            count_type(*version, Instruction::Type::ParenReference) != 1 ||
            count_type(*version, Instruction::Type::ParenAssign) != 1 ||
            count_type(*version, Instruction::Type::ParenDelete) != 1 ||
            count_type(*version, Instruction::Type::Const) != 6)
            return false;
        const ApplyInst* apply = nullptr;
        const ParenReferenceInst* ref = nullptr;
        const ParenAssignInst* assign = nullptr;
        const ParenDeleteInst* deletion = nullptr;
        std::vector<const StoreSlotInst*> writes;
        for (const auto& instruction : instructions) {
            switch (instruction->type()) {
                case Instruction::Type::Apply:
                    apply = static_cast<const ApplyInst*>(instruction.get()); break;
                case Instruction::Type::ParenReference:
                    ref = static_cast<const ParenReferenceInst*>(instruction.get()); break;
                case Instruction::Type::ParenAssign:
                    assign = static_cast<const ParenAssignInst*>(instruction.get()); break;
                case Instruction::Type::ParenDelete:
                    deletion = static_cast<const ParenDeleteInst*>(instruction.get()); break;
                case Instruction::Type::StoreSlot:
                    writes.push_back(static_cast<const StoreSlotInst*>(instruction.get())); break;
                default: break;
            }
        }
        if (!apply || !ref || !assign || !deletion || writes.size() != 7 ||
            !std::holds_alternative<InternedString>(apply->callee_or_base) ||
            std::get<InternedString>(apply->callee_or_base) != "A" ||
            !apply->arguments.empty() || apply->results.size() != 1 ||
            ref->indices.size() != 1 || assign->indices.size() != 1 ||
            deletion->indices.size() != 1 ||
            writes[0]->slot != writes[2]->slot ||
            writes[4]->slot != writes[5]->slot ||
            writes[5]->slot != writes[6]->slot ||
            writes[5]->value != assign->result ||
            writes[6]->value != deletion->result ||
            version->values().find(assign->result).def != assign ||
            version->values().find(deletion->result).def != deletion)
            return false;
        return apply->to_string().find(" = apply A()") != std::string::npos &&
               ref->to_string().find(" = paren_ref %") != std::string::npos &&
               assign->to_string().find(" = paren_assign %") != std::string::npos &&
               deletion->to_string().find(" = paren_delete %") != std::string::npos;
    }
    if (syntax == "call_statement") {
        const auto& instructions = version->entry_block()->instructions();
        if (instructions.size() != 5 ||
            count_type(*version, Instruction::Type::Call) != 1 ||
            count_type(*version, Instruction::Type::StoreSlot) != 1 ||
            function.slot_table().size() != 1 ||
            instructions[3]->type() != Instruction::Type::Call)
            return false;
        const auto& call = static_cast<const CallInst&>(*instructions[3]);
        return call.callee == "fun" && call.results.empty() &&
               call.arguments.size() == 1 &&
               call.dispatch_type == DispatchType::Dynamic &&
               call.effect() == Effect::MayWriteSlot &&
               version->values().find(call.arguments[0]).def->type() ==
                   Instruction::Type::LoadSlot &&
               call.to_string() == "call fun(%1)";
    }
    if (syntax == "dot_access") {
        const auto& instructions = version->entry_block()->instructions();
        if (count_type(*version, Instruction::Type::DotAccess) != 1 ||
            count_type(*version, Instruction::Type::DotReference) != 2 ||
            count_type(*version, Instruction::Type::DotAssign) != 1 ||
            count_type(*version, Instruction::Type::Apply) != 1 ||
            function.slot_table().size() != 3) return false;
        const DotAccessInst* unresolved = nullptr;
        const DotReferenceInst* static_ref = nullptr;
        const DotReferenceInst* dynamic_ref = nullptr;
        const DotAssignInst* assign = nullptr;
        const StoreSlotInst* writeback = nullptr;
        for (std::size_t i = 0; i < instructions.size(); ++i) {
            const auto* instruction = instructions[i].get();
            if (instruction->type() == Instruction::Type::DotAccess)
                unresolved = static_cast<const DotAccessInst*>(instruction);
            if (instruction->type() == Instruction::Type::DotReference) {
                const auto* ref = static_cast<const DotReferenceInst*>(instruction);
                if (!static_ref) static_ref = ref;
                else dynamic_ref = ref;
            }
            if (instruction->type() == Instruction::Type::DotAssign) {
                assign = static_cast<const DotAssignInst*>(instruction);
                if (i + 1 >= instructions.size() ||
                    instructions[i + 1]->type() != Instruction::Type::StoreSlot)
                    return false;
                writeback = static_cast<const StoreSlotInst*>(instructions[i + 1].get());
            }
        }
        if (!unresolved || !static_ref || !dynamic_ref || !assign || !writeback ||
            !std::holds_alternative<InternedString>(unresolved->base_or_name) ||
            std::get<InternedString>(unresolved->base_or_name) != "a" ||
            !std::holds_alternative<InternedString>(unresolved->member) ||
            std::get<InternedString>(unresolved->member) != "b" ||
            !std::holds_alternative<InternedString>(static_ref->member) ||
            std::get<InternedString>(static_ref->member) != "b" ||
            !std::holds_alternative<ValueId>(dynamic_ref->member) ||
            std::get<InternedString>(assign->member) != "b" ||
            writeback->slot != SlotId{0} || writeback->value != assign->result ||
            version->values().find(dynamic_ref->result).form != ValueInfo::Form::MaybePack)
            return false;
        return unresolved->to_string().find(" = dot_access a.b") != std::string::npos &&
               static_ref->to_string().find(" = dot_ref %") != std::string::npos &&
               assign->to_string().find(" = dot_assign %") != std::string::npos &&
               dynamic_ref->to_string().find(".(%") != std::string::npos;
    }
    if (syntax == "short_circuit") {
        if (function.slot_table().size() != 6 ||
            count_type(*version, Instruction::Type::StoreSlot) != 10 ||
            count_type(*version, Instruction::Type::Branch) != 9 ||
            count_type(*version, Instruction::Type::Intrinsic) != 9)
            return false;
        std::size_t logical_casts = 0;
        std::size_t target_stores = 0;
        for (const auto& block : version->blocks()) {
            for (const auto& instruction : block->instructions()) {
                if (instruction->type() == Instruction::Type::Intrinsic) {
                    const auto& intrinsic = static_cast<const IntrinsicInst&>(*instruction);
                    if (intrinsic.id == IntrinsicId::LogicalScalar &&
                        intrinsic.effect() == Effect::NoSlotWrite)
                        ++logical_casts;
                }
                if (instruction->type() == Instruction::Type::StoreSlot) {
                    const auto& store = static_cast<const StoreSlotInst&>(*instruction);
                    const auto& slot_name = function.slot_table().find(store.slot).name;
                    if (slot_name == "z" || slot_name == "w" || slot_name == "q") {
                        if (version->values().find(store.value).def->type() !=
                            Instruction::Type::Const) return false;
                        ++target_stores;
                    }
                }
            }
        }
        return logical_casts == 9 && target_stores == 6 &&
               find_block(*version, "bb1") &&
               find_block(*version, "bb2") &&
               find_block(*version, "bb3") &&
               find_block(*version, "bb4") &&
               static_cast<const BranchInst&>(
                   *version->entry_block()->terminator()).true_target ==
                   find_block(*version, "bb4") &&
               static_cast<const BranchInst&>(
                   *version->entry_block()->terminator()).false_target ==
                   find_block(*version, "bb2") &&
               find_block(*version, "bb1")->instructions()[0]->to_string().find(
                   "const true : 1x1 logical") != std::string::npos &&
               find_block(*version, "bb2")->instructions()[0]->to_string().find(
                   "const false : 1x1 logical") != std::string::npos;
    }
    if (syntax == "colon")
        return count_type(*version, Instruction::Type::Colon) == 7;
    if (syntax == "unary")
        return count_type(*version, Instruction::Type::Unary) == 10;
    if (syntax == "binary_arithmetic")
        return count_type(*version, Instruction::Type::Binary) == 20;
    if (syntax == "binary_compare_logic")
        return count_type(*version, Instruction::Type::Binary) == 16;
    if (syntax == "storage_decl")
        return count_type(*version, Instruction::Type::GlobalDecl) == 1 &&
               count_type(*version, Instruction::Type::PersistentDecl) == 1 &&
               function.slot_table().size() == 4;
    if (syntax == "if_else") {
        auto* entry = find_block(*version, "bb0");
        auto* yes = find_block(*version, "bb1");
        auto* no = find_block(*version, "bb2");
        auto* end = find_block(*version, "bb3");
        return entry && yes && no && end &&
            static_cast<const BranchInst&>(*entry->terminator()).true_target == yes &&
            static_cast<const BranchInst&>(*entry->terminator()).false_target == no &&
            static_cast<const GotoInst&>(*yes->terminator()).target == end &&
            static_cast<const GotoInst&>(*no->terminator()).target == end;
    }
    if (syntax == "switch_case")
        return count_type(*version, Instruction::Type::Intrinsic) == 2 &&
               count_type(*version, Instruction::Type::Branch) == 2 &&
               find_block(*version, "bb8");
    if (syntax == "for_loop") {
        const auto* outer_end = find_block(*version, "bb5");
        const auto* inner_end = find_block(*version, "bb10");
        const auto* outer_latch = find_block(*version, "bb4");
        const auto* inner_latch = find_block(*version, "bb9");
        if (!outer_end || !inner_end || !outer_latch || !inner_latch ||
            count_type(*version, Instruction::Type::Intrinsic) != 10) return false;
        const BasicBlock* targets[] = {inner_latch, inner_end, outer_latch, outer_end};
        const char* branches[] = {"bb11", "bb14", "bb17", "bb20"};
        for (std::size_t i = 0; i < 4; ++i) {
            auto* block = find_block(*version, branches[i]);
            if (!block || !block->terminator() ||
                block->terminator()->type() != Instruction::Type::Goto ||
                static_cast<const GotoInst&>(*block->terminator()).target != targets[i])
                return false;
        }
        return function.slot_table().find(SlotId{1}).name == "for.index.bb1" &&
               function.slot_table().find(SlotId{3}).name == "for.index.bb6";
    }
    if (syntax == "while_loop") {
        auto* outer_header = find_block(*version, "bb1");
        auto* outer_body = find_block(*version, "bb2");
        auto* outer_end = find_block(*version, "bb3");
        auto* inner_header = find_block(*version, "bb26");
        auto* inner_body = find_block(*version, "bb27");
        auto* inner_end = find_block(*version, "bb28");
        auto* first_for_latch = find_block(*version, "bb7");
        auto* first_for_end = find_block(*version, "bb8");
        auto* second_for_latch = find_block(*version, "bb24");
        if (!outer_header || !outer_body || !outer_end || !inner_header ||
            !inner_body || !inner_end || !first_for_latch || !first_for_end ||
            !second_for_latch) return false;
        for (const auto* header : {outer_header, inner_header}) {
            if (header->terminator()->type() != Instruction::Type::Branch ||
                count_type(*version, Instruction::Type::Branch) < 2) return false;
            const auto& branch = static_cast<const BranchInst&>(*header->terminator());
            if (version->values().find(branch.condition).def->parent() != header)
                return false;
        }
        if (static_cast<const BranchInst&>(*outer_header->terminator()).true_target != outer_body ||
            static_cast<const BranchInst&>(*outer_header->terminator()).false_target != outer_end ||
            static_cast<const BranchInst&>(*inner_header->terminator()).true_target != inner_body ||
            static_cast<const BranchInst&>(*inner_header->terminator()).false_target != inner_end)
            return false;
        const BasicBlock* targets[] = {
            first_for_latch, first_for_end, outer_header, outer_end};
        const char* branches[] = {"bb9", "bb12", "bb15", "bb18"};
        for (std::size_t i = 0; i < 4; ++i) {
            auto* block = find_block(*version, branches[i]);
            if (!block || !block->terminator() ||
                block->terminator()->type() != Instruction::Type::Goto ||
                static_cast<const GotoInst&>(*block->terminator()).target != targets[i])
                return false;
        }
        return static_cast<const GotoInst&>(*inner_body->terminator()).target == inner_end &&
               static_cast<const GotoInst&>(*inner_end->terminator()).target == second_for_latch;
    }
    return false;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc != 3) return 2;
    const std::string syntax = argv[2];
    if (syntax == "script_only" || syntax == "script_with_function" ||
        syntax == "nested_function") {
        try { return check(argv[1], syntax) ? 0 : 1; }
        catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
            return 1;
        }
    }
    if (baltam::bt_ast_interface::initialize(BT_AST_INIT_NO_BUILTIN) != BT_AST_INIT_SUCCESS)
        return 1;
    bool valid = false;
    try { valid = check(argv[1], argv[2]); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; }
    const int finalized = baltam::bt_ast_interface::finalize();
    if (!valid) std::cerr << "AST IR lowering failed: " << argv[2] << '\n';
    return valid && finalized == 0 ? 0 : 1;
}
