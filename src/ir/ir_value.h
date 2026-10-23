#pragma once

#include <cstdint>
#include <string>
#include <variant>

namespace baltam {

// 编号的范围和绑定状态由所属代码单元的表检查。
struct ValueId {
    std::uint32_t index;
};

struct SlotId {
    std::uint32_t index;
};

constexpr bool operator==(ValueId lhs, ValueId rhs) noexcept {
    return lhs.index == rhs.index;
}

constexpr bool operator!=(ValueId lhs, ValueId rhs) noexcept {
    return !(lhs == rhs);
}

constexpr bool operator==(SlotId lhs, SlotId rhs) noexcept {
    return lhs.index == rhs.index;
}

constexpr bool operator!=(SlotId lhs, SlotId rhs) noexcept {
    return !(lhs == rhs);
}

static_assert(sizeof(ValueId) == sizeof(std::uint32_t));
static_assert(sizeof(SlotId) == sizeof(std::uint32_t));

// 槽位类别由所属代码单元的槽位表保存，运行时按类别解释。
enum class SlotTag : std::uint8_t {
    BaseVar,
    ScriptVar,
    Local,
    Capture,
    InternalLocal,
    Global,
    Persistent,
    Nargin,
    Nargout,
    Varargin,
    Varargout,
};

struct Slot {
    SlotId id;
    SlotTag tag = SlotTag::Local;
};

// 当前用字符串保存名字文本；后续可接入真正的字符串池。
using InternedString = std::string;

// 操作数只表示已求值的结果或尚未消歧的名字；已绑定变量须先通过 LoadSlotInst 取得 ValueId。
using Operand = std::variant<ValueId, InternedString>;

} // baltam 命名空间
