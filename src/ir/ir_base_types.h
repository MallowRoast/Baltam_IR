#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <ostream>
#include <string>

namespace baltam {

/**
 * @brief 强类型实体 ID 模板。
 *
 * 该模板通过不同的 Tag 类型区分不同实体的 ID，避免不同 ID 类型之间的误用。
 *
 * @tparam Tag 用于区分实体类别的标签类型。
 */
template <typename Tag>
class EntityId {
public:
    using underlying_type = std::uint32_t;

    static constexpr underlying_type kInvalidValue =
        std::numeric_limits<underlying_type>::max();

    /**
     * @brief 构造一个无效 ID。
     */
    constexpr EntityId() noexcept = default;

    /**
     * @brief 根据底层整数值构造一个 ID。
     *
     * @param value 底层整数值。
     */
    explicit constexpr EntityId(underlying_type value) noexcept : value_(value) {}

    /**
     * @brief 返回一个无效 ID。
     *
     * @return 无效 ID 对象。
     */
    [[nodiscard]] static constexpr EntityId invalid() noexcept { return EntityId(); }

    /**
     * @brief 判断当前 ID 是否有效。
     *
     * @return 若不是无效哨兵值则返回 true。
     */
    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return value_ != kInvalidValue;
    }

    /**
     * @brief 获取底层整数值。
     *
     * @return 当前 ID 的底层整数表示。
     */
    [[nodiscard]] constexpr underlying_type value() const noexcept { return value_; }

    /**
     * @brief 将 ID 显式转换为布尔值。
     *
     * @return ID 有效时返回 true。
     */
    constexpr explicit operator bool() const noexcept { return is_valid(); }

    /**
     * @brief 判断两个 ID 是否相等。
     *
     * @param lhs 左侧 ID。
     * @param rhs 右侧 ID。
     * @return 两者底层值相等时返回 true。
     */
    friend constexpr bool operator==(EntityId lhs, EntityId rhs) noexcept {
        return lhs.value_ == rhs.value_;
    }

    /**
     * @brief 判断两个 ID 是否不相等。
     *
     * @param lhs 左侧 ID。
     * @param rhs 右侧 ID。
     * @return 两者底层值不相等时返回 true。
     */
    friend constexpr bool operator!=(EntityId lhs, EntityId rhs) noexcept {
        return !(lhs == rhs);
    }

    /**
     * @brief 判断左侧 ID 是否小于右侧 ID。
     *
     * @param lhs 左侧 ID。
     * @param rhs 右侧 ID。
     * @return 左侧底层值更小时返回 true。
     */
    friend constexpr bool operator<(EntityId lhs, EntityId rhs) noexcept {
        return lhs.value_ < rhs.value_;
    }

    /**
     * @brief 判断左侧 ID 是否小于等于右侧 ID。
     *
     * @param lhs 左侧 ID。
     * @param rhs 右侧 ID。
     * @return 左侧底层值不大于右侧时返回 true。
     */
    friend constexpr bool operator<=(EntityId lhs, EntityId rhs) noexcept {
        return lhs.value_ <= rhs.value_;
    }

    /**
     * @brief 判断左侧 ID 是否大于右侧 ID。
     *
     * @param lhs 左侧 ID。
     * @param rhs 右侧 ID。
     * @return 左侧底层值更大时返回 true。
     */
    friend constexpr bool operator>(EntityId lhs, EntityId rhs) noexcept {
        return lhs.value_ > rhs.value_;
    }

    /**
     * @brief 判断左侧 ID 是否大于等于右侧 ID。
     *
     * @param lhs 左侧 ID。
     * @param rhs 右侧 ID。
     * @return 左侧底层值不小于右侧时返回 true。
     */
    friend constexpr bool operator>=(EntityId lhs, EntityId rhs) noexcept {
        return lhs.value_ >= rhs.value_;
    }

private:
    underlying_type value_ = kInvalidValue;
};

/**
 * @brief 将实体 ID 输出到流中。
 *
 * 有效 ID 输出底层数值，无效 ID 输出占位文本。
 *
 * @tparam Tag ID 的标签类型。
 * @param os 输出流。
 * @param id 待输出的实体 ID。
 * @return 输出流对象。
 */
template <typename Tag>
inline std::ostream& operator<<(std::ostream& os, EntityId<Tag> id) {
    if (!id.is_valid()) {
        return os << "<invalid>";
    }
    return os << id.value();
}

/**
 * @brief `SlotId` 的标签类型。
 */
struct SlotIdTag;

/**
 * @brief `Slot` 的强类型 ID。
 */
using SlotId = EntityId<SlotIdTag>;

/**
 * @brief `ValueId` 的标签类型。
 */
struct ValueIdTag;

/**
 * @brief IR 临时值的强类型 ID。
 */
using ValueId = EntityId<ValueIdTag>;

/**
 * @brief 第一版字符串名字承载类型。
 *
 * 当前阶段直接使用 `std::string` 保存名字文本，后续如果字符串池成为热点或需要稳定句柄，
 * 再切换到真正的 interned string 实现。
 */
using InternedString = std::string;

/**
 * @brief 无效的 `SlotId` 常量。
 */
inline constexpr SlotId InvalidSlotId = SlotId::invalid();

/**
 * @brief slot 的静态类别。
 */
enum class SlotTag : std::uint8_t {
    BaseVar,
    ScriptVar,
    Local,
    Arg,
    Ret,
    Capture,
    InternalLocal,
    Global,
    Persistent,
    Nargin,
    Nargout,
    Varargin,
    Varargout,
};

/**
 * @brief slot 固定值类型事实。
 */
enum class SlotValueType : std::uint8_t {
    Unknown,
    Int64Scalar,
    LogicalScalar,
};

/**
 * @brief 静态变量槽位。
 */
struct Slot {
    SlotId id = InvalidSlotId;
    SlotTag tag = SlotTag::Local;

    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return id.is_valid();
    }
};

inline constexpr Slot InvalidSlot{};

[[nodiscard]] constexpr inline bool operator==(Slot lhs, Slot rhs) noexcept {
    return lhs.id == rhs.id && lhs.tag == rhs.tag;
}

[[nodiscard]] constexpr inline bool operator!=(Slot lhs, Slot rhs) noexcept {
    return !(lhs == rhs);
}

/**
 * @brief 无效的 `ValueId` 常量。
 */
inline constexpr ValueId InvalidValueId = ValueId::invalid();

/**
 * @brief 归一化路径类型。
 *
 * 第一版直接复用 `std::filesystem::path` 作为路径承载类型，路径归一化策略由外层加载流程保证。
 */
using NormalizedPath = std::filesystem::path;

/**
 * @brief 源码范围。
 *
 * 该类型使用半开区间 `[begin_offset, end_offset)` 表示文件内的字节偏移范围。
 * 文件身份由外层 IR 节点持有，因此 `SourceSpan` 只保存轻量的区间信息。
 */
struct SourceSpan {
    using offset_type = std::uint32_t;

    static constexpr offset_type kInvalidOffset =
        std::numeric_limits<offset_type>::max();

    /**
     * @brief 构造一个无效源码范围。
     */
    constexpr SourceSpan() noexcept = default;

    /**
     * @brief 根据起止偏移构造源码范围。
     *
     * @param begin_offset 起始字节偏移，包含该位置。
     * @param end_offset 结束字节偏移，不包含该位置。
     */
    constexpr SourceSpan(offset_type begin_offset, offset_type end_offset) noexcept
        : begin_offset(begin_offset), end_offset(end_offset) {}

    /**
     * @brief 返回一个无效源码范围。
     *
     * @return 无效 `SourceSpan`。
     */
    [[nodiscard]] static constexpr SourceSpan invalid() noexcept {
        return SourceSpan();
    }

    /**
     * @brief 创建一个位于指定偏移处的空范围。
     *
     * @param offset 指定的字节偏移。
     * @return 起止位置相同的空范围。
     */
    [[nodiscard]] static constexpr SourceSpan empty_at(offset_type offset) noexcept {
        return SourceSpan(offset, offset);
    }

    /**
     * @brief 判断源码范围是否合法。
     *
     * @return 起止偏移均有效且 `begin_offset <= end_offset` 时返回 true。
     */
    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return begin_offset != kInvalidOffset &&
               end_offset != kInvalidOffset &&
               begin_offset <= end_offset;
    }

    /**
     * @brief 判断源码范围是否为空。
     *
     * @return 当前范围合法且起止偏移相等时返回 true。
     */
    [[nodiscard]] constexpr bool empty() const noexcept {
        return is_valid() && begin_offset == end_offset;
    }

    /**
     * @brief 获取源码范围的字节长度。
     *
     * @return 合法范围的长度；非法范围返回 0。
     */
    [[nodiscard]] constexpr offset_type size() const noexcept {
        return is_valid() ? (end_offset - begin_offset) : 0;
    }

    /**
     * @brief 判断指定偏移是否落在当前范围内。
     *
     * @param offset 待检查的字节偏移。
     * @return 偏移位于半开区间内时返回 true。
     */
    [[nodiscard]] constexpr bool contains(offset_type offset) const noexcept {
        return is_valid() && begin_offset <= offset && offset < end_offset;
    }

    /**
     * @brief 判断当前范围是否完全覆盖另一个范围。
     *
     * @param other 待检查的源码范围。
     * @return 当前范围完全包含另一个范围时返回 true。
     */
    [[nodiscard]] constexpr bool covers(SourceSpan other) const noexcept {
        return is_valid() &&
               other.is_valid() &&
               begin_offset <= other.begin_offset &&
               other.end_offset <= end_offset;
    }

    /**
     * @brief 判断当前范围是否与另一个范围重叠。
     *
     * @param other 待检查的源码范围。
     * @return 两个合法范围存在交集时返回 true。
     */
    [[nodiscard]] constexpr bool overlaps(SourceSpan other) const noexcept {
        return is_valid() &&
               other.is_valid() &&
               begin_offset < other.end_offset &&
               other.begin_offset < end_offset;
    }

    /**
     * @brief 合并当前范围与另一个范围。
     *
     * 若其中一个范围无效，则返回另一个范围。
     *
     * @param other 待合并的源码范围。
     * @return 能覆盖两者的最小范围。
     */
    [[nodiscard]] constexpr SourceSpan merge(SourceSpan other) const noexcept {
        if (!is_valid()) {
            return other;
        }
        if (!other.is_valid()) {
            return *this;
        }

        const offset_type merged_begin = std::min(begin_offset, other.begin_offset);
        const offset_type merged_end = std::max(end_offset, other.end_offset);
        return SourceSpan(merged_begin, merged_end);
    }

    /**
     * @brief 判断两个源码范围是否相等。
     *
     * @param lhs 左侧源码范围。
     * @param rhs 右侧源码范围。
     * @return 起止偏移都相等时返回 true。
     */
    friend constexpr bool operator==(SourceSpan lhs, SourceSpan rhs) noexcept {
        return lhs.begin_offset == rhs.begin_offset &&
               lhs.end_offset == rhs.end_offset;
    }

    /**
     * @brief 判断两个源码范围是否不相等。
     *
     * @param lhs 左侧源码范围。
     * @param rhs 右侧源码范围。
     * @return 任一偏移不同则返回 true。
     */
    friend constexpr bool operator!=(SourceSpan lhs, SourceSpan rhs) noexcept {
        return !(lhs == rhs);
    }

    offset_type begin_offset = kInvalidOffset;
    offset_type end_offset = kInvalidOffset;
};

/**
 * @brief 将源码范围输出到流中。
 *
 * 合法范围以 `[begin, end)` 形式输出，无效范围输出占位文本。
 *
 * @param os 输出流。
 * @param span 待输出的源码范围。
 * @return 输出流对象。
 */
inline std::ostream& operator<<(std::ostream& os, SourceSpan span) {
    if (!span.is_valid()) {
        return os << "<invalid-span>";
    }
    return os << '[' << span.begin_offset << ", " << span.end_offset << ')';
}

} // namespace baltam

namespace std {

/**
 * @brief `EntityId` 的哈希支持。
 *
 * @tparam Tag ID 的标签类型。
 */
template <typename Tag>
struct hash<baltam::EntityId<Tag>> {
    /**
     * @brief 计算实体 ID 的哈希值。
     *
     * @param id 待计算哈希的实体 ID。
     * @return 哈希结果。
     */
    std::size_t operator()(baltam::EntityId<Tag> id) const noexcept {
        return std::hash<typename baltam::EntityId<Tag>::underlying_type>{}(
            id.value());
    }
};

/**
 * @brief `SourceSpan` 的哈希支持。
 */
template <>
struct hash<baltam::SourceSpan> {
    /**
     * @brief 计算源码范围的哈希值。
     *
     * @param span 待计算哈希的源码范围。
     * @return 哈希结果。
     */
    std::size_t operator()(baltam::SourceSpan span) const noexcept {
        const std::size_t begin_hash =
            std::hash<baltam::SourceSpan::offset_type>{}(span.begin_offset);
        const std::size_t end_hash =
            std::hash<baltam::SourceSpan::offset_type>{}(span.end_offset);
        return begin_hash ^ (end_hash << 1U);
    }
};

} // namespace std
