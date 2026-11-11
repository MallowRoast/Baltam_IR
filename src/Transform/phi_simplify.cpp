#include "Transform/phi_simplify.h"

namespace baltam {

bool simplify_trivial_phis(IRStageVersion& version) {
    bool any_changed = false;
    bool changed;
    do {
        changed = false;
        for (const auto& block : version.blocks()) {
            for (std::size_t i = 0; i < block->instructions().size();) {
                auto& instruction = *block->instructions()[i];
                if (instruction.type() != Instruction::Type::Phi)
                    break;
                const auto& phi = static_cast<const PhiInst&>(instruction);
                std::optional<ValueId> replacement;
                bool different = false;
                for (std::size_t n = 0; n < phi.operand_count(); ++n) {
                    const auto value = phi.incoming(n).value;
                    if (!value) {
                        different = true;
                        break;
                    }
                    if (*value == phi.result)
                        continue;
                    if (replacement && replacement != value) {
                        different = true;
                        break;
                    }
                    replacement = value;
                }
                if (replacement && !different) {
                    version.replace_all_uses(phi.result, *replacement);
                    block->erase_instruction(i);
                    changed = true;
                } else {
                    ++i;
                }
            }
        }
        any_changed |= changed;
    } while (changed);
    return any_changed;
}

} // baltam 命名空间
