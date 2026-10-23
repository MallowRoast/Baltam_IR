#include "ir/ir_module.h"
#include "ir/world_counter.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace baltam {
// 测试用对象只验证 IR 对常量的共享持有，不依赖运行时对象库。
struct ba_obj {
    int test_value;
};
}

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_throws(const std::function<void()>& action, const char* message) {
    try {
        action();
    } catch (const std::logic_error&) {
        return;
    }
    throw std::runtime_error(message);
}

} // 匿名命名空间

int main() {
    using namespace baltam;

    auto& world_counter = WorldCounter::instance();
    require(&world_counter == &WorldCounter::instance() &&
                world_counter.current() == 0,
            "全局世界计数单例或初始值不正确");
    const auto first_world = world_counter.advance();
    require(first_world == 1 && world_counter.current() == first_world,
            "世界计数递增结果不正确");

    auto module = std::make_shared<IRModule>(
        SourceType::File, ModuleType::FunctionFile,
        std::filesystem::path("/project/tmp/../main.m"));
    require(module->source_path() == std::filesystem::path("/project/main.m"),
            "文件路径未规范化");
    require(world_counter.advance() == 2,
            "全局世界计数未继续递增");

    // 函数文件的首个模块级函数是主函数。
    auto main_ptr = std::make_unique<FunctionUnit>(
        "main", std::vector<std::string>{"x"}, std::vector<std::string>{"y"});
    auto helper_ptr = std::make_unique<FunctionUnit>(
        "helper", std::vector<std::string>{"v"}, std::vector<std::string>{"z"});
    FunctionUnit& main = *main_ptr;
    FunctionUnit& helper = *helper_ptr;
    std::vector<IndexedFunction> functions;
    functions.emplace_back("function/main", std::move(main_ptr));
    functions.emplace_back("local/helper", std::move(helper_ptr));
    module->set_functions(std::move(functions));
    FunctionUnit& inner = main.add_nested_function("inner", {"input"}, {"output"});
    FunctionUnit& deep = inner.add_nested_function("deep", {}, {});
    require(main.parameter_slots() == std::vector<SlotId>{SlotId{0}} &&
                main.return_slots() == std::vector<SlotId>{SlotId{1}} &&
                main.slot_table().find(main.parameter_slots()[0])->name == "x" &&
                main.slot_table().find(main.return_slots()[0])->name == "y" &&
                inner.parameter_slots() == std::vector<SlotId>{SlotId{0}} &&
                inner.return_slots() == std::vector<SlotId>{SlotId{1}} &&
                inner.slot_table().find(inner.parameter_slots()[0])->name == "input" &&
                inner.slot_table().find(inner.return_slots()[0])->name == "output",
            "函数的入参或出参槽位未按声明顺序保存");
    BasicBlock& main_entry = main.add_block(IRStage::NonSSA);
    BasicBlock& main_next = main.add_block(IRStage::NonSSA);
    BasicBlock& main_ssa = main.add_block(IRStage::SSA);
    BasicBlock& main_optimized = main.add_block(IRStage::OptimizedSSA);
    BasicBlock& inner_entry = inner.add_block(IRStage::NonSSA);
    require(main.non_ssa_blocks().front().get() == &main_entry && main_next.index() == 1 &&
                main.ssa_blocks().front().get() == &main_ssa &&
                main.optimized_ssa_blocks().front().get() == &main_optimized &&
                inner.non_ssa_blocks().front().get() == &inner_entry,
            "函数基本块的所有权或阶段不正确");
    auto constant = std::make_shared<const ba_obj>(ba_obj{42});
    std::weak_ptr<const ba_obj> weak_constant = constant;
    auto& non_ssa_const = main_entry.add_instruction<ConstInst>(constant, ValueId{0});
    main_entry.add_instruction<ConstInst>(constant, ValueId{1});
    auto& horizontal = main_entry.add_instruction<ConcatInst>(
        std::vector<ValueId>{ValueId{0}, ValueId{1}}, ValueId{2},
        ConcatInst::Axis::Horizontal, false);
    auto& ssa_const = main_ssa.add_instruction<ConstInst>(constant, ValueId{1});
    main_ssa.add_instruction<ConstInst>(constant, ValueId{2});
    auto& vertical = main_ssa.add_instruction<ConcatInst>(
        std::vector<ValueId>{ValueId{1}, ValueId{2}}, ValueId{3},
        ConcatInst::Axis::Vertical, false);
    for (std::uint32_t id = 3; id < 7; ++id) {
        main_next.add_instruction<ConstInst>(constant, ValueId{id});
    }
    auto& cell = main_next.add_instruction<CellConstructInst>(
        std::vector<ValueId>{ValueId{3}, ValueId{4}, ValueId{5}, ValueId{6}},
        2, 2, ValueId{7});
    require(non_ssa_const.type() == Instruction::Type::Const &&
                non_ssa_const.result == ValueId{0} &&
                ssa_const.result == ValueId{1} &&
                non_ssa_const.value == ssa_const.value &&
                non_ssa_const.value->test_value == 42 &&
                main_entry.instructions().front().get() == &non_ssa_const &&
                main_ssa.instructions().front().get() == &ssa_const,
            "常量指令未按所属基本块保存或共享常量对象");
    require(horizontal.type() == Instruction::Type::Concat &&
                horizontal.items.size() == 2 &&
                horizontal.items[0] == ValueId{0} &&
                horizontal.items[1] == ValueId{1} &&
                horizontal.result == ValueId{2} &&
                horizontal.axis == ConcatInst::Axis::Horizontal &&
                !horizontal.is_dynamic &&
                main_entry.instructions().back().get() == &horizontal &&
                vertical.axis == ConcatInst::Axis::Vertical &&
                vertical.result == ValueId{3} &&
                main_ssa.instructions().back().get() == &vertical,
            "并置指令未按顺序保存输入、结果或方向");
    require(cell.type() == Instruction::Type::CellConstruct &&
                cell.rows == 2 && cell.cols == 2 &&
                cell.items.size() == 4 &&
                cell.items[0] == ValueId{3} && cell.items[1] == ValueId{4} &&
                cell.items[2] == ValueId{5} && cell.items[3] == ValueId{6} &&
                cell.result == ValueId{7} &&
                main_next.instructions().back().get() == &cell,
            "元胞构造指令未按形状和源码顺序保存元素");
    const SlotId local_slot = main.slot_table().add_slot(
        "local", SlotTag::Local);
    const SlotId global_slot = main.slot_table().add_slot(
        "global", SlotTag::Global);
    const SlotId other_global_slot = main.slot_table().add_slot(
        "other_global", SlotTag::Global);
    const SlotId persistent_slot = main.slot_table().add_slot(
        "persistent", SlotTag::Persistent);
    auto& local_load = main_entry.add_instruction<LoadSlotInst>(
        ValueId{40}, local_slot);
    auto& local_store = main_entry.add_instruction<StoreSlotInst>(
        local_slot, ValueId{40});
    auto& global_load = main_ssa.add_instruction<LoadSlotInst>(
        ValueId{41}, global_slot);
    auto& global_store = main_ssa.add_instruction<StoreSlotInst>(
        global_slot, ValueId{41});
    require(local_load.type() == Instruction::Type::LoadSlot &&
                local_load.result == ValueId{40} &&
                local_load.slot == local_slot &&
                local_store.type() == Instruction::Type::StoreSlot &&
                main.slot_table().find(local_store.slot)->tag == SlotTag::Local &&
                local_store.value == ValueId{40} &&
                main.slot_table().find(global_load.slot)->tag == SlotTag::Global &&
                global_store.slot == global_slot &&
                global_store.value == ValueId{41} &&
                main_entry.instructions().back().get() == &local_store &&
                main_ssa.instructions().back().get() == &global_store,
            "槽位读取或写入未保存值编号及槽位类别");
    auto& colon_two = main_ssa.add_instruction<ColonInst>(
        ValueId{4}, ValueId{1}, std::nullopt, ValueId{2});
    auto& colon_three = main_ssa.add_instruction<ColonInst>(
        ValueId{5}, ValueId{1}, std::optional<ValueId>{ValueId{2}}, ValueId{2});
    require(colon_two.type() == Instruction::Type::Colon &&
                colon_two.result == ValueId{4} &&
                colon_two.start == ValueId{1} &&
                !colon_two.step && colon_two.stop == ValueId{2} &&
                colon_two.needs_runtime_dispatch &&
                colon_three.result == ValueId{5} &&
                colon_three.step && *colon_three.step == ValueId{2} &&
                colon_three.needs_runtime_dispatch &&
                main_ssa.instructions().back().get() == &colon_three,
            "冒号指令未区分省略步长和显式步长");
    auto& transpose = main_ssa.add_instruction<UnaryInst>(
        ValueId{30}, ValueId{1}, UnaryOp::Transpose);
    auto& ctranspose = main_ssa.add_instruction<UnaryInst>(
        ValueId{31}, ValueId{1}, UnaryOp::Ctranspose);
    auto& matrix_mul = main_ssa.add_instruction<BinaryInst>(
        ValueId{32}, ValueId{1}, ValueId{2}, BinaryOp::Mul);
    auto& element_mul = main_ssa.add_instruction<BinaryInst>(
        ValueId{33}, ValueId{1}, ValueId{2}, BinaryOp::ElemMul);
    auto& logical_and = main_ssa.add_instruction<BinaryInst>(
        ValueId{34}, ValueId{1}, ValueId{2}, BinaryOp::And);
    require(transpose.type() == Instruction::Type::Unary &&
                transpose.op == UnaryOp::Transpose &&
                transpose.result == ValueId{30} &&
                ctranspose.op == UnaryOp::Ctranspose &&
                ctranspose.operand == ValueId{1} &&
                ctranspose.needs_runtime_dispatch &&
                matrix_mul.type() == Instruction::Type::Binary &&
                matrix_mul.op == BinaryOp::Mul &&
                element_mul.op == BinaryOp::ElemMul &&
                element_mul.lhs == ValueId{1} &&
                element_mul.rhs == ValueId{2} &&
                logical_and.op == BinaryOp::And &&
                logical_and.needs_runtime_dispatch,
            "一元或二元运算未保留运算种类及动态分派状态");
    require_throws([&] {
        main_ssa.add_instruction<BinaryInst>(
            ValueId{36}, ValueId{1}, ValueId{2},
            static_cast<BinaryOp>(255));
    }, "二元运算未拒绝无效操作类型");
    auto& magic_colon = main_ssa.add_instruction<MagicColonInst>(ValueId{6});
    require(magic_colon.type() == Instruction::Type::MagicColon &&
                magic_colon.result == ValueId{6} &&
                main_ssa.instructions().back().get() == &magic_colon,
            "索引冒号占位指令未保存结果编号");
    auto& magic_end = main_ssa.add_instruction<MagicEndInst>(
        std::vector<MagicEndInst::MagicEndContext>{
            {InternedString{"fun"}, 1, 1},
            {SlotId{0}, 2, 2}}, ValueId{7});
    auto& bound_end = main_ssa.add_instruction<MagicEndInst>(
        std::vector<MagicEndInst::MagicEndContext>{{ValueId{1}, 1, 1}},
        ValueId{8});
    require(magic_end.type() == Instruction::Type::MagicEnd &&
                magic_end.result == ValueId{7} &&
                magic_end.candidate_contexts.size() == 2 &&
                std::holds_alternative<InternedString>(
                    magic_end.candidate_contexts[0].callee_or_base) &&
                std::get<InternedString>(
                    magic_end.candidate_contexts[0].callee_or_base) == "fun" &&
                magic_end.candidate_contexts[1].dim == 2 &&
                magic_end.candidate_contexts[1].nindices == 2 &&
                std::holds_alternative<SlotId>(
                    magic_end.candidate_contexts[1].callee_or_base) &&
                std::get<SlotId>(magic_end.candidate_contexts[1].callee_or_base)
                    == SlotId{0} &&
                std::holds_alternative<ValueId>(
                    bound_end.candidate_contexts[0].callee_or_base) &&
                main_ssa.instructions().back().get() == &bound_end,
            "索引 end 指令未保留候选顺序或对象绑定类型");
    require_throws([&] {
        main_ssa.add_instruction<MagicEndInst>(
            std::vector<MagicEndInst::MagicEndContext>{
                {InternedString{"A"}, 0, 1}}, ValueId{9});
    }, "索引 end 指令未拒绝无效维度");
    require_throws([&] {
        main_next.add_instruction<CellConstructInst>(
            std::vector<ValueId>{ValueId{3}}, 2, 2, ValueId{8});
    }, "元胞构造指令未拒绝元素数量与形状不一致");
    require_throws([&] {
        main_entry.add_instruction<ConcatInst>(
            std::vector<ValueId>{}, ValueId{3},
            ConcatInst::Axis::Horizontal, false);
    }, "并置指令未拒绝空输入");
    require_throws([&] {
        main_entry.add_instruction<ConstInst>(nullptr, ValueId{2});
    }, "常量指令未拒绝空值");
    auto& global_decl = main_entry.add_instruction<GlobalDeclInst>(
        std::vector<SlotId>{global_slot, other_global_slot});
    auto& persistent_decl = main_entry.add_instruction<PersistentDeclInst>(
        std::vector<SlotId>{persistent_slot});
    require(global_decl.type() == Instruction::Type::GlobalDecl &&
                global_decl.slots.size() == 2 &&
                global_decl.slots[0] == global_slot &&
                global_decl.slots[1] == other_global_slot &&
                persistent_decl.type() == Instruction::Type::PersistentDecl &&
                persistent_decl.slots.size() == 1 &&
                persistent_decl.slots[0] == persistent_slot &&
                main.slot_table().find(persistent_decl.slots[0])->tag ==
                    SlotTag::Persistent &&
                main_entry.terminator() == nullptr,
            "全局变量或持久变量声明未保存对应槽位");
    require_throws([&] {
        main_entry.add_instruction<GlobalDeclInst>(
            std::vector<SlotId>{});
    }, "全局变量声明未拒绝空槽位列表");
    require_throws([&] {
        main_entry.add_instruction<PersistentDeclInst>(
            std::vector<SlotId>{});
    }, "持久变量声明未拒绝空槽位列表");
    auto& function_return = main_next.add_instruction<ReturnInst>(
        std::vector<ValueId>{ValueId{7}});
    require(function_return.type() == Instruction::Type::Return &&
                function_return.values.size() == 1 &&
                function_return.values[0] == ValueId{7} &&
                main_next.terminator() == &function_return,
            "函数返回指令未保存返回值或终结基本块");
    constant.reset();
    require(!weak_constant.expired(), "常量对象未随指令存活");
    require(module->entry_function() == &main && module->functions().front().get() == &main &&
                module->functions()[1].get() == &helper,
            "首个模块级函数应为入口函数");
    require(module->find_function("function/main") == &main &&
                module->find_function("local/helper") == &helper &&
                module->find_function("nested/main/inner") == nullptr &&
                main.nested_functions().front().get() == &inner &&
                inner.nested_functions().front().get() == &deep,
            "模块函数索引或嵌套函数所有权不正确");
    std::vector<IndexedFunction> duplicate_functions;
    duplicate_functions.emplace_back("function/first",
        std::make_unique<FunctionUnit>("first", std::vector<std::string>{},
                                       std::vector<std::string>{}));
    duplicate_functions.emplace_back("function/first",
        std::make_unique<FunctionUnit>("second", std::vector<std::string>{},
                                       std::vector<std::string>{}));
    IRModule duplicate(SourceType::Memory, ModuleType::FunctionFile, std::nullopt);
    require_throws([&] { duplicate.set_functions(std::move(duplicate_functions)); },
                   "重复的函数索引键未被拒绝");
    require(duplicate_functions[0].second && duplicate_functions[1].second,
            "校验失败后函数所有权不应转移");

    // 运行时保留模块引用后，内部函数指针在缓存引用释放时仍有效。
    std::weak_ptr<IRModule> weak = module;
    std::shared_ptr<const IRModule> retained = module;
    module.reset();
    require(!weak.expired() && retained->entry_function() == &main,
            "模块仍被持有时函数指针应保持有效");
    retained.reset();
    require(weak.expired(), "最后一个持有者释放后模块仍未销毁");
    require(weak_constant.expired(), "模块销毁后常量对象仍未释放");

    IRModule script(SourceType::File, ModuleType::Script,
                    std::filesystem::path("/project/script.m"));
    auto script_unit_ptr = std::make_unique<CommandUnit>();
    CommandUnit& script_unit = *script_unit_ptr;
    script.set_command_unit(std::move(script_unit_ptr));
    BasicBlock& script_entry = script_unit.add_block();
    require(script_unit.blocks().front().get() == &script_entry && script_entry.index() == 0,
            "脚本基本块未由命令单元持有");
    std::vector<IndexedFunction> script_functions;
    script_functions.emplace_back("local/helper",
        std::make_unique<FunctionUnit>("helper", std::vector<std::string>{},
                                       std::vector<std::string>{}));
    script.set_functions(std::move(script_functions));

    IRModule class_file(SourceType::File, ModuleType::ClassFile,
                        std::filesystem::path("/project/MyClass.m"));
    std::vector<IndexedFunction> class_functions;
    class_functions.emplace_back("method/MyClass/run",
        std::make_unique<FunctionUnit>("run", std::vector<std::string>{},
                                       std::vector<std::string>{}));
    class_file.set_functions(std::move(class_functions));
    require(class_file.module_type() == ModuleType::ClassFile &&
                class_file.find_function("method/MyClass/run") != nullptr,
            "类文件的方法未被正确保存");

    IRModule command(SourceType::Command, ModuleType::Command, std::nullopt);
    auto command_unit_ptr = std::make_unique<CommandUnit>();
    CommandUnit& command_unit = *command_unit_ptr;
    command.set_command_unit(std::move(command_unit_ptr));
    BasicBlock& condition_block = command_unit.add_block();
    BasicBlock& then_block = command_unit.add_block();
    BasicBlock& exit_block = command_unit.add_block();
    condition_block.add_instruction<ConstInst>(
        std::make_shared<const ba_obj>(ba_obj{1}), ValueId{0});
    auto& branch = condition_block.add_instruction<BranchInst>(
        ValueId{0}, &then_block, &exit_block);
    auto& jump = then_block.add_instruction<GotoInst>(&exit_block);
    auto& command_return = exit_block.add_instruction<ReturnInst>(
        std::vector<ValueId>{});
    require(branch.type() == Instruction::Type::Branch &&
                branch.condition == ValueId{0} &&
                branch.true_target == &then_block &&
                branch.false_target == &exit_block &&
                condition_block.terminator() == &branch &&
                jump.type() == Instruction::Type::Goto &&
                jump.target == &exit_block &&
                then_block.terminator() == &jump &&
                command_return.values.empty() &&
                exit_block.terminator() == &command_return,
            "条件跳转、无条件跳转或空返回未正确终结基本块");
    require_throws([&] {
        condition_block.add_instruction<ReturnInst>(std::vector<ValueId>{});
    }, "终结指令之后仍允许添加指令");
    require_throws([&] {
        GotoInst invalid(nullptr);
    }, "无条件跳转未拒绝空目标");

    IRModule incomplete(SourceType::Memory, ModuleType::FunctionFile, std::nullopt);
    require(incomplete.entry_function() == nullptr,
            "尚未加入函数的模块不应有入口");
    require_throws([] {
        IRModule bad(SourceType::File, ModuleType::Script, std::nullopt);
    }, "缺少路径的文件模块未被拒绝");
    require_throws([] {
        IRModule bad(SourceType::Memory, ModuleType::Command, std::nullopt);
    }, "命令模块类型与源码类型不匹配时未被拒绝");
}
