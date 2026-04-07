#ifndef BALTAM_IR_INTERPRETER_INTERPRETER_H
#define BALTAM_IR_INTERPRETER_INTERPRETER_H

#include <memory>
#include <unordered_map>
#include <vector>

#include "ba_obj/ba_obj.h"
#include "ir/ir.h"

namespace baltam::interpreter {

using RuntimeObject = std::shared_ptr<ba_obj>;

struct RuntimeValue {
    enum Type {
        Concrete,
        Undef,
    };

    Type type = Undef;
    RuntimeObject object;
};

struct ExecResult {
    std::vector<RuntimeValue> outputs;
    std::unordered_map<ValueId, RuntimeValue> values;
};

ExecResult execute_function(Function& function, const std::vector<RuntimeObject>& args = {});

}  // namespace baltam::interpreter

#endif
