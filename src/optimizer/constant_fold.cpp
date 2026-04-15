#include "optimizer/constant_fold.h"

#include "analysis/value_def.h"

#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace baltam {
namespace optimizer {
namespace {

using NumberValue = SSANumberNode::NumberValue;

struct EvalEntry {
    bool visiting = false;
    bool computed = false;
    std::optional<NumberValue> value;
};

using FoldableDirectBuiltinCallFn = std::optional<NumberValue> (*)(const std::vector<NumberValue>&);

std::optional<NumberValue> try_eval_value(
    ValueId value_id, const analysis::ValueDefAnalysis::Result& defs,
    std::unordered_map<ValueId, EvalEntry>& cache);

std::optional<NumberValue> fold_sin_call(const std::vector<NumberValue>& inputs) {
    if (inputs.size() != 1) {
        return std::nullopt;
    }

    return std::visit(
        [](const auto& item) -> std::optional<NumberValue> {
            using T = std::decay_t<decltype(item)>;

            if constexpr (std::is_same_v<T, double> || std::is_same_v<T, std::complex<double>>) {
                return NumberValue{std::sin(item)};
            }

            return std::nullopt;
        },
        inputs.front());
}

std::optional<NumberValue> fold_sqrt_call(const std::vector<NumberValue>& inputs) {
    if (inputs.size() != 1) {
        return std::nullopt;
    }

    return std::visit(
        [](const auto& item) -> std::optional<NumberValue> {
            using T = std::decay_t<decltype(item)>;

            if constexpr (std::is_same_v<T, double>) {
                if (item < 0.0) {
                    return NumberValue{std::sqrt(std::complex<double>{item, 0.0})};
                }
                return NumberValue{std::sqrt(item)};
            } else if constexpr (std::is_same_v<T, std::complex<double>>) {
                return NumberValue{std::sqrt(item)};
            }

            return std::nullopt;
        },
        inputs.front());
}

std::optional<NumberValue> fold_abs_call(const std::vector<NumberValue>& inputs) {
    if (inputs.size() != 1) {
        return std::nullopt;
    }

    return std::visit(
        [](const auto& item) -> std::optional<NumberValue> {
            using T = std::decay_t<decltype(item)>;

            if constexpr (std::is_same_v<T, bool>) {
                return NumberValue{item ? 1.0 : 0.0};
            } else if constexpr (std::is_same_v<T, IntegerConstant>) {
                switch (item.type()) {
                    case IntegerConstant::Type::Int8:
                        return NumberValue{
                            item.as_int8().value() < 0 ? item.negated() : item};
                    case IntegerConstant::Type::Int16:
                        return NumberValue{
                            item.as_int16().value() < 0 ? item.negated() : item};
                    case IntegerConstant::Type::Int32:
                        return NumberValue{
                            item.as_int32().value() < 0 ? item.negated() : item};
                    case IntegerConstant::Type::Int64:
                        return NumberValue{
                            item.as_int64().value() < 0 ? item.negated() : item};
                    case IntegerConstant::Type::UInt8:
                    case IntegerConstant::Type::UInt16:
                    case IntegerConstant::Type::UInt32:
                    case IntegerConstant::Type::UInt64:
                        return NumberValue{item};
                }
                return std::nullopt;
            } else if constexpr (std::is_same_v<T, double> ||
                                 std::is_same_v<T, std::complex<double>>) {
                return NumberValue{std::abs(item)};
            }

            return std::nullopt;
        },
        inputs.front());
}

const std::unordered_map<std::string, FoldableDirectBuiltinCallFn>&
foldable_direct_builtin_calls() {
    static const std::unordered_map<std::string, FoldableDirectBuiltinCallFn> folders = {
        {"sin", &fold_sin_call},
        {"sqrt", &fold_sqrt_call},
        {"abs", &fold_abs_call},
    };
    return folders;
}

std::optional<NumberValue> try_fold_call(
    const SSACallNode& call, const analysis::ValueDefAnalysis::Result& defs,
    std::unordered_map<ValueId, EvalEntry>& cache) {
    if (call.callee().type != SSACallNode::Callee::Direct || call.results().size() != 1) {
        return std::nullopt;
    }

    const auto folder_it = foldable_direct_builtin_calls().find(call.callee().direct_symbol);
    if (folder_it == foldable_direct_builtin_calls().end()) {
        return std::nullopt;
    }

    std::vector<NumberValue> input_values;
    input_values.reserve(call.inputs().size());
    for (const ValueRef input : call.inputs()) {
        if (!input.valid()) {
            return std::nullopt;
        }

        const std::optional<NumberValue> input_value = try_eval_value(input.id, defs, cache);
        if (!input_value.has_value()) {
            return std::nullopt;
        }
        input_values.push_back(*input_value);
    }

    return folder_it->second(input_values);
}

template <typename T>
T saturating_add_signed(T lhs, T rhs) {
    if (rhs > 0 && lhs > static_cast<T>(std::numeric_limits<T>::max() - rhs)) {
        return std::numeric_limits<T>::max();
    }
    if (rhs < 0 && lhs < static_cast<T>(std::numeric_limits<T>::min() - rhs)) {
        return std::numeric_limits<T>::min();
    }
    return static_cast<T>(lhs + rhs);
}

template <typename T>
T saturating_add_unsigned(T lhs, T rhs) {
    if (lhs > static_cast<T>(std::numeric_limits<T>::max() - rhs)) {
        return std::numeric_limits<T>::max();
    }
    return static_cast<T>(lhs + rhs);
}

template <typename T>
T saturating_sub_signed(T lhs, T rhs) {
    if (rhs > 0 && lhs < static_cast<T>(std::numeric_limits<T>::min() + rhs)) {
        return std::numeric_limits<T>::min();
    }
    if (rhs < 0 && lhs > static_cast<T>(std::numeric_limits<T>::max() + rhs)) {
        return std::numeric_limits<T>::max();
    }
    return static_cast<T>(lhs - rhs);
}

template <typename T>
T saturating_sub_unsigned(T lhs, T rhs) {
    if (lhs < rhs) {
        return 0;
    }
    return static_cast<T>(lhs - rhs);
}

template <typename T>
T saturating_mul_signed(T lhs, T rhs) {
    if (lhs == 0 || rhs == 0) {
        return 0;
    }

    if (lhs > 0) {
        if (rhs > 0) {
            if (lhs > static_cast<T>(std::numeric_limits<T>::max() / rhs)) {
                return std::numeric_limits<T>::max();
            }
        } else {
            if (rhs < static_cast<T>(std::numeric_limits<T>::min() / lhs)) {
                return std::numeric_limits<T>::min();
            }
        }
    } else {
        if (rhs > 0) {
            if (lhs < static_cast<T>(std::numeric_limits<T>::min() / rhs)) {
                return std::numeric_limits<T>::min();
            }
        } else {
            if (lhs < static_cast<T>(std::numeric_limits<T>::max() / rhs)) {
                return std::numeric_limits<T>::max();
            }
        }
    }

    return static_cast<T>(lhs * rhs);
}

template <typename T>
T saturating_mul_unsigned(T lhs, T rhs) {
    if (lhs == 0 || rhs == 0) {
        return 0;
    }
    if (lhs > static_cast<T>(std::numeric_limits<T>::max() / rhs)) {
        return std::numeric_limits<T>::max();
    }
    return static_cast<T>(lhs * rhs);
}

std::optional<IntegerConstant> add_same_type_integers(const IntegerConstant& lhs,
                                                      const IntegerConstant& rhs) {
    if (lhs.type() != rhs.type()) {
        return std::nullopt;
    }

    switch (lhs.type()) {
        case IntegerConstant::Type::Int8:
            return IntegerConstant(
                saturating_add_signed(lhs.as_int8().value(), rhs.as_int8().value()));
        case IntegerConstant::Type::Int16:
            return IntegerConstant(
                saturating_add_signed(lhs.as_int16().value(), rhs.as_int16().value()));
        case IntegerConstant::Type::Int32:
            return IntegerConstant(
                saturating_add_signed(lhs.as_int32().value(), rhs.as_int32().value()));
        case IntegerConstant::Type::Int64:
            return IntegerConstant(
                saturating_add_signed(lhs.as_int64().value(), rhs.as_int64().value()));
        case IntegerConstant::Type::UInt8:
            return IntegerConstant(
                saturating_add_unsigned(lhs.as_uint8().value(), rhs.as_uint8().value()));
        case IntegerConstant::Type::UInt16:
            return IntegerConstant(
                saturating_add_unsigned(lhs.as_uint16().value(), rhs.as_uint16().value()));
        case IntegerConstant::Type::UInt32:
            return IntegerConstant(
                saturating_add_unsigned(lhs.as_uint32().value(), rhs.as_uint32().value()));
        case IntegerConstant::Type::UInt64:
            return IntegerConstant(
                saturating_add_unsigned(lhs.as_uint64().value(), rhs.as_uint64().value()));
    }
    return std::nullopt;
}

std::optional<IntegerConstant> subtract_same_type_integers(const IntegerConstant& lhs,
                                                           const IntegerConstant& rhs) {
    if (lhs.type() != rhs.type()) {
        return std::nullopt;
    }

    switch (lhs.type()) {
        case IntegerConstant::Type::Int8:
            return IntegerConstant(
                saturating_sub_signed(lhs.as_int8().value(), rhs.as_int8().value()));
        case IntegerConstant::Type::Int16:
            return IntegerConstant(
                saturating_sub_signed(lhs.as_int16().value(), rhs.as_int16().value()));
        case IntegerConstant::Type::Int32:
            return IntegerConstant(
                saturating_sub_signed(lhs.as_int32().value(), rhs.as_int32().value()));
        case IntegerConstant::Type::Int64:
            return IntegerConstant(
                saturating_sub_signed(lhs.as_int64().value(), rhs.as_int64().value()));
        case IntegerConstant::Type::UInt8:
            return IntegerConstant(
                saturating_sub_unsigned(lhs.as_uint8().value(), rhs.as_uint8().value()));
        case IntegerConstant::Type::UInt16:
            return IntegerConstant(
                saturating_sub_unsigned(lhs.as_uint16().value(), rhs.as_uint16().value()));
        case IntegerConstant::Type::UInt32:
            return IntegerConstant(
                saturating_sub_unsigned(lhs.as_uint32().value(), rhs.as_uint32().value()));
        case IntegerConstant::Type::UInt64:
            return IntegerConstant(
                saturating_sub_unsigned(lhs.as_uint64().value(), rhs.as_uint64().value()));
    }
    return std::nullopt;
}

std::optional<IntegerConstant> multiply_same_type_integers(const IntegerConstant& lhs,
                                                           const IntegerConstant& rhs) {
    if (lhs.type() != rhs.type()) {
        return std::nullopt;
    }

    switch (lhs.type()) {
        case IntegerConstant::Type::Int8:
            return IntegerConstant(
                saturating_mul_signed(lhs.as_int8().value(), rhs.as_int8().value()));
        case IntegerConstant::Type::Int16:
            return IntegerConstant(
                saturating_mul_signed(lhs.as_int16().value(), rhs.as_int16().value()));
        case IntegerConstant::Type::Int32:
            return IntegerConstant(
                saturating_mul_signed(lhs.as_int32().value(), rhs.as_int32().value()));
        case IntegerConstant::Type::Int64:
            return IntegerConstant(
                saturating_mul_signed(lhs.as_int64().value(), rhs.as_int64().value()));
        case IntegerConstant::Type::UInt8:
            return IntegerConstant(
                saturating_mul_unsigned(lhs.as_uint8().value(), rhs.as_uint8().value()));
        case IntegerConstant::Type::UInt16:
            return IntegerConstant(
                saturating_mul_unsigned(lhs.as_uint16().value(), rhs.as_uint16().value()));
        case IntegerConstant::Type::UInt32:
            return IntegerConstant(
                saturating_mul_unsigned(lhs.as_uint32().value(), rhs.as_uint32().value()));
        case IntegerConstant::Type::UInt64:
            return IntegerConstant(
                saturating_mul_unsigned(lhs.as_uint64().value(), rhs.as_uint64().value()));
    }
    return std::nullopt;
}

std::optional<IntegerConstant> divide_same_type_integers(const IntegerConstant& lhs,
                                                         const IntegerConstant& rhs) {
    if (lhs.type() != rhs.type()) {
        return std::nullopt;
    }

    switch (lhs.type()) {
        case IntegerConstant::Type::Int8: {
            const std::int8_t divisor = rhs.as_int8().value();
            if (divisor == 0) {
                // 整数除零会在运行时报错，这里故意不折叠。
                return std::nullopt;
            }
            const std::int8_t dividend = lhs.as_int8().value();
            if (dividend == std::numeric_limits<std::int8_t>::min() && divisor == -1) {
                return IntegerConstant(std::numeric_limits<std::int8_t>::max());
            }
            return IntegerConstant(static_cast<std::int8_t>(dividend / divisor));
        }
        case IntegerConstant::Type::Int16: {
            const std::int16_t divisor = rhs.as_int16().value();
            if (divisor == 0) {
                // 整数除零会在运行时报错，这里故意不折叠。
                return std::nullopt;
            }
            const std::int16_t dividend = lhs.as_int16().value();
            if (dividend == std::numeric_limits<std::int16_t>::min() && divisor == -1) {
                return IntegerConstant(std::numeric_limits<std::int16_t>::max());
            }
            return IntegerConstant(static_cast<std::int16_t>(dividend / divisor));
        }
        case IntegerConstant::Type::Int32: {
            const std::int32_t divisor = rhs.as_int32().value();
            if (divisor == 0) {
                // 整数除零会在运行时报错，这里故意不折叠。
                return std::nullopt;
            }
            const std::int32_t dividend = lhs.as_int32().value();
            if (dividend == std::numeric_limits<std::int32_t>::min() && divisor == -1) {
                return IntegerConstant(std::numeric_limits<std::int32_t>::max());
            }
            return IntegerConstant(static_cast<std::int32_t>(dividend / divisor));
        }
        case IntegerConstant::Type::Int64: {
            const std::int64_t divisor = rhs.as_int64().value();
            if (divisor == 0) {
                // 整数除零会在运行时报错，这里故意不折叠。
                return std::nullopt;
            }
            const std::int64_t dividend = lhs.as_int64().value();
            if (dividend == std::numeric_limits<std::int64_t>::min() && divisor == -1) {
                return IntegerConstant(std::numeric_limits<std::int64_t>::max());
            }
            return IntegerConstant(static_cast<std::int64_t>(dividend / divisor));
        }
        case IntegerConstant::Type::UInt8: {
            const std::uint8_t divisor = rhs.as_uint8().value();
            if (divisor == 0) {
                // 整数除零会在运行时报错，这里故意不折叠。
                return std::nullopt;
            }
            return IntegerConstant(static_cast<std::uint8_t>(lhs.as_uint8().value() / divisor));
        }
        case IntegerConstant::Type::UInt16: {
            const std::uint16_t divisor = rhs.as_uint16().value();
            if (divisor == 0) {
                // 整数除零会在运行时报错，这里故意不折叠。
                return std::nullopt;
            }
            return IntegerConstant(static_cast<std::uint16_t>(lhs.as_uint16().value() / divisor));
        }
        case IntegerConstant::Type::UInt32: {
            const std::uint32_t divisor = rhs.as_uint32().value();
            if (divisor == 0) {
                // 整数除零会在运行时报错，这里故意不折叠。
                return std::nullopt;
            }
            return IntegerConstant(static_cast<std::uint32_t>(lhs.as_uint32().value() / divisor));
        }
        case IntegerConstant::Type::UInt64: {
            const std::uint64_t divisor = rhs.as_uint64().value();
            if (divisor == 0) {
                // 整数除零会在运行时报错，这里故意不折叠。
                return std::nullopt;
            }
            return IntegerConstant(static_cast<std::uint64_t>(lhs.as_uint64().value() / divisor));
        }
    }
    return std::nullopt;
}

template <typename T>
T saturating_signed_from_double(double value) {
    if (std::isinf(value)) {
        return value > 0 ? std::numeric_limits<T>::max() : std::numeric_limits<T>::min();
    }

    const long double truncated = std::trunc(static_cast<long double>(value));
    if (truncated > static_cast<long double>(std::numeric_limits<T>::max())) {
        return std::numeric_limits<T>::max();
    }
    if (truncated < static_cast<long double>(std::numeric_limits<T>::min())) {
        return std::numeric_limits<T>::min();
    }
    return static_cast<T>(truncated);
}

template <typename T>
T saturating_unsigned_from_double(double value) {
    if (std::isinf(value)) {
        return value > 0 ? std::numeric_limits<T>::max() : 0;
    }

    const long double truncated = std::trunc(static_cast<long double>(value));
    if (truncated > static_cast<long double>(std::numeric_limits<T>::max())) {
        return std::numeric_limits<T>::max();
    }
    if (truncated < 0.0L) {
        return 0;
    }
    return static_cast<T>(truncated);
}

std::optional<IntegerConstant> convert_double_to_integer_type(double value,
                                                              IntegerConstant::Type type) {
    if (std::isnan(value)) {
        return std::nullopt;
    }

    switch (type) {
        case IntegerConstant::Type::Int8:
            return IntegerConstant(saturating_signed_from_double<std::int8_t>(value));
        case IntegerConstant::Type::Int16:
            return IntegerConstant(saturating_signed_from_double<std::int16_t>(value));
        case IntegerConstant::Type::Int32:
            return IntegerConstant(saturating_signed_from_double<std::int32_t>(value));
        case IntegerConstant::Type::Int64:
            return IntegerConstant(saturating_signed_from_double<std::int64_t>(value));
        case IntegerConstant::Type::UInt8:
            return IntegerConstant(saturating_unsigned_from_double<std::uint8_t>(value));
        case IntegerConstant::Type::UInt16:
            return IntegerConstant(saturating_unsigned_from_double<std::uint16_t>(value));
        case IntegerConstant::Type::UInt32:
            return IntegerConstant(saturating_unsigned_from_double<std::uint32_t>(value));
        case IntegerConstant::Type::UInt64:
            return IntegerConstant(saturating_unsigned_from_double<std::uint64_t>(value));
    }
    return std::nullopt;
}

double multiply_complex_component(double scalar, double component) {
    if (component == 0.0) {
        return 0.0;
    }
    return scalar * component;
}

double multiply_complex_term(double lhs, double rhs) {
    if (lhs == 0.0 || rhs == 0.0) {
        return 0.0;
    }
    return lhs * rhs;
}

std::complex<double> multiply_double_complex(double lhs, const std::complex<double>& rhs) {
    // 不直接用 `std::complex` 乘法，避免复数某一分量本来就是 0 时，
    // 被 `NaN`/`Inf` 参与计算后错误污染成 `NaN`。
    return std::complex<double>(multiply_complex_component(lhs, rhs.real()),
                                multiply_complex_component(lhs, rhs.imag()));
}

std::complex<double> multiply_complex_complex(const std::complex<double>& lhs,
                                              const std::complex<double>& rhs) {
    // 按复数乘法定义 `(ac - bd) + (ad + bc)i` 逐项计算，并对零分量做保护。
    const double ac = multiply_complex_term(lhs.real(), rhs.real());
    const double bd = multiply_complex_term(lhs.imag(), rhs.imag());
    const double ad = multiply_complex_term(lhs.real(), rhs.imag());
    const double bc = multiply_complex_term(lhs.imag(), rhs.real());
    return std::complex<double>(ac - bd, ad + bc);
}

std::complex<double> divide_double_complex(double lhs, const std::complex<double>& rhs) {
    if (rhs.imag() == 0.0) {
        return std::complex<double>(lhs / rhs.real(), 0.0);
    }
    if (rhs.real() == 0.0) {
        return std::complex<double>(0.0, -(lhs / rhs.imag()));
    }

    const double denominator = rhs.real() * rhs.real() + rhs.imag() * rhs.imag();
    return std::complex<double>((lhs * rhs.real()) / denominator,
                                -(lhs * rhs.imag()) / denominator);
}

std::complex<double> divide_complex_double(const std::complex<double>& lhs, double rhs) {
    return std::complex<double>(lhs.real() / rhs, lhs.imag() / rhs);
}

std::complex<double> divide_complex_complex(const std::complex<double>& lhs,
                                            const std::complex<double>& rhs) {
    if (rhs.imag() == 0.0) {
        return std::complex<double>(lhs.real() / rhs.real(), lhs.imag() / rhs.real());
    }
    if (rhs.real() == 0.0) {
        return std::complex<double>(lhs.imag() / rhs.imag(), -(lhs.real() / rhs.imag()));
    }

    const double denominator = rhs.real() * rhs.real() + rhs.imag() * rhs.imag();
    return std::complex<double>((lhs.real() * rhs.real() + lhs.imag() * rhs.imag()) /
                                    denominator,
                                (lhs.imag() * rhs.real() - lhs.real() * rhs.imag()) /
                                    denominator);
}

bool double_is_integer_valued(double value) {
    return std::isfinite(value) && std::trunc(value) == value;
}

NumberValue fold_real_power(double base, double exponent) {
    if ((base == 1.0 && std::isnan(exponent)) || (std::isnan(base) && exponent == 0.0)) {
        // MATLAB `power` / `mpower` 对这两类实数输入返回 `NaN`，与 IEEE `pow` 不同。
        return NumberValue{std::numeric_limits<double>::quiet_NaN()};
    }

    if (base < 0.0 && std::isfinite(exponent) && !double_is_integer_valued(exponent)) {
        return NumberValue{std::pow(std::complex<double>(base, 0.0), exponent)};
    }

    return NumberValue{std::pow(base, exponent)};
}

std::optional<IntegerConstant> fold_integer_power_result(double base, double exponent,
                                                         IntegerConstant::Type result_type) {
    const NumberValue powered = fold_real_power(base, exponent);
    const auto* real_result = std::get_if<double>(&powered);
    if (real_result == nullptr) {
        // 当前整数路径只在结果仍为实数时折叠，避免把复杂的整数/复数语义提前固化。
        return std::nullopt;
    }
    return convert_double_to_integer_type(*real_result, result_type);
}

std::optional<NumberValue> fold_unary(UnaryOpType op, const NumberValue& operand) {
    return std::visit(
        [op](const auto& item) -> std::optional<NumberValue> {
            using T = std::decay_t<decltype(item)>;

            switch (op) {
                case UnaryOpType::Logic_Not:
                    if constexpr (std::is_same_v<T, bool>) {
                        return NumberValue{!item};
                    } else if constexpr (std::is_same_v<T, IntegerConstant>) {
                        return NumberValue{item.is_zero()};
                    } else if constexpr (std::is_same_v<T, double>) {
                        return NumberValue{item == T{0}};
                    }
                    // 复数 `logic_not` 当前会在运行时报错。常量折叠这里不能把它提前
                    // 变成编译期报错，也不能错误地折成一个 `bool` 常量。
                    return std::nullopt;

                case UnaryOpType::UPlus:
                    if constexpr (std::is_same_v<T, bool>) {
                        return NumberValue{item ? 1.0 : 0.0};
                    } else {
                        return NumberValue{item};
                    }

                case UnaryOpType::UMinus:
                    if constexpr (std::is_same_v<T, bool>) {
                        return NumberValue{item ? -1.0 : 0.0};
                    } else if constexpr (std::is_same_v<T, IntegerConstant>) {
                        return NumberValue{item.negated()};
                    } else if constexpr (std::is_same_v<T, double> ||
                                         std::is_same_v<T, std::complex<double>>) {
                        return NumberValue{-item};
                    }
                    return std::nullopt;

                case UnaryOpType::Transpose:
                    return NumberValue{item};

                case UnaryOpType::CTranspose:
                    if constexpr (std::is_same_v<T, std::complex<double>>) {
                        return NumberValue{std::conj(item)};
                    } else {
                        return NumberValue{item};
                    }
            }

            return std::nullopt;
        },
        operand);
}

std::optional<NumberValue> fold_add(const NumberValue& lhs, const NumberValue& rhs) {
    return std::visit(
        [](const auto& lhs_item, const auto& rhs_item) -> std::optional<NumberValue> {
            using L = std::decay_t<decltype(lhs_item)>;
            using R = std::decay_t<decltype(rhs_item)>;

            if constexpr (std::is_same_v<L, bool> && std::is_same_v<R, bool>) {
                return NumberValue{static_cast<double>(lhs_item) + static_cast<double>(rhs_item)};
            } else if constexpr ((std::is_same_v<L, bool> &&
                                  std::is_same_v<R, IntegerConstant>) ||
                                 (std::is_same_v<L, IntegerConstant> &&
                                  std::is_same_v<R, bool>)) {
                // `bool` 不和 `IntegerConstant` 做隐式整数加法，这里故意不折叠。
                return std::nullopt;
            } else if constexpr (std::is_same_v<L, bool> && std::is_same_v<R, double>) {
                return NumberValue{static_cast<double>(lhs_item) + rhs_item};
            } else if constexpr (std::is_same_v<L, double> && std::is_same_v<R, bool>) {
                return NumberValue{lhs_item + static_cast<double>(rhs_item)};
            } else if constexpr (std::is_same_v<L, bool> &&
                                 std::is_same_v<R, std::complex<double>>) {
                return NumberValue{rhs_item + static_cast<double>(lhs_item)};
            } else if constexpr (std::is_same_v<L, std::complex<double>> &&
                                 std::is_same_v<R, bool>) {
                return NumberValue{lhs_item + static_cast<double>(rhs_item)};
            } else if constexpr (std::is_same_v<L, IntegerConstant> &&
                                 std::is_same_v<R, IntegerConstant>) {
                if (const std::optional<IntegerConstant> sum = add_same_type_integers(lhs_item, rhs_item)) {
                    return NumberValue{*sum};
                }
                return std::nullopt;
            } else if constexpr (std::is_same_v<L, IntegerConstant> && std::is_same_v<R, double>) {
                return NumberValue{lhs_item.as_double() + rhs_item};
            } else if constexpr (std::is_same_v<L, double> && std::is_same_v<R, IntegerConstant>) {
                return NumberValue{lhs_item + rhs_item.as_double()};
            } else if constexpr ((std::is_same_v<L, IntegerConstant> &&
                                  std::is_same_v<R, std::complex<double>>) ||
                                 (std::is_same_v<L, std::complex<double>> &&
                                  std::is_same_v<R, IntegerConstant>)) {
                // 复数和整数常量的组合当前不在常量折叠里处理，保持运行时语义。
                return std::nullopt;
            } else if constexpr (std::is_same_v<L, double> && std::is_same_v<R, double>) {
                return NumberValue{lhs_item + rhs_item};
            } else if constexpr (std::is_same_v<L, double> &&
                                 std::is_same_v<R, std::complex<double>>) {
                return NumberValue{lhs_item + rhs_item};
            } else if constexpr (std::is_same_v<L, std::complex<double>> &&
                                 std::is_same_v<R, double>) {
                return NumberValue{lhs_item + rhs_item};
            } else if constexpr (std::is_same_v<L, std::complex<double>> &&
                                 std::is_same_v<R, std::complex<double>>) {
                return NumberValue{lhs_item + rhs_item};
            }

            return std::nullopt;
        },
        lhs, rhs);
}

std::optional<NumberValue> fold_subtract(const NumberValue& lhs, const NumberValue& rhs) {
    return std::visit(
        [](const auto& lhs_item, const auto& rhs_item) -> std::optional<NumberValue> {
            using L = std::decay_t<decltype(lhs_item)>;
            using R = std::decay_t<decltype(rhs_item)>;

            if constexpr (std::is_same_v<L, bool> && std::is_same_v<R, bool>) {
                return NumberValue{static_cast<double>(lhs_item) - static_cast<double>(rhs_item)};
            } else if constexpr ((std::is_same_v<L, bool> &&
                                  std::is_same_v<R, IntegerConstant>) ||
                                 (std::is_same_v<L, IntegerConstant> &&
                                  std::is_same_v<R, bool>)) {
                // `bool` 不和 `IntegerConstant` 做隐式整数减法，这里故意不折叠。
                return std::nullopt;
            } else if constexpr (std::is_same_v<L, bool> && std::is_same_v<R, double>) {
                return NumberValue{static_cast<double>(lhs_item) - rhs_item};
            } else if constexpr (std::is_same_v<L, double> && std::is_same_v<R, bool>) {
                return NumberValue{lhs_item - static_cast<double>(rhs_item)};
            } else if constexpr (std::is_same_v<L, bool> &&
                                 std::is_same_v<R, std::complex<double>>) {
                return NumberValue{std::complex<double>(static_cast<double>(lhs_item), 0.0) -
                                   rhs_item};
            } else if constexpr (std::is_same_v<L, std::complex<double>> &&
                                 std::is_same_v<R, bool>) {
                return NumberValue{lhs_item - static_cast<double>(rhs_item)};
            } else if constexpr (std::is_same_v<L, IntegerConstant> &&
                                 std::is_same_v<R, IntegerConstant>) {
                if (const std::optional<IntegerConstant> difference =
                        subtract_same_type_integers(lhs_item, rhs_item)) {
                    return NumberValue{*difference};
                }
                return std::nullopt;
            } else if constexpr (std::is_same_v<L, IntegerConstant> && std::is_same_v<R, double>) {
                return NumberValue{lhs_item.as_double() - rhs_item};
            } else if constexpr (std::is_same_v<L, double> && std::is_same_v<R, IntegerConstant>) {
                return NumberValue{lhs_item - rhs_item.as_double()};
            } else if constexpr ((std::is_same_v<L, IntegerConstant> &&
                                  std::is_same_v<R, std::complex<double>>) ||
                                 (std::is_same_v<L, std::complex<double>> &&
                                  std::is_same_v<R, IntegerConstant>)) {
                // 复数和整数常量的组合当前不在常量折叠里处理，保持运行时语义。
                return std::nullopt;
            } else if constexpr (std::is_same_v<L, double> && std::is_same_v<R, double>) {
                return NumberValue{lhs_item - rhs_item};
            } else if constexpr (std::is_same_v<L, double> &&
                                 std::is_same_v<R, std::complex<double>>) {
                return NumberValue{std::complex<double>(lhs_item, 0.0) - rhs_item};
            } else if constexpr (std::is_same_v<L, std::complex<double>> &&
                                 std::is_same_v<R, double>) {
                return NumberValue{lhs_item - rhs_item};
            } else if constexpr (std::is_same_v<L, std::complex<double>> &&
                                 std::is_same_v<R, std::complex<double>>) {
                return NumberValue{lhs_item - rhs_item};
            }

            return std::nullopt;
        },
        lhs, rhs);
}

std::optional<NumberValue> fold_multiply(const NumberValue& lhs, const NumberValue& rhs) {
    return std::visit(
        [](const auto& lhs_item, const auto& rhs_item) -> std::optional<NumberValue> {
            using L = std::decay_t<decltype(lhs_item)>;
            using R = std::decay_t<decltype(rhs_item)>;

            if constexpr (std::is_same_v<L, bool> && std::is_same_v<R, bool>) {
                return NumberValue{static_cast<double>(lhs_item && rhs_item)};
            } else if constexpr ((std::is_same_v<L, bool> &&
                                  std::is_same_v<R, IntegerConstant>) ||
                                 (std::is_same_v<L, IntegerConstant> &&
                                  std::is_same_v<R, bool>)) {
                // `bool` 不和 `IntegerConstant` 做隐式整数乘法，这里故意不折叠。
                return std::nullopt;
            } else if constexpr (std::is_same_v<L, bool> && std::is_same_v<R, double>) {
                return NumberValue{static_cast<double>(lhs_item) * rhs_item};
            } else if constexpr (std::is_same_v<L, double> && std::is_same_v<R, bool>) {
                return NumberValue{lhs_item * static_cast<double>(rhs_item)};
            } else if constexpr (std::is_same_v<L, bool> &&
                                 std::is_same_v<R, std::complex<double>>) {
                return NumberValue{
                    multiply_double_complex(static_cast<double>(lhs_item), rhs_item)};
            } else if constexpr (std::is_same_v<L, std::complex<double>> &&
                                 std::is_same_v<R, bool>) {
                return NumberValue{
                    multiply_double_complex(static_cast<double>(rhs_item), lhs_item)};
            } else if constexpr (std::is_same_v<L, IntegerConstant> &&
                                 std::is_same_v<R, IntegerConstant>) {
                if (const std::optional<IntegerConstant> product =
                        multiply_same_type_integers(lhs_item, rhs_item)) {
                    return NumberValue{*product};
                }
                return std::nullopt;
            } else if constexpr (std::is_same_v<L, IntegerConstant> && std::is_same_v<R, double>) {
                if (const std::optional<IntegerConstant> product =
                        convert_double_to_integer_type(lhs_item.as_double() * rhs_item,
                                                       lhs_item.type())) {
                    return NumberValue{*product};
                }
                return std::nullopt;
            } else if constexpr (std::is_same_v<L, double> && std::is_same_v<R, IntegerConstant>) {
                if (const std::optional<IntegerConstant> product =
                        convert_double_to_integer_type(lhs_item * rhs_item.as_double(),
                                                       rhs_item.type())) {
                    return NumberValue{*product};
                }
                return std::nullopt;
            } else if constexpr ((std::is_same_v<L, IntegerConstant> &&
                                  std::is_same_v<R, std::complex<double>>) ||
                                 (std::is_same_v<L, std::complex<double>> &&
                                  std::is_same_v<R, IntegerConstant>)) {
                // 复数和整数常量的组合当前不在常量折叠里处理，保持运行时语义。
                return std::nullopt;
            } else if constexpr (std::is_same_v<L, double> && std::is_same_v<R, double>) {
                return NumberValue{lhs_item * rhs_item};
            } else if constexpr (std::is_same_v<L, double> &&
                                 std::is_same_v<R, std::complex<double>>) {
                return NumberValue{multiply_double_complex(lhs_item, rhs_item)};
            } else if constexpr (std::is_same_v<L, std::complex<double>> &&
                                 std::is_same_v<R, double>) {
                return NumberValue{multiply_double_complex(rhs_item, lhs_item)};
            } else if constexpr (std::is_same_v<L, std::complex<double>> &&
                                 std::is_same_v<R, std::complex<double>>) {
                return NumberValue{multiply_complex_complex(lhs_item, rhs_item)};
            }

            return std::nullopt;
        },
        lhs, rhs);
}

std::optional<NumberValue> fold_divide(const NumberValue& numerator,
                                       const NumberValue& denominator) {
    return std::visit(
        [](const auto& lhs_item, const auto& rhs_item) -> std::optional<NumberValue> {
            using L = std::decay_t<decltype(lhs_item)>;
            using R = std::decay_t<decltype(rhs_item)>;

            if constexpr (std::is_same_v<L, bool> && std::is_same_v<R, bool>) {
                return NumberValue{static_cast<double>(lhs_item) / static_cast<double>(rhs_item)};
            } else if constexpr ((std::is_same_v<L, bool> &&
                                  std::is_same_v<R, IntegerConstant>) ||
                                 (std::is_same_v<L, IntegerConstant> &&
                                  std::is_same_v<R, bool>)) {
                // `bool` 不和 `IntegerConstant` 做隐式整数除法，这里故意不折叠。
                return std::nullopt;
            } else if constexpr (std::is_same_v<L, bool> && std::is_same_v<R, double>) {
                return NumberValue{static_cast<double>(lhs_item) / rhs_item};
            } else if constexpr (std::is_same_v<L, double> && std::is_same_v<R, bool>) {
                return NumberValue{lhs_item / static_cast<double>(rhs_item)};
            } else if constexpr (std::is_same_v<L, bool> &&
                                 std::is_same_v<R, std::complex<double>>) {
                return NumberValue{
                    divide_double_complex(static_cast<double>(lhs_item), rhs_item)};
            } else if constexpr (std::is_same_v<L, std::complex<double>> &&
                                 std::is_same_v<R, bool>) {
                return NumberValue{
                    divide_complex_double(lhs_item, static_cast<double>(rhs_item))};
            } else if constexpr (std::is_same_v<L, IntegerConstant> &&
                                 std::is_same_v<R, IntegerConstant>) {
                if (const std::optional<IntegerConstant> quotient =
                        divide_same_type_integers(lhs_item, rhs_item)) {
                    return NumberValue{*quotient};
                }
                return std::nullopt;
            } else if constexpr (std::is_same_v<L, IntegerConstant> && std::is_same_v<R, double>) {
                if (const std::optional<IntegerConstant> quotient =
                        convert_double_to_integer_type(lhs_item.as_double() / rhs_item,
                                                       lhs_item.type())) {
                    return NumberValue{*quotient};
                }
                return std::nullopt;
            } else if constexpr (std::is_same_v<L, double> && std::is_same_v<R, IntegerConstant>) {
                if (rhs_item.is_zero()) {
                    // 整数除零会在运行时报错，这里故意不折叠。
                    return std::nullopt;
                }
                if (const std::optional<IntegerConstant> quotient =
                        convert_double_to_integer_type(lhs_item / rhs_item.as_double(),
                                                       rhs_item.type())) {
                    return NumberValue{*quotient};
                }
                return std::nullopt;
            } else if constexpr ((std::is_same_v<L, IntegerConstant> &&
                                  std::is_same_v<R, std::complex<double>>) ||
                                 (std::is_same_v<L, std::complex<double>> &&
                                  std::is_same_v<R, IntegerConstant>)) {
                // 复数和整数常量的组合当前不在常量折叠里处理，保持运行时语义。
                return std::nullopt;
            } else if constexpr (std::is_same_v<L, double> && std::is_same_v<R, double>) {
                return NumberValue{lhs_item / rhs_item};
            } else if constexpr (std::is_same_v<L, double> &&
                                 std::is_same_v<R, std::complex<double>>) {
                return NumberValue{divide_double_complex(lhs_item, rhs_item)};
            } else if constexpr (std::is_same_v<L, std::complex<double>> &&
                                 std::is_same_v<R, double>) {
                return NumberValue{divide_complex_double(lhs_item, rhs_item)};
            } else if constexpr (std::is_same_v<L, std::complex<double>> &&
                                 std::is_same_v<R, std::complex<double>>) {
                return NumberValue{divide_complex_complex(lhs_item, rhs_item)};
            }

            return std::nullopt;
        },
        numerator, denominator);
}

std::optional<NumberValue> fold_power(const NumberValue& base, const NumberValue& exponent) {
    return std::visit(
        [](const auto& lhs_item, const auto& rhs_item) -> std::optional<NumberValue> {
            using L = std::decay_t<decltype(lhs_item)>;
            using R = std::decay_t<decltype(rhs_item)>;

            if constexpr (std::is_same_v<L, bool> && std::is_same_v<R, bool>) {
                return fold_real_power(static_cast<double>(lhs_item), static_cast<double>(rhs_item));
            } else if constexpr ((std::is_same_v<L, bool> &&
                                  std::is_same_v<R, IntegerConstant>) ||
                                 (std::is_same_v<L, IntegerConstant> &&
                                  std::is_same_v<R, bool>)) {
                // 与 `plus` / `times` 路径一致，这里先不为 `bool` 与整数常量混合幂运算定型。
                return std::nullopt;
            } else if constexpr (std::is_same_v<L, bool> && std::is_same_v<R, double>) {
                return fold_real_power(static_cast<double>(lhs_item), rhs_item);
            } else if constexpr (std::is_same_v<L, double> && std::is_same_v<R, bool>) {
                return fold_real_power(lhs_item, static_cast<double>(rhs_item));
            } else if constexpr (std::is_same_v<L, bool> &&
                                 std::is_same_v<R, std::complex<double>>) {
                return NumberValue{
                    std::pow(std::complex<double>(static_cast<double>(lhs_item), 0.0), rhs_item)};
            } else if constexpr (std::is_same_v<L, std::complex<double>> &&
                                 std::is_same_v<R, bool>) {
                return NumberValue{std::pow(lhs_item, static_cast<double>(rhs_item))};
            } else if constexpr (std::is_same_v<L, IntegerConstant> &&
                                 std::is_same_v<R, IntegerConstant>) {
                if (lhs_item.type() != rhs_item.type()) {
                    return std::nullopt;
                }
                if (const std::optional<IntegerConstant> powered = fold_integer_power_result(
                        lhs_item.as_double(), rhs_item.as_double(), lhs_item.type())) {
                    return NumberValue{*powered};
                }
                return std::nullopt;
            } else if constexpr (std::is_same_v<L, IntegerConstant> && std::is_same_v<R, double>) {
                if (const std::optional<IntegerConstant> powered = fold_integer_power_result(
                        lhs_item.as_double(), rhs_item, lhs_item.type())) {
                    return NumberValue{*powered};
                }
                return std::nullopt;
            } else if constexpr (std::is_same_v<L, double> && std::is_same_v<R, IntegerConstant>) {
                if (const std::optional<IntegerConstant> powered = fold_integer_power_result(
                        lhs_item, rhs_item.as_double(), rhs_item.type())) {
                    return NumberValue{*powered};
                }
                return std::nullopt;
            } else if constexpr ((std::is_same_v<L, IntegerConstant> &&
                                  std::is_same_v<R, std::complex<double>>) ||
                                 (std::is_same_v<L, std::complex<double>> &&
                                  std::is_same_v<R, IntegerConstant>)) {
                // MATLAB 文档要求整数操作数本身不能是复数；这里保守地留给运行时处理。
                return std::nullopt;
            } else if constexpr (std::is_same_v<L, double> && std::is_same_v<R, double>) {
                return fold_real_power(lhs_item, rhs_item);
            } else if constexpr (std::is_same_v<L, double> &&
                                 std::is_same_v<R, std::complex<double>>) {
                return NumberValue{std::pow(std::complex<double>(lhs_item, 0.0), rhs_item)};
            } else if constexpr (std::is_same_v<L, std::complex<double>> &&
                                 std::is_same_v<R, double>) {
                return NumberValue{std::pow(lhs_item, rhs_item)};
            } else if constexpr (std::is_same_v<L, std::complex<double>> &&
                                 std::is_same_v<R, std::complex<double>>) {
                return NumberValue{std::pow(lhs_item, rhs_item)};
            }

            return std::nullopt;
        },
        base, exponent);
}

bool integer_equal_integer(const IntegerConstant& lhs, const IntegerConstant& rhs) {
    auto is_signed = [](IntegerConstant::Type type) {
        switch (type) {
            case IntegerConstant::Type::Int8:
            case IntegerConstant::Type::Int16:
            case IntegerConstant::Type::Int32:
            case IntegerConstant::Type::Int64:
                return true;
            case IntegerConstant::Type::UInt8:
            case IntegerConstant::Type::UInt16:
            case IntegerConstant::Type::UInt32:
            case IntegerConstant::Type::UInt64:
                return false;
        }
        return false;
    };

    auto to_signed = [](const IntegerConstant& value) -> std::int64_t {
        switch (value.type()) {
            case IntegerConstant::Type::Int8:
                return value.as_int8().value();
            case IntegerConstant::Type::Int16:
                return value.as_int16().value();
            case IntegerConstant::Type::Int32:
                return value.as_int32().value();
            case IntegerConstant::Type::Int64:
                return value.as_int64().value();
            case IntegerConstant::Type::UInt8:
            case IntegerConstant::Type::UInt16:
            case IntegerConstant::Type::UInt32:
            case IntegerConstant::Type::UInt64:
                break;
        }
        return 0;
    };

    auto to_unsigned = [](const IntegerConstant& value) -> std::uint64_t {
        switch (value.type()) {
            case IntegerConstant::Type::UInt8:
                return value.as_uint8().value();
            case IntegerConstant::Type::UInt16:
                return value.as_uint16().value();
            case IntegerConstant::Type::UInt32:
                return value.as_uint32().value();
            case IntegerConstant::Type::UInt64:
                return value.as_uint64().value();
            case IntegerConstant::Type::Int8:
            case IntegerConstant::Type::Int16:
            case IntegerConstant::Type::Int32:
            case IntegerConstant::Type::Int64:
                break;
        }
        return 0;
    };

    const bool lhs_signed = is_signed(lhs.type());
    const bool rhs_signed = is_signed(rhs.type());
    if (lhs_signed && rhs_signed) {
        return to_signed(lhs) == to_signed(rhs);
    }
    if (!lhs_signed && !rhs_signed) {
        return to_unsigned(lhs) == to_unsigned(rhs);
    }

    if (lhs_signed) {
        const std::int64_t lhs_value = to_signed(lhs);
        return lhs_value >= 0 && static_cast<std::uint64_t>(lhs_value) == to_unsigned(rhs);
    }

    const std::int64_t rhs_value = to_signed(rhs);
    return rhs_value >= 0 && to_unsigned(lhs) == static_cast<std::uint64_t>(rhs_value);
}

template <typename Scalar>
bool scalar_equal_complex(Scalar scalar, const std::complex<double>& complex_value) {
    // `==` / `!=` 允许标量与复数比较；只有虚部为 0 时才可能相等。
    return complex_value.imag() == 0.0 && static_cast<double>(scalar) == complex_value.real();
}

bool number_equal(const NumberValue& lhs, const NumberValue& rhs) {
    return std::visit(
        [](const auto& lhs_item, const auto& rhs_item) -> bool {
            using L = std::decay_t<decltype(lhs_item)>;
            using R = std::decay_t<decltype(rhs_item)>;

            if constexpr (std::is_same_v<L, bool> && std::is_same_v<R, bool>) {
                return lhs_item == rhs_item;
            } else if constexpr (std::is_same_v<L, bool> && std::is_same_v<R, IntegerConstant>) {
                return integer_equal_integer(IntegerConstant(std::uint8_t(lhs_item ? 1 : 0)), rhs_item);
            } else if constexpr (std::is_same_v<L, IntegerConstant> && std::is_same_v<R, bool>) {
                return integer_equal_integer(lhs_item, IntegerConstant(std::uint8_t(rhs_item ? 1 : 0)));
            } else if constexpr (std::is_same_v<L, bool> && std::is_same_v<R, double>) {
                return static_cast<double>(lhs_item) == rhs_item;
            } else if constexpr (std::is_same_v<L, double> && std::is_same_v<R, bool>) {
                return lhs_item == static_cast<double>(rhs_item);
            } else if constexpr (std::is_same_v<L, bool> &&
                                 std::is_same_v<R, std::complex<double>>) {
                return scalar_equal_complex(lhs_item, rhs_item);
            } else if constexpr (std::is_same_v<L, std::complex<double>> &&
                                 std::is_same_v<R, bool>) {
                return scalar_equal_complex(rhs_item, lhs_item);
            } else if constexpr (std::is_same_v<L, IntegerConstant> &&
                                 std::is_same_v<R, IntegerConstant>) {
                return integer_equal_integer(lhs_item, rhs_item);
            } else if constexpr (std::is_same_v<L, IntegerConstant> && std::is_same_v<R, double>) {
                return lhs_item.as_double() == rhs_item;
            } else if constexpr (std::is_same_v<L, double> && std::is_same_v<R, IntegerConstant>) {
                return lhs_item == rhs_item.as_double();
            } else if constexpr (std::is_same_v<L, IntegerConstant> &&
                                 std::is_same_v<R, std::complex<double>>) {
                return scalar_equal_complex(lhs_item.as_double(), rhs_item);
            } else if constexpr (std::is_same_v<L, std::complex<double>> &&
                                 std::is_same_v<R, IntegerConstant>) {
                return scalar_equal_complex(rhs_item.as_double(), lhs_item);
            } else if constexpr (std::is_same_v<L, double> && std::is_same_v<R, double>) {
                return lhs_item == rhs_item;
            } else if constexpr (std::is_same_v<L, double> &&
                                 std::is_same_v<R, std::complex<double>>) {
                return scalar_equal_complex(lhs_item, rhs_item);
            } else if constexpr (std::is_same_v<L, std::complex<double>> &&
                                 std::is_same_v<R, double>) {
                return scalar_equal_complex(rhs_item, lhs_item);
            } else if constexpr (std::is_same_v<L, std::complex<double>> &&
                                 std::is_same_v<R, std::complex<double>>) {
                return lhs_item == rhs_item;
            }

            return false;
        },
        lhs, rhs);
}

std::optional<NumberValue> fold_eq(const NumberValue& lhs, const NumberValue& rhs) {
    // 相等比较的常量折叠统一返回 `bool`；`!=` 直接在这个结果上取反，避免两处漂移。
    return NumberValue{number_equal(lhs, rhs)};
}

std::optional<NumberValue> fold_ne(const NumberValue& lhs, const NumberValue& rhs) {
    return NumberValue{!number_equal(lhs, rhs)};
}

bool number_is_truthy(const NumberValue& value) {
    return std::visit(
        [](const auto& item) -> bool {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, bool>) {
                return item;
            } else if constexpr (std::is_same_v<T, IntegerConstant>) {
                return !item.is_zero();
            } else if constexpr (std::is_same_v<T, double>) {
                // MATLAB `&` / `|` 对数值按“非零即真”处理；`NaN != 0.0`，因此这里也视为真。
                return item != 0.0;
            } else if constexpr (std::is_same_v<T, std::complex<double>>) {
                // MATLAB 文档对 element-wise `&` / `|` 的语义是“非零即真”。
                // 复数只要实部或虚部任一非零，就应当视为 true。
                return item.real() != 0.0 || item.imag() != 0.0;
            }
            return false;
        },
        value);
}

std::optional<NumberValue> fold_and(const NumberValue& lhs, const NumberValue& rhs) {
    return NumberValue{number_is_truthy(lhs) && number_is_truthy(rhs)};
}

std::optional<NumberValue> fold_or(const NumberValue& lhs, const NumberValue& rhs) {
    return NumberValue{number_is_truthy(lhs) || number_is_truthy(rhs)};
}

template <typename Compare>
bool integer_ordered_compare(const IntegerConstant& lhs, const IntegerConstant& rhs,
                             Compare compare) {
    auto is_signed = [](IntegerConstant::Type type) {
        switch (type) {
            case IntegerConstant::Type::Int8:
            case IntegerConstant::Type::Int16:
            case IntegerConstant::Type::Int32:
            case IntegerConstant::Type::Int64:
                return true;
            case IntegerConstant::Type::UInt8:
            case IntegerConstant::Type::UInt16:
            case IntegerConstant::Type::UInt32:
            case IntegerConstant::Type::UInt64:
                return false;
        }
        return false;
    };

    auto to_signed = [](const IntegerConstant& value) -> std::int64_t {
        switch (value.type()) {
            case IntegerConstant::Type::Int8:
                return value.as_int8().value();
            case IntegerConstant::Type::Int16:
                return value.as_int16().value();
            case IntegerConstant::Type::Int32:
                return value.as_int32().value();
            case IntegerConstant::Type::Int64:
                return value.as_int64().value();
            case IntegerConstant::Type::UInt8:
            case IntegerConstant::Type::UInt16:
            case IntegerConstant::Type::UInt32:
            case IntegerConstant::Type::UInt64:
                break;
        }
        return 0;
    };

    auto to_unsigned = [](const IntegerConstant& value) -> std::uint64_t {
        switch (value.type()) {
            case IntegerConstant::Type::UInt8:
                return value.as_uint8().value();
            case IntegerConstant::Type::UInt16:
                return value.as_uint16().value();
            case IntegerConstant::Type::UInt32:
                return value.as_uint32().value();
            case IntegerConstant::Type::UInt64:
                return value.as_uint64().value();
            case IntegerConstant::Type::Int8:
            case IntegerConstant::Type::Int16:
            case IntegerConstant::Type::Int32:
            case IntegerConstant::Type::Int64:
                break;
        }
        return 0;
    };

    const bool lhs_signed = is_signed(lhs.type());
    const bool rhs_signed = is_signed(rhs.type());
    if (lhs_signed && rhs_signed) {
        return compare(to_signed(lhs), to_signed(rhs));
    }
    if (!lhs_signed && !rhs_signed) {
        return compare(to_unsigned(lhs), to_unsigned(rhs));
    }

    if (lhs_signed) {
        const std::int64_t lhs_value = to_signed(lhs);
        if (lhs_value < 0) {
            return compare(-1, 0);
        }
        return compare(static_cast<std::uint64_t>(lhs_value), to_unsigned(rhs));
    }

    const std::int64_t rhs_value = to_signed(rhs);
    if (rhs_value < 0) {
        return compare(0, -1);
    }
    return compare(to_unsigned(lhs), static_cast<std::uint64_t>(rhs_value));
}

double number_real_part(const NumberValue& value) {
    return std::visit(
        [](const auto& item) -> double {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, bool>) {
                return static_cast<double>(item);
            } else if constexpr (std::is_same_v<T, IntegerConstant>) {
                return item.as_double();
            } else if constexpr (std::is_same_v<T, double>) {
                return item;
            } else if constexpr (std::is_same_v<T, std::complex<double>>) {
                return item.real();
            }
            return 0.0;
        },
        value);
}

template <typename T>
IntegerConstant to_integer_like_constant(const T& value) {
    if constexpr (std::is_same_v<T, bool>) {
        return IntegerConstant(std::uint8_t(value ? 1 : 0));
    } else {
        return value;
    }
}

template <typename Compare>
std::optional<NumberValue> fold_ordered_compare(const NumberValue& lhs, const NumberValue& rhs,
                                                Compare compare) {
    return std::visit(
        [&](const auto& lhs_item, const auto& rhs_item) -> std::optional<NumberValue> {
            using L = std::decay_t<decltype(lhs_item)>;
            using R = std::decay_t<decltype(rhs_item)>;

            if constexpr ((std::is_same_v<L, bool> || std::is_same_v<L, IntegerConstant>) &&
                          (std::is_same_v<R, bool> || std::is_same_v<R, IntegerConstant>)) {
                const IntegerConstant lhs_integer = to_integer_like_constant(lhs_item);
                const IntegerConstant rhs_integer = to_integer_like_constant(rhs_item);
                // MATLAB 的有序比较对整数照常成立；这里只在纯整数路径上保留精确比较。
                return NumberValue{integer_ordered_compare(lhs_integer, rhs_integer, compare)};
            }

            // MATLAB 对复数做有序比较时只看实部；其他标量也统一按实部参与比较。
            return NumberValue{compare(number_real_part(NumberValue{lhs_item}),
                                       number_real_part(NumberValue{rhs_item}))};
        },
        lhs, rhs);
}

std::optional<NumberValue> fold_gt(const NumberValue& lhs, const NumberValue& rhs) {
    return fold_ordered_compare(lhs, rhs, [](const auto& lhs_item, const auto& rhs_item) {
        return lhs_item > rhs_item;
    });
}

std::optional<NumberValue> fold_ge(const NumberValue& lhs, const NumberValue& rhs) {
    return fold_ordered_compare(lhs, rhs, [](const auto& lhs_item, const auto& rhs_item) {
        return lhs_item >= rhs_item;
    });
}

std::optional<NumberValue> fold_lt(const NumberValue& lhs, const NumberValue& rhs) {
    return fold_ordered_compare(lhs, rhs, [](const auto& lhs_item, const auto& rhs_item) {
        return lhs_item < rhs_item;
    });
}

std::optional<NumberValue> fold_le(const NumberValue& lhs, const NumberValue& rhs) {
    return fold_ordered_compare(lhs, rhs, [](const auto& lhs_item, const auto& rhs_item) {
        return lhs_item <= rhs_item;
    });
}

std::optional<NumberValue> fold_binary(BinOpType op, const NumberValue& lhs, const NumberValue& rhs) {
    switch (op) {
        case BinOpType::Add:
            return fold_add(lhs, rhs);
        case BinOpType::And:
            return fold_and(lhs, rhs);
        case BinOpType::Eq:
            return fold_eq(lhs, rhs);
        case BinOpType::Subtract:
            return fold_subtract(lhs, rhs);
        case BinOpType::Ge:
            return fold_ge(lhs, rhs);
        case BinOpType::Ne:
            return fold_ne(lhs, rhs);
        case BinOpType::Or:
            return fold_or(lhs, rhs);
        case BinOpType::Power:
        case BinOpType::MPower:
            return fold_power(lhs, rhs);
        case BinOpType::Times:
        case BinOpType::Multiply:
            return fold_multiply(lhs, rhs);
        case BinOpType::RDivide:
        case BinOpType::MRightDivide:
            return fold_divide(lhs, rhs);
        case BinOpType::LDivide:
        case BinOpType::MLeftDivide:
            return fold_divide(rhs, lhs);
        case BinOpType::Gt:
            return fold_gt(lhs, rhs);
        case BinOpType::Le:
            return fold_le(lhs, rhs);
        case BinOpType::Lt:
            return fold_lt(lhs, rhs);
        default:
            return std::nullopt;
    }
}

std::optional<NumberValue> try_eval_value(
    ValueId value_id, const analysis::ValueDefAnalysis::Result& defs,
    std::unordered_map<ValueId, EvalEntry>& cache) {
    if (value_id == InvalidValueId) {
        return std::nullopt;
    }

    EvalEntry& entry = cache[value_id];
    if (entry.computed) {
        return entry.value;
    }
    if (entry.visiting) {
        return std::nullopt;
    }

    entry.visiting = true;

    const UntypedSSANode* def_node = defs.definition_of(value_id);
    if (def_node == nullptr) {
        entry.visiting = false;
        entry.computed = true;
        entry.value = std::nullopt;
        return std::nullopt;
    }

    const UntypedSSANode& def = *def_node;
    switch (def.type()) {
        case UntypedSSANode::SSA_Number:
            entry.value = static_cast<const SSANumberNode&>(def).value();
            break;

        case UntypedSSANode::SSA_Copy: {
            const ValueRef src = static_cast<const SSACopyNode&>(def).src();
            entry.value = src.valid() ? try_eval_value(src.id, defs, cache) : std::nullopt;
            break;
        }

        case UntypedSSANode::SSA_UnaryOp: {
            const auto& unary = static_cast<const SSAUnaryOpNode&>(def);
            const ValueRef operand = unary.operand();
            if (!operand.valid()) {
                entry.value = std::nullopt;
                break;
            }

            const std::optional<NumberValue> folded_operand =
                try_eval_value(operand.id, defs, cache);
            entry.value = folded_operand.has_value() ? fold_unary(unary.op(), *folded_operand)
                                                     : std::nullopt;
            break;
        }

        case UntypedSSANode::SSA_BinOp: {
            const auto& binary = static_cast<const SSABinOpNode&>(def);
            const ValueRef lhs = binary.lhs();
            const ValueRef rhs = binary.rhs();
            if (!lhs.valid() || !rhs.valid()) {
                entry.value = std::nullopt;
                break;
            }

            const std::optional<NumberValue> folded_lhs = try_eval_value(lhs.id, defs, cache);
            const std::optional<NumberValue> folded_rhs = try_eval_value(rhs.id, defs, cache);
            entry.value = folded_lhs.has_value() && folded_rhs.has_value()
                              ? fold_binary(binary.op(), *folded_lhs, *folded_rhs)
                              : std::nullopt;
            break;
        }

        case UntypedSSANode::SSA_Text:
        case UntypedSSANode::SSA_Undef:
        case UntypedSSANode::SSA_Phi:
        case UntypedSSANode::SSA_GlobalLoad:
        case UntypedSSANode::SSA_GlobalStore:
        case UntypedSSANode::SSA_CondJump:
        case UntypedSSANode::SSA_Jump:
        case UntypedSSANode::SSA_Return:
            entry.value = std::nullopt;
            break;

        case UntypedSSANode::SSA_Call:
            entry.value = try_fold_call(static_cast<const SSACallNode&>(def), defs, cache);
            break;
    }

    entry.visiting = false;
    entry.computed = true;
    return entry.value;
}

}  // namespace

const char* UntypedSSAConstantFoldPass::name() const {
    return "untyped-ssa-constant-fold";
}

analysis::PreservedAnalyses UntypedSSAConstantFoldPass::run(
    Function& function, analysis::FunctionAnalysisManager& analysis_manager) {
    if (function.stage() != IRNode::UntypedSSA) {
        return analysis::PreservedAnalyses::all();
    }

    const analysis::ValueDefAnalysis::Result& defs =
        analysis_manager.get<analysis::ValueDefAnalysis>(function);
    std::unordered_map<ValueId, EvalEntry> cache;
    bool changed = false;

    for (const auto& block : function.blocks()) {
        if (block == nullptr) {
            continue;
        }

        for (IRNode* node : block->instructions()) {
            auto* unary = dynamic_cast<SSAUnaryOpNode*>(node);
            if (unary != nullptr) {
                const ValueRef operand = unary->operand();
                if (!operand.valid()) {
                    continue;
                }

                const std::optional<NumberValue> operand_value =
                    try_eval_value(operand.id, defs, cache);
                const std::optional<NumberValue> folded_value = operand_value.has_value()
                                                                    ? fold_unary(unary->op(), *operand_value)
                                                                    : std::nullopt;
                if (!folded_value.has_value()) {
                    continue;
                }

                auto* replacement = function.create_node<SSANumberNode>(
                    unary->result(), *folded_value, unary->source_location());
                if (!block->replace_instruction(unary, replacement)) {
                    throw std::runtime_error("常量折叠替换 `SSA_UnaryOpNode` 失败。");
                }
                changed = true;
                continue;
            }

            auto* binary = dynamic_cast<SSABinOpNode*>(node);
            if (binary != nullptr) {
                const ValueRef lhs = binary->lhs();
                const ValueRef rhs = binary->rhs();
                if (!lhs.valid() || !rhs.valid()) {
                    continue;
                }

                const std::optional<NumberValue> lhs_value = try_eval_value(lhs.id, defs, cache);
                const std::optional<NumberValue> rhs_value = try_eval_value(rhs.id, defs, cache);
                const std::optional<NumberValue> folded_value =
                    lhs_value.has_value() && rhs_value.has_value()
                        ? fold_binary(binary->op(), *lhs_value, *rhs_value)
                        : std::nullopt;
                if (!folded_value.has_value()) {
                    continue;
                }

                auto* replacement = function.create_node<SSANumberNode>(
                    binary->result(), *folded_value, binary->source_location());
                if (!block->replace_instruction(binary, replacement)) {
                    throw std::runtime_error("常量折叠替换 `SSABinOpNode` 失败。");
                }
                changed = true;
                continue;
            }

            auto* call = dynamic_cast<SSACallNode*>(node);
            if (call == nullptr) {
                continue;
            }

            const std::optional<NumberValue> folded_value = try_fold_call(*call, defs, cache);
            if (!folded_value.has_value()) {
                continue;
            }

            auto* replacement = function.create_node<SSANumberNode>(
                call->results().front(), *folded_value, call->source_location());
            if (!block->replace_instruction(call, replacement)) {
                throw std::runtime_error("常量折叠替换 `SSACallNode` 失败。");
            }
            changed = true;
        }
    }

    return changed ? analysis::PreservedAnalyses::none() : analysis::PreservedAnalyses::all();
}

}  // namespace optimizer
}  // namespace baltam
