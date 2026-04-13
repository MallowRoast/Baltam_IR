#ifndef BALTAM_IR_INTEGER_CONSTANT_H
#define BALTAM_IR_INTEGER_CONSTANT_H

#include <cstdint>
#include <limits>
#include <optional>

namespace baltam {

/**
 * @brief 固定宽度整数常量。
 *
 * 当前对外主要暴露三类接口：
 *
 * - 按具体整数类型构造
 * - 用同类型 `as_xxx()` 精确读取
 * - 少量和优化共享的辅助接口，如 `type()`、`as_double()`、`is_zero()`
 *   和 `negated()`
 *
 * 内部使用类型标签与原始 bit pattern 保存值；构造和读取都直接通过位运算
 * 编解码，不额外暴露其他辅助接口。
 */
struct IntegerConstant {
public:
    enum class Type : std::uint8_t {
        Int8,
        Int16,
        Int32,
        Int64,
        UInt8,
        UInt16,
        UInt32,
        UInt64,
    };

    explicit constexpr IntegerConstant(std::int8_t value)
        : type_(Type::Int8), bits_(static_cast<std::uint64_t>(static_cast<std::uint8_t>(value))) {}

    explicit constexpr IntegerConstant(std::int16_t value)
        : type_(Type::Int16),
          bits_(static_cast<std::uint64_t>(static_cast<std::uint16_t>(value))) {}

    explicit constexpr IntegerConstant(std::int32_t value)
        : type_(Type::Int32),
          bits_(static_cast<std::uint64_t>(static_cast<std::uint32_t>(value))) {}

    explicit constexpr IntegerConstant(std::int64_t value)
        : type_(Type::Int64), bits_(static_cast<std::uint64_t>(value)) {}

    explicit constexpr IntegerConstant(std::uint8_t value)
        : type_(Type::UInt8), bits_(static_cast<std::uint64_t>(value)) {}

    explicit constexpr IntegerConstant(std::uint16_t value)
        : type_(Type::UInt16), bits_(static_cast<std::uint64_t>(value)) {}

    explicit constexpr IntegerConstant(std::uint32_t value)
        : type_(Type::UInt32), bits_(static_cast<std::uint64_t>(value)) {}

    explicit constexpr IntegerConstant(std::uint64_t value)
        : type_(Type::UInt64), bits_(value) {}

    std::optional<std::int8_t> as_int8() const {
        if (type_ != Type::Int8) {
            return std::nullopt;
        }
        const std::uint64_t raw = bits_ & 0xFFu;
        const std::uint64_t extended = (raw & 0x80u) == 0 ? raw : (raw | ~static_cast<std::uint64_t>(0xFFu));
        return static_cast<std::int8_t>(static_cast<std::int64_t>(extended));
    }

    std::optional<std::int16_t> as_int16() const {
        if (type_ != Type::Int16) {
            return std::nullopt;
        }
        const std::uint64_t raw = bits_ & 0xFFFFu;
        const std::uint64_t extended =
            (raw & 0x8000u) == 0 ? raw : (raw | ~static_cast<std::uint64_t>(0xFFFFu));
        return static_cast<std::int16_t>(static_cast<std::int64_t>(extended));
    }

    std::optional<std::int32_t> as_int32() const {
        if (type_ != Type::Int32) {
            return std::nullopt;
        }
        const std::uint64_t raw = bits_ & 0xFFFFFFFFu;
        const std::uint64_t extended =
            (raw & 0x80000000u) == 0 ? raw : (raw | ~static_cast<std::uint64_t>(0xFFFFFFFFu));
        return static_cast<std::int32_t>(static_cast<std::int64_t>(extended));
    }

    std::optional<std::int64_t> as_int64() const {
        if (type_ != Type::Int64) {
            return std::nullopt;
        }
        const std::uint64_t raw = bits_ & 0xFFFFFFFFFFFFFFFFull;
        const std::uint64_t extended =
            (raw & 0x8000000000000000ull) == 0
                ? raw
                : (raw | ~static_cast<std::uint64_t>(0xFFFFFFFFFFFFFFFFull));
        return static_cast<std::int64_t>(extended);
    }

    std::optional<std::uint8_t> as_uint8() const {
        if (type_ != Type::UInt8) {
            return std::nullopt;
        }
        return static_cast<std::uint8_t>(bits_ & 0xFFu);
    }

    std::optional<std::uint16_t> as_uint16() const {
        if (type_ != Type::UInt16) {
            return std::nullopt;
        }
        return static_cast<std::uint16_t>(bits_ & 0xFFFFu);
    }

    std::optional<std::uint32_t> as_uint32() const {
        if (type_ != Type::UInt32) {
            return std::nullopt;
        }
        return static_cast<std::uint32_t>(bits_ & 0xFFFFFFFFu);
    }

    std::optional<std::uint64_t> as_uint64() const {
        if (type_ != Type::UInt64) {
            return std::nullopt;
        }
        return static_cast<std::uint64_t>(bits_ & 0xFFFFFFFFFFFFFFFFull);
    }

    Type type() const {
        return type_;
    }

    double as_double() const {
        switch (type_) {
            case Type::Int8:
                return static_cast<double>(as_int8().value());
            case Type::Int16:
                return static_cast<double>(as_int16().value());
            case Type::Int32:
                return static_cast<double>(as_int32().value());
            case Type::Int64:
                return static_cast<double>(as_int64().value());
            case Type::UInt8:
                return static_cast<double>(as_uint8().value());
            case Type::UInt16:
                return static_cast<double>(as_uint16().value());
            case Type::UInt32:
                return static_cast<double>(as_uint32().value());
            case Type::UInt64:
                return static_cast<double>(as_uint64().value());
        }
        return 0.0;
    }

    bool is_zero() const {
        return bits_ == 0;
    }

    IntegerConstant negated() const {
        switch (type_) {
            case Type::Int8:
                if (bits_ == 0x80u) {
                    return IntegerConstant(Type::Int8, 0x7Fu);
                }
                return IntegerConstant(Type::Int8, (~bits_ + 1) & 0xFFu);
            case Type::Int16:
                if (bits_ == 0x8000u) {
                    return IntegerConstant(Type::Int16, 0x7FFFu);
                }
                return IntegerConstant(Type::Int16, (~bits_ + 1) & 0xFFFFu);
            case Type::Int32:
                if (bits_ == 0x80000000u) {
                    return IntegerConstant(Type::Int32, 0x7FFFFFFFu);
                }
                return IntegerConstant(Type::Int32, (~bits_ + 1) & 0xFFFFFFFFu);
            case Type::Int64:
                if (bits_ == 0x8000000000000000ull) {
                    return IntegerConstant(Type::Int64, 0x7FFFFFFFFFFFFFFFull);
                }
                return IntegerConstant(Type::Int64, ~bits_ + 1);
            case Type::UInt8:
                return IntegerConstant(Type::UInt8, 0);
            case Type::UInt16:
                return IntegerConstant(Type::UInt16, 0);
            case Type::UInt32:
                return IntegerConstant(Type::UInt32, 0);
            case Type::UInt64:
                return IntegerConstant(Type::UInt64, 0);
        }
        return IntegerConstant(std::uint64_t{0});
    }

private:
    constexpr IntegerConstant(Type type, std::uint64_t bits) : type_(type), bits_(bits) {}

    Type type_ = Type::Int64;
    std::uint64_t bits_ = 0;
};

}  // namespace baltam

#endif
