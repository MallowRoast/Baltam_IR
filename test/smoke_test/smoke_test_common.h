#pragma once

#include "ir/ir_lowering.h"
#include "ir/ir_print.h"
#include "ir/ir_verify.h"

#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace baltam::smoke_test {

[[noreturn]] inline void fail(std::string message) {
    throw std::runtime_error(std::move(message));
}

inline void require(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

struct SmokeArtifacts {
    IRBuildResult result;
    std::string printed_ir;
};

inline SmokeArtifacts build_ir(std::string_view mfile_path) {
    SmokeArtifacts artifacts;
    artifacts.result = parse_and_lower_mfile_to_ir(mfile_path);

    require(artifacts.result.module != nullptr, "结果 IR module 不能为空");
    require(artifacts.result.mfile != nullptr, "结果文件单元不能为空");

    IRPrintOptions print_options;
    print_options.load_source_from_path = true;
    artifacts.printed_ir = format_ir(*artifacts.result.module, print_options);
    return artifacts;
}

inline void require_no_error_diagnostics(const IRBuildResult& result) {
    for (const IRBuildDiagnostic& diagnostic : result.diagnostics) {
        if (diagnostic.severity == IRBuildDiagnostic::Error) {
            fail("lowering 不应产生 Error 诊断: " + diagnostic.message);
        }
    }
}

inline void require_ir_is_complete(const IRBuildResult& result) {
    require(result.module != nullptr, "结果 IR module 不能为空");
    require(result.mfile != nullptr, "结果文件单元不能为空");
    require_no_error_diagnostics(result);

    const IRVerifyResult verify_result = verify_ir(*result.module);
    if (!verify_result.ok()) {
        for (const IRVerifyDiagnostic& diagnostic : verify_result.diagnostics) {
            if (diagnostic.severity == IRVerifyDiagnostic::Error) {
                fail("IR verifier 不应产生 Error 诊断: " + diagnostic.message);
            }
        }
    }

    require(result.mfile->entry_unit != nullptr, "入口代码单元不能为空");
    require(!result.mfile->code_units.empty(), "至少应生成一个代码单元");

    bool entry_unit_found = false;
    for (const auto& unit_ptr : result.mfile->code_units) {
        require(unit_ptr != nullptr, "代码单元不能为空");

        const CodeUnit* unit = unit_ptr.get();
        require(unit->entry_block != nullptr, "代码单元必须有入口基本块");
        require(!unit->basic_blocks.empty(), "代码单元至少应有一个基本块");

        bool entry_block_found = false;
        for (const auto& block_ptr : unit->basic_blocks) {
            require(block_ptr != nullptr, "基本块不能为空");

            const BasicBlock* block = block_ptr.get();
            require(block->parent == unit, "基本块 parent 应指向所属代码单元");
            require(block->has_terminator(), "每个基本块都应以终结指令结束");

            if (block == unit->entry_block) {
                entry_block_found = true;
            }

            for (const auto& inst_ptr : block->instructions) {
                require(inst_ptr != nullptr, "指令不能为空");
                require(inst_ptr->parent == block, "指令 parent 应指向所属基本块");
            }
        }

        require(entry_block_found, "入口基本块必须属于当前代码单元");
        if (unit == result.mfile->entry_unit) {
            entry_unit_found = true;
        }
    }

    for (const auto& unit_ptr : result.module->anonymous_functions.functions) {
        require(unit_ptr != nullptr, "匿名函数体不能为空");

        const AnonymousFunctionUnit* unit = unit_ptr.get();
        require(unit->entry_block != nullptr, "匿名函数体必须有入口基本块");
        require(!unit->basic_blocks.empty(), "匿名函数体至少应有一个基本块");

        bool entry_block_found = false;
        for (const auto& block_ptr : unit->basic_blocks) {
            require(block_ptr != nullptr, "匿名函数体基本块不能为空");

            const BasicBlock* block = block_ptr.get();
            require(block->parent == unit, "匿名函数体基本块 parent 应指向所属代码单元");
            require(block->has_terminator(), "匿名函数体每个基本块都应以终结指令结束");

            if (block == unit->entry_block) {
                entry_block_found = true;
            }

            for (const auto& inst_ptr : block->instructions) {
                require(inst_ptr != nullptr, "匿名函数体指令不能为空");
                require(inst_ptr->parent == block, "匿名函数体指令 parent 应指向所属基本块");
            }
        }

        require(entry_block_found, "匿名函数体入口基本块必须属于当前代码单元");
    }

    require(entry_unit_found, "文件入口代码单元必须属于 code_units");
}

inline std::size_t count_instructions(const CodeUnit& unit, Instruction::Type type) {
    std::size_t count = 0;
    for (const auto& block : unit.basic_blocks) {
        for (const auto& inst : block->instructions) {
            if (inst->type() == type) {
                ++count;
            }
        }
    }
    return count;
}

inline const Instruction* find_first_instruction(
    const CodeUnit& unit,
    Instruction::Type type) {
    for (const auto& block : unit.basic_blocks) {
        for (const auto& inst : block->instructions) {
            if (inst->type() == type) {
                return inst.get();
            }
        }
    }
    return nullptr;
}

inline std::string find_line_containing(
    std::string_view text,
    std::string_view needle) {
    std::istringstream input{std::string(text)};
    std::string line;
    while (std::getline(input, line)) {
        if (line.find(needle) != std::string::npos) {
            return line;
        }
    }
    return {};
}

} // namespace baltam::smoke_test
