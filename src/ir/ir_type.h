#pragma once

#include <cstdint>
#include <iosfwd>

namespace baltam {

/**
 * @brief 第一版值类型原子。
 *
 * `TypeAtom` 只枚举类型格中的叶子类型。`Numeric`、`Text`、`Container`
 * 这类分类不作为 atom 存在，而是通过 `TypeSet` mask 表达。
 */
enum class TypeAtom : std::uint8_t {
    Logical,

    Int64,
    UInt64,
    Float64,
    Complex,

    Char,
    String,

    Cell,
    Struct,

    FunctionHandle,
};

/**
 * @brief bitset-backed union type。
 *
 * - `bits == 0` 表示 Bottom / Empty。
 * - `bits == all_known_type_bits()` 表示 Top / Any。
 * - 单 bit 表示单一精确 atom。
 * - 多 bit 表示 union。
 */
struct TypeSet {
    using bits_type = std::uint64_t;

    bits_type bits = 0;

    /**
     * @brief 判断当前集合是否为空类型集合。
     */
    [[nodiscard]] constexpr bool empty() const noexcept {
        return bits == 0;
    }

    /**
     * @brief 判断当前集合是否包含指定 atom。
     */
    [[nodiscard]] constexpr bool contains(TypeAtom atom) const noexcept;

    /**
     * @brief 判断当前集合是否为另一个集合的子集。
     */
    [[nodiscard]] constexpr bool is_subset_of(TypeSet other) const noexcept {
        return (bits & ~other.bits) == 0;
    }
};

/**
 * @brief 返回指定 atom 对应的 bit。
 */
[[nodiscard]] constexpr TypeSet::bits_type type_atom_bit(TypeAtom atom) noexcept {
    return TypeSet::bits_type{1} << static_cast<std::uint8_t>(atom);
}

constexpr bool TypeSet::contains(TypeAtom atom) const noexcept {
    return (bits & type_atom_bit(atom)) != 0;
}

/**
 * @brief 所有当前已知 atom 的全集 bitmask。
 */
[[nodiscard]] constexpr TypeSet::bits_type all_known_type_bits() noexcept {
    return
        type_atom_bit(TypeAtom::Logical) |
        type_atom_bit(TypeAtom::Int64) |
        type_atom_bit(TypeAtom::UInt64) |
        type_atom_bit(TypeAtom::Float64) |
        type_atom_bit(TypeAtom::Complex) |
        type_atom_bit(TypeAtom::Char) |
        type_atom_bit(TypeAtom::String) |
        type_atom_bit(TypeAtom::Cell) |
        type_atom_bit(TypeAtom::Struct) |
        type_atom_bit(TypeAtom::FunctionHandle);
}

/**
 * @brief Bottom / Empty 类型集合。
 */
[[nodiscard]] constexpr TypeSet bottom_type_set() noexcept {
    return TypeSet{0};
}

/**
 * @brief Top / Any 类型集合。
 */
[[nodiscard]] constexpr TypeSet any_type_set() noexcept {
    return TypeSet{all_known_type_bits()};
}

/**
 * @brief 单一 atom 类型集合。
 */
[[nodiscard]] constexpr TypeSet singleton_type_set(TypeAtom atom) noexcept {
    return TypeSet{type_atom_bit(atom)};
}

/**
 * @brief 两个类型集合的 join。
 */
[[nodiscard]] constexpr TypeSet join(TypeSet lhs, TypeSet rhs) noexcept {
    return TypeSet{lhs.bits | rhs.bits};
}

/**
 * @brief 两个类型集合的 meet。
 */
[[nodiscard]] constexpr TypeSet meet(TypeSet lhs, TypeSet rhs) noexcept {
    return TypeSet{lhs.bits & rhs.bits};
}

/**
 * @brief 判断两个类型集合是否相等。
 */
[[nodiscard]] constexpr bool operator==(TypeSet lhs, TypeSet rhs) noexcept {
    return lhs.bits == rhs.bits;
}

/**
 * @brief 判断两个类型集合是否不等。
 */
[[nodiscard]] constexpr bool operator!=(TypeSet lhs, TypeSet rhs) noexcept {
    return !(lhs == rhs);
}

/**
 * @brief 逻辑类型分类。
 */
[[nodiscard]] constexpr TypeSet logical_type_set() noexcept {
    return singleton_type_set(TypeAtom::Logical);
}

/**
 * @brief 整数类型分类。
 */
[[nodiscard]] constexpr TypeSet integer_type_set() noexcept {
    return join(
        singleton_type_set(TypeAtom::Int64),
        singleton_type_set(TypeAtom::UInt64));
}

/**
 * @brief 浮点/复数类型分类。
 */
[[nodiscard]] constexpr TypeSet floating_type_set() noexcept {
    return join(
        singleton_type_set(TypeAtom::Float64),
        singleton_type_set(TypeAtom::Complex));
}

/**
 * @brief 数值类型分类。
 */
[[nodiscard]] constexpr TypeSet numeric_type_set() noexcept {
    return join(
        logical_type_set(),
        join(integer_type_set(), floating_type_set()));
}

/**
 * @brief 文本类型分类。
 */
[[nodiscard]] constexpr TypeSet text_type_set() noexcept {
    return join(
        singleton_type_set(TypeAtom::Char),
        singleton_type_set(TypeAtom::String));
}

/**
 * @brief 容器类型分类。
 */
[[nodiscard]] constexpr TypeSet container_type_set() noexcept {
    return join(
        singleton_type_set(TypeAtom::Cell),
        singleton_type_set(TypeAtom::Struct));
}

/**
 * @brief 可调用类型分类。
 */
[[nodiscard]] constexpr TypeSet callable_type_set() noexcept {
    return singleton_type_set(TypeAtom::FunctionHandle);
}

/**
 * @brief 判断 `value` 是否可能属于 `category`。
 */
[[nodiscard]] constexpr bool maybe(TypeSet value, TypeSet category) noexcept {
    return (value.bits & category.bits) != 0;
}

/**
 * @brief 判断 `value` 是否必然属于 `category`。
 *
 * Bottom 不被视为“必然属于”任何分类。
 */
[[nodiscard]] constexpr bool definitely(TypeSet value, TypeSet category) noexcept {
    return !value.empty() && value.is_subset_of(category);
}

/**
 * @brief 返回 atom 的稳定调试名。
 */
[[nodiscard]] const char* type_atom_name(TypeAtom atom) noexcept;

/**
 * @brief 将类型集合格式化为稳定调试文本。
 */
std::ostream& operator<<(std::ostream& os, TypeSet type_set);

} // namespace baltam
