#pragma once

#include <bitset>
#include <cstddef>
#include <cstdint>
#include <iosfwd>

namespace baltam {

/**
 * @brief std::bitset-backed union type。
 *
 * - `bits == 0` 表示 Bottom / Empty。
 * - `bits == TypeSet::any().bits` 表示 Top / Any。
 * - 单 bit 表示单一精确叶子类型。
 * - 多 bit 表示 union。
 */
struct TypeSet {
    static constexpr std::size_t BitCount = 11;
    static_assert(BitCount <= 64, "TypeSet::BitCount must fit in uint64_t");

    using bits_type = std::bitset<BitCount>;

    bits_type bits{};

    constexpr TypeSet() noexcept = default;

    explicit constexpr TypeSet(std::uint64_t raw_bits) noexcept : bits(raw_bits) {}

    explicit TypeSet(bits_type raw_bits) noexcept : bits(raw_bits) {}

    [[nodiscard]] bool operator==(TypeSet other) const noexcept {
        return bits == other.bits;
    }

    [[nodiscard]] bool operator!=(TypeSet other) const noexcept {
        return bits != other.bits;
    }

    /**
     * @brief 判断当前集合是否为空类型集合。
     */
    [[nodiscard]] bool empty() const noexcept {
        return bits.none();
    }

    /**
     * @brief 判断当前集合是否完整包含另一个集合。
     */
    [[nodiscard]] bool is_superset_of(TypeSet other) const noexcept {
        return !other.empty() && other.is_subset_of(*this);
    }

    /**
     * @brief 判断当前集合是否为另一个集合的子集。
     */
    [[nodiscard]] bool is_subset_of(TypeSet other) const noexcept {
        return (bits & ~other.bits).none();
    }

    /**
     * @brief 返回两个类型集合的 join。
     */
    [[nodiscard]] TypeSet join(TypeSet other) const noexcept {
        return TypeSet{bits | other.bits};
    }

    /**
     * @brief 返回两个类型集合的 meet。
     */
    [[nodiscard]] TypeSet meet(TypeSet other) const noexcept {
        return TypeSet{bits & other.bits};
    }

    /**
     * @brief 判断当前集合是否可能属于某个分类。
     */
    [[nodiscard]] bool maybe(TypeSet category) const noexcept {
        return (bits & category.bits).any();
    }

    /**
     * @brief 判断当前集合是否必然属于某个分类。
     *
     * Bottom 不被视为“必然属于”任何分类。
     */
    [[nodiscard]] bool definitely(TypeSet category) const noexcept {
        return !empty() && is_subset_of(category);
    }

    [[nodiscard]] static constexpr TypeSet bottom() noexcept {
        return TypeSet{};
    }

    [[nodiscard]] static constexpr TypeSet any() noexcept {
        return TypeSet(AnyMask);
    }

    [[nodiscard]] static constexpr TypeSet logical() noexcept {
        return TypeSet(LogicalMask);
    }

    [[nodiscard]] static constexpr TypeSet int64() noexcept {
        return TypeSet(Int64Mask);
    }

    [[nodiscard]] static constexpr TypeSet uint64() noexcept {
        return TypeSet(UInt64Mask);
    }

    [[nodiscard]] static constexpr TypeSet float64() noexcept {
        return TypeSet(Float64Mask);
    }

    [[nodiscard]] static constexpr TypeSet complex() noexcept {
        return TypeSet(ComplexMask);
    }

    [[nodiscard]] static constexpr TypeSet char_array() noexcept {
        return TypeSet(CharMask);
    }

    [[nodiscard]] static constexpr TypeSet string_scalar() noexcept {
        return TypeSet(StringMask);
    }

    [[nodiscard]] static constexpr TypeSet cell_array() noexcept {
        return TypeSet(CellMask);
    }

    [[nodiscard]] static constexpr TypeSet struct_array() noexcept {
        return TypeSet(StructMask);
    }

    [[nodiscard]] static constexpr TypeSet function_handle() noexcept {
        return TypeSet(FunctionHandleMask);
    }

    [[nodiscard]] static constexpr TypeSet external_object() noexcept {
        return TypeSet(ExternalObjectMask);
    }

    [[nodiscard]] static constexpr TypeSet integer() noexcept {
        return TypeSet(Int64Mask | UInt64Mask);
    }

    [[nodiscard]] static constexpr TypeSet floating() noexcept {
        return TypeSet(Float64Mask | ComplexMask);
    }

    [[nodiscard]] static constexpr TypeSet text() noexcept {
        return TypeSet(CharMask | StringMask);
    }

    [[nodiscard]] static constexpr TypeSet container() noexcept {
        return TypeSet(CellMask | StructMask);
    }

    [[nodiscard]] static constexpr TypeSet callable() noexcept {
        return TypeSet(FunctionHandleMask);
    }

    [[nodiscard]] static constexpr TypeSet numeric() noexcept {
        return TypeSet(
            LogicalMask |
            Int64Mask |
            UInt64Mask |
            Float64Mask |
            ComplexMask);
    }

    friend std::ostream& operator<<(std::ostream& os, TypeSet type_set);

private:
    static constexpr std::uint64_t LogicalMask = std::uint64_t{1} << 0;
    static constexpr std::uint64_t Int64Mask = std::uint64_t{1} << 1;
    static constexpr std::uint64_t UInt64Mask = std::uint64_t{1} << 2;
    static constexpr std::uint64_t Float64Mask = std::uint64_t{1} << 3;
    static constexpr std::uint64_t ComplexMask = std::uint64_t{1} << 4;
    static constexpr std::uint64_t CharMask = std::uint64_t{1} << 5;
    static constexpr std::uint64_t StringMask = std::uint64_t{1} << 6;
    static constexpr std::uint64_t CellMask = std::uint64_t{1} << 7;
    static constexpr std::uint64_t StructMask = std::uint64_t{1} << 8;
    static constexpr std::uint64_t FunctionHandleMask = std::uint64_t{1} << 9;
    static constexpr std::uint64_t ExternalObjectMask = std::uint64_t{1} << 10;
    static constexpr std::uint64_t AnyMask =
        BitCount >= 64
            ? ~std::uint64_t{0}
            : ((std::uint64_t{1} << BitCount) - 1);
};

} // namespace baltam
