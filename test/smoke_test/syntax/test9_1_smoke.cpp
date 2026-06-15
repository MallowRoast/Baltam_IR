#include "smoke_test_common.h"

#include <algorithm>
#include <iostream>
#include <string_view>
#include <vector>

namespace baltam {
namespace {

const FunctionUnit* find_function(const MFileUnit& mfile, std::string_view name) {
    for (const auto& unit_ptr : mfile.code_units) {
        if (unit_ptr != nullptr && unit_ptr->is_function() && unit_ptr->name == name) {
            return static_cast<const FunctionUnit*>(unit_ptr.get());
        }
    }
    return nullptr;
}

std::vector<const PersistentDeclInst*> find_persistent_decls(const CodeUnit& unit) {
    std::vector<const PersistentDeclInst*> decls;
    for (const auto& block_ptr : unit.basic_blocks) {
        for (const auto& inst_ptr : block_ptr->instructions) {
            if (inst_ptr != nullptr && inst_ptr->type() == Instruction::PersistentDecl) {
                decls.push_back(static_cast<const PersistentDeclInst*>(inst_ptr.get()));
            }
        }
    }
    return decls;
}

const SlotInfo* require_slot(
    const CodeUnit& unit,
    std::string_view name,
    SlotTag tag,
    const char* message) {
    const SlotInfo* slot = smoke_test::find_slot_by_name(unit, name);
    smoke_test::require(slot != nullptr, message);
    smoke_test::require(slot->slot.tag == tag, message);
    return slot;
}

void require_declares_slots(
    const CodeUnit& unit,
    const std::vector<std::string_view>& names,
    const char* message) {
    const std::vector<const PersistentDeclInst*> decls = find_persistent_decls(unit);
    smoke_test::require(decls.size() == 1, message);
    smoke_test::require(decls.front()->slots.size() == names.size(), message);

    for (std::string_view name : names) {
        const SlotInfo* slot = require_slot(unit, name, SlotTag::Persistent, message);
        smoke_test::require(
            std::find(decls.front()->slots.begin(), decls.front()->slots.end(), slot->slot) !=
                decls.front()->slots.end(),
            message);
    }
}

std::size_t count_loads(const CodeUnit& unit, Slot slot) {
    std::size_t count = 0;
    for (const auto& block_ptr : unit.basic_blocks) {
        for (const auto& inst_ptr : block_ptr->instructions) {
            if (inst_ptr == nullptr || inst_ptr->type() != Instruction::LoadSlot) {
                continue;
            }
            const auto& load = static_cast<const LoadSlotInst&>(*inst_ptr);
            if (load.slot == slot) {
                ++count;
            }
        }
    }
    return count;
}

std::size_t count_stores(const CodeUnit& unit, Slot slot) {
    std::size_t count = 0;
    for (const auto& block_ptr : unit.basic_blocks) {
        for (const auto& inst_ptr : block_ptr->instructions) {
            if (inst_ptr == nullptr || inst_ptr->type() != Instruction::StoreSlot) {
                continue;
            }
            const auto& store = static_cast<const StoreSlotInst&>(*inst_ptr);
            if (store.slot == slot) {
                ++count;
            }
        }
    }
    return count;
}

void verify_main_function(const FunctionUnit& function) {
    smoke_test::require(function.param_slots.size() == 1, "test9_1 应声明一个输入参数");
    smoke_test::require(function.return_slots.size() == 1, "test9_1 应声明一个返回值");
    require_slot(function, "a", SlotTag::Arg, "test9_1 参数 a 应为 Arg slot");
    require_slot(function, "z", SlotTag::Ret, "test9_1 返回 z 应为 Ret slot");
    require_declares_slots(
        function,
        {"x", "y"},
        "test9_1 主函数应声明两个 persistent slot");

    const Slot x = require_slot(function, "x", SlotTag::Persistent, "x 应为 Persistent slot")->slot;
    const Slot y = require_slot(function, "y", SlotTag::Persistent, "y 应为 Persistent slot")->slot;
    smoke_test::require(
        count_stores(function, x) == 1,
        "x 赋值应写入 Persistent slot");
    smoke_test::require(
        count_loads(function, y) == 1,
        "y 读取应来自 Persistent slot");
}

void verify_local_function(const FunctionUnit& function) {
    smoke_test::require(function.param_slots.empty(), "f 不应声明输入参数");
    smoke_test::require(function.return_slots.size() == 1, "f 应声明一个返回值");
    require_slot(function, "z", SlotTag::Ret, "f 返回 z 应为 Ret slot");
    require_declares_slots(
        function,
        {"x"},
        "f 应声明 x persistent slot");

    const Slot x = require_slot(function, "x", SlotTag::Persistent, "f x 应为 Persistent slot")->slot;
    smoke_test::require(
        count_loads(function, x) == 1,
        "f 应从 Persistent slot 读取 x");
}

} // namespace
} // namespace baltam

int main() {
    try {
        const baltam::smoke_test::SmokeArtifacts artifacts =
            baltam::smoke_test::build_ir(TEST9_1_MFILE_PATH);
        baltam::smoke_test::require_ir_is_complete(artifacts.result);
        baltam::smoke_test::require(artifacts.result.mfile->is_function_file(), "test9_1 应为函数文件");
        baltam::smoke_test::require(
            artifacts.result.mfile->code_units.size() == 2,
            "test9_1 文件应包含主函数和一个 local function");

        const baltam::FunctionUnit* function =
            baltam::find_function(*artifacts.result.mfile, "test9_1");
        const baltam::FunctionUnit* f =
            baltam::find_function(*artifacts.result.mfile, "f");
        baltam::smoke_test::require(function != nullptr, "应找到 test9_1 函数单元");
        baltam::smoke_test::require(f != nullptr, "应找到 f local function");

        baltam::verify_main_function(*function);
        baltam::verify_local_function(*f);
    } catch (const std::exception& ex) {
        std::cerr << "test9_1_smoke 失败: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
