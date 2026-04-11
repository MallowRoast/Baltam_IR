#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

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

struct MagicEndCallInfo {
    std::string base_name;
    std::optional<std::int64_t> index_position;
    std::optional<std::int64_t> total_index_count;
};

std::optional<std::int64_t> as_int64(const NumberNode::NumberValue& value) {
    if (const auto* item = std::get_if<std::int64_t>(&value)) {
        return *item;
    }
    if (const auto* item = std::get_if<std::uint64_t>(&value)) {
        return static_cast<std::int64_t>(*item);
    }
    return std::nullopt;
}

std::vector<MagicEndCallInfo> collect_magic_end_calls(const Function& function) {
    std::unordered_map<std::string, std::int64_t> number_defs;
    std::vector<MagicEndCallInfo> calls;

    for (const auto& block : function.blocks()) {
        if (block == nullptr) {
            continue;
        }

        for (IRNode* node : block->instructions()) {
            if (node == nullptr) {
                continue;
            }

            const auto& non_ssa = static_cast<const NonSSANode&>(*node);
            switch (non_ssa.type()) {
                case NonSSANode::Number: {
                    const auto& number = static_cast<const NumberNode&>(*node);
                    const std::optional<std::int64_t> value = as_int64(number.value());
                    if (value.has_value()) {
                        number_defs[number.result().name] = *value;
                    }
                    break;
                }
                case NonSSANode::Call: {
                    const auto& call = static_cast<const CallNode&>(*node);
                    if (call.callee_type() != CallNode::Direct || call.callee() != "magic_end" ||
                        call.inputs().size() != 3) {
                        break;
                    }

                    MagicEndCallInfo info;
                    info.base_name = call.inputs()[0].name;

                    const auto index_it = number_defs.find(call.inputs()[1].name);
                    if (index_it != number_defs.end()) {
                        info.index_position = index_it->second;
                    }

                    const auto total_it = number_defs.find(call.inputs()[2].name);
                    if (total_it != number_defs.end()) {
                        info.total_index_count = total_it->second;
                    }

                    calls.push_back(std::move(info));
                    break;
                }
                default:
                    break;
            }
        }
    }

    return calls;
}

bool has_magic_end_call(const std::vector<MagicEndCallInfo>& calls, const std::string& base_name,
                        std::int64_t index_position, std::int64_t total_index_count) {
    for (const MagicEndCallInfo& call : calls) {
        if (call.base_name == base_name && call.index_position == index_position &&
            call.total_index_count == total_index_count) {
            return true;
        }
    }
    return false;
}

void test_test4_cell_index_lowering() {
    const Module non_ssa_module = lower_test_script_to_non_ssa("test4");
    const std::string text = print_module(non_ssa_module);

    expect(text.find("call @__ir_cell_get__(%my_args") != std::string::npos,
           "test4 should lower cell expansion/get to __ir_cell_get__.");
    expect(text.find("call @__ir_cell_set__(%varargin") != std::string::npos,
           "test4 should lower cell write-back to __ir_cell_set__.");
}

void test_test8_magic_end_lowering() {
    const Module non_ssa_module = lower_test_script_to_non_ssa("test8");
    Function* entry_function = non_ssa_module.entry_function();
    expect(entry_function != nullptr, "test8 should have a non-SSA entry function.");

    const std::vector<MagicEndCallInfo> calls = collect_magic_end_calls(*entry_function);
    expect(!calls.empty(), "test8 should lower `end` into direct magic_end calls.");
    expect(has_magic_end_call(calls, "b", 1, 4),
           "test8 should lower b(1, end, 2, 1) with magic_end(b, 1, 4).");
    expect(has_magic_end_call(calls, "b", 0, 4),
           "test8 should lower the first end in b(end, 3, 2, end) with index 0.");
    expect(has_magic_end_call(calls, "b", 3, 4),
           "test8 should lower the last end in b(end, 3, 2, end) with index 3.");
    expect(has_magic_end_call(calls, "b", 0, 1),
           "test8 should lower single-index end as magic_end(b, 0, 1).");
    expect(has_magic_end_call(calls, "myname", 0, 1),
           "test8 should pass the variable itself into magic_end for shadowed names.");
}

}  // namespace

int main() {
    return run_runtime_test("test_cell_index_lowering", [] {
        test_test4_cell_index_lowering();
        test_test8_magic_end_lowering();
    });
}
