// SPDX-License-Identifier: MIT
#ifndef GXNET_VALUE_HPP
#define GXNET_VALUE_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace gxnet {

/// A dimensional quantity: unit, decimal exponent and integer mantissa.
///
/// Wire form is `unit<sep>exponent<sep>mantissa`, for example `KG;-3;2995`
/// meaning 2.995 kg, or `EUR;-2;1990` meaning 19.90 EUR. The separator is ';'
/// in the current header format and '|' in the legacy `!`/`?` format.
struct Dimension {
    std::string unit;
    int exponent = 0;
    std::int64_t mantissa = 0;

    Dimension() = default;
    Dimension(std::string u, int exp, std::int64_t mant) : unit(std::move(u)), exponent(exp), mantissa(mant) {}

    /// Numeric value as a double. Provided for display only: financial and
    /// legal-for-trade arithmetic should stay on the integer mantissa.
    double toDouble() const;

    std::string encode(char separator = ';') const;
    static std::optional<Dimension> parse(std::string_view text, char separator = ';');

    friend bool operator==(const Dimension& a, const Dimension& b) {
        return a.unit == b.unit && a.exponent == b.exponent && a.mantissa == b.mantissa;
    }
    friend bool operator!=(const Dimension& a, const Dimension& b) { return !(a == b); }
};

/// A payload field. Monostate means "this token carries no payload", which is
/// the case for Command (X) and Block (V) tokens.
using Value = std::variant<std::monostate, std::int16_t, std::int32_t, Dimension, std::string>;

bool isEmpty(const Value& v);

/// Renders a value in wire form. Text values are returned raw; escaping is
/// applied by the codec, not here.
std::string encodeValue(const Value& v, char dimension_separator = ';');

/// Limits of the Word and Long payload types.
constexpr std::int32_t kWordMin = -32768;
constexpr std::int32_t kWordMax = 32767;

}  // namespace gxnet

#endif  // GXNET_VALUE_HPP
