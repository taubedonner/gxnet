// SPDX-License-Identifier: MIT
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "gxnet/token.hpp"
#include "gxnet/value.hpp"
#include "gxnet/version.hpp"

namespace gxnet {

// --- Version --------------------------------------------------------------
//
// Version::parse and the whole of Token now live in the headers so they can be
// evaluated at compile time; only what needs formatting stays here.

std::string Version::str() const {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d.%02d", major, minor);
    return buf;
}

// --- Token ----------------------------------------------------------------

std::string Token::str() const {
    static const char* kHex = "0123456789ABCDEF";
    std::string out(4, '\0');
    out[0] = group_letter;
    out[1] = type_letter;
    out[2] = kHex[(index >> 4) & 0xF];
    out[3] = kHex[index & 0xF];
    return out;
}

// --- Dimension ------------------------------------------------------------

double Dimension::toDouble() const { return static_cast<double>(mantissa) * std::pow(10.0, exponent); }

std::string Dimension::encode(char separator) const {
    std::string out = unit;
    out += separator;
    out += std::to_string(exponent);
    out += separator;
    out += std::to_string(mantissa);
    return out;
}

std::optional<Dimension> Dimension::parse(std::string_view text, char separator) {
    std::size_t first = text.find(separator);
    if (first == std::string_view::npos) return std::nullopt;
    std::size_t second = text.find(separator, first + 1);
    if (second == std::string_view::npos) return std::nullopt;
    // A third separator would make the field ambiguous.
    if (text.find(separator, second + 1) != std::string_view::npos) {
        return std::nullopt;
    }

    auto toLong = [](std::string_view s, std::int64_t& out) -> bool {
        if (s.empty()) return false;
        bool neg = false;
        std::size_t i = 0;
        if (s[0] == '-' || s[0] == '+') {
            neg = s[0] == '-';
            i = 1;
            if (s.size() == 1) return false;
        }
        std::int64_t v = 0;
        for (; i < s.size(); ++i) {
            if (s[i] < '0' || s[i] > '9') return false;
            v = v * 10 + (s[i] - '0');
        }
        out = neg ? -v : v;
        return true;
    };

    std::int64_t exponent = 0;
    std::int64_t mantissa = 0;
    if (!toLong(text.substr(first + 1, second - first - 1), exponent)) {
        return std::nullopt;
    }
    if (!toLong(text.substr(second + 1), mantissa)) return std::nullopt;
    if (exponent < -30 || exponent > 30) return std::nullopt;

    return Dimension(std::string(text.substr(0, first)), static_cast<int>(exponent), mantissa);
}

// --- Value ----------------------------------------------------------------

bool isEmpty(const Value& v) { return std::holds_alternative<std::monostate>(v); }

std::string encodeValue(const Value& v, char dimension_separator) {
    struct Visitor {
        char sep;
        std::string operator()(std::monostate) const { return {}; }
        std::string operator()(std::int16_t x) const { return std::to_string(x); }
        std::string operator()(std::int32_t x) const { return std::to_string(x); }
        std::string operator()(const Dimension& d) const { return d.encode(sep); }
        std::string operator()(const std::string& s) const { return s; }
    };
    return std::visit(Visitor{dimension_separator}, v);
}

}  // namespace gxnet
