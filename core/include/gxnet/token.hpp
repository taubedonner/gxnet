// SPDX-License-Identifier: MIT
#ifndef GXNET_TOKEN_HPP
#define GXNET_TOKEN_HPP

#include <array>
#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace gxnet {

/// Command group. The letter is the first character of a token; the numeric
/// value is the high nibble of the command class code.
enum class Group : std::uint8_t {
    Basic = 0x0,           ///< G - basic unit
    AutoLabeler = 0x1,     ///< A - automatic labeler
    PackageSync = 0x3,     ///< P - package synchronous data
    Logic = 0x4,           ///< L - protocol logic
    Database = 0x5,        ///< D - database tables
    LabelResource = 0x6,   ///< E - labels, logos, fonts
    ServiceBackup = 0x7,   ///< S - service, backup, diagnostics
    AutoLabelerB = 0x8,    ///< B - automatic labeler, second range
    Tool = 0x9,            ///< W - tool / dialog
    Control = 0xA,         ///< X - control commands
    PackingMachine = 0xB,  ///< V - packing machine
    Medium = 0xD,          ///< M - storage medium
};

/// Payload type. The letter is the second character of a token; the numeric
/// value is the low nibble of the command class code.
enum class DataType : std::uint8_t {
    Command = 0,    ///< X - no payload
    Word = 1,       ///< W - 16-bit signed
    Long = 2,       ///< L - 32-bit signed
    Dimension = 3,  ///< D - dimensional quantity (unit, exponent, mantissa)
    Block = 6,      ///< V - block command, terminated by LX02
    Text = 7,       ///< T - text
};

namespace detail {

struct GroupEntry {
    char letter;
    Group group;
    const char* name;
};

// Group letters and their nibble in the command class code. 0x2 and 0xC are
// unassigned in the reference.
inline constexpr std::array<GroupEntry, 12> kGroups{{
    {'G', Group::Basic, "basic unit"},
    {'A', Group::AutoLabeler, "automatic labeler"},
    {'P', Group::PackageSync, "package synchronous"},
    {'L', Group::Logic, "logic"},
    {'D', Group::Database, "database"},
    {'E', Group::LabelResource, "label resources"},
    {'S', Group::ServiceBackup, "service/backup"},
    {'B', Group::AutoLabelerB, "automatic labeler (B)"},
    {'W', Group::Tool, "tool"},
    {'X', Group::Control, "control"},
    {'V', Group::PackingMachine, "packing machine"},
    {'M', Group::Medium, "storage medium"},
}};

struct TypeEntry {
    char letter;
    DataType type;
    const char* name;
    int arity;
};

inline constexpr std::array<TypeEntry, 6> kTypes{{
    {'X', DataType::Command, "command", 0},
    {'W', DataType::Word, "word", 1},
    {'L', DataType::Long, "long", 1},
    {'D', DataType::Dimension, "dimension", 1},
    {'V', DataType::Block, "block", 0},
    {'T', DataType::Text, "text", 1},
}};

constexpr int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

}  // namespace detail

/// Number of payload fields a token consumes in the data line.
/// Command and Block tokens carry none; every other type carries exactly one.
constexpr int payloadArity(DataType type) {
    for (const auto& e : detail::kTypes) {
        if (e.type == type) return e.arity;
    }
    return 0;
}

constexpr const char* groupName(Group g) {
    for (const auto& e : detail::kGroups) {
        if (e.group == g) return e.name;
    }
    return "unknown";
}

constexpr const char* dataTypeName(DataType t) {
    for (const auto& e : detail::kTypes) {
        if (e.type == t) return e.name;
    }
    return "unknown";
}

/// A four character subfunction identifier such as "GW7D" or "LX02":
/// group letter, type letter, then a two digit hexadecimal index.
struct Token {
    char group_letter = 'G';
    char type_letter = 'X';
    std::uint8_t index = 0;

    constexpr Token() = default;
    constexpr Token(char group, char type, std::uint8_t idx) : group_letter(group), type_letter(type), index(idx) {}

    /// Parses a token, rejecting unknown group/type letters and malformed
    /// index digits. Lower case hexadecimal digits are accepted.
    static constexpr std::optional<Token> parse(std::string_view text);

    std::string str() const;

    constexpr std::optional<Group> group() const {
        for (const auto& e : detail::kGroups) {
            if (e.letter == group_letter) return e.group;
        }
        return std::nullopt;
    }

    constexpr std::optional<DataType> type() const {
        for (const auto& e : detail::kTypes) {
            if (e.letter == type_letter) return e.type;
        }
        return std::nullopt;
    }

    /// Command class code, (group << 4) | type. Returns nullopt if either
    /// letter is unknown.
    constexpr std::optional<std::uint8_t> classCode() const {
        const auto g = group();
        const auto t = type();
        if (!g || !t) return std::nullopt;
        return static_cast<std::uint8_t>((static_cast<std::uint8_t>(*g) << 4) | static_cast<std::uint8_t>(*t));
    }

    /// The inverse of `classCode`: rebuilds a token from the class code and
    /// index it is encoded as. Returns nullopt for the unassigned nibbles.
    ///
    /// Needed wherever a token appears as a number rather than as text. The
    /// binary form encodes every token this way, and LGW_UFKENN carries one as
    /// an ordinary word value: that is how a label field says which subfunction
    /// supplies its content, so a layout export is full of them.
    static constexpr std::optional<Token> fromClassCode(std::uint8_t cls, std::uint8_t index);

    /// Convenience: payload arity of this token's type, or 0 if unknown.
    constexpr int arity() const {
        const auto t = type();
        return t ? payloadArity(*t) : 0;
    }

    constexpr bool isBlock() const { return type_letter == 'V'; }
    /// True for LX02, the logical terminator of a block command.
    constexpr bool isBlockClose() const { return group_letter == 'L' && type_letter == 'X' && index == 0x02; }

    /// Member order is the comparison order, which is also the order the
    /// registry table is sorted in.
    friend constexpr auto operator<=>(const Token&, const Token&) = default;
    friend constexpr bool operator==(const Token&, const Token&) = default;
};

constexpr std::optional<Token> Token::fromClassCode(std::uint8_t cls, std::uint8_t index) {
    char group_letter = 0;
    for (const auto& e : detail::kGroups) {
        if (static_cast<std::uint8_t>(e.group) == (cls >> 4)) group_letter = e.letter;
    }
    char type_letter = 0;
    for (const auto& e : detail::kTypes) {
        if (static_cast<std::uint8_t>(e.type) == (cls & 0x0F)) type_letter = e.letter;
    }
    if (group_letter == 0 || type_letter == 0) return std::nullopt;
    return Token{group_letter, type_letter, index};
}

constexpr std::optional<Token> Token::parse(std::string_view text) {
    if (text.size() != 4) return std::nullopt;

    const char group = text[0];
    const char type = text[1];

    // Any upper case letter is accepted as a group. The bundled table covers
    // the Gx device family; the Ix family uses further letters (for example the
    // R group seen in LV01|RX01 registration telegrams), and rejecting those
    // outright would make the parser useless for mixed installations. Unknown
    // groups are reported by validate() instead, where they are a warning.
    if (group < 'A' || group > 'Z') return std::nullopt;

    bool type_ok = false;
    for (const auto& e : detail::kTypes) {
        if (e.letter == type) {
            type_ok = true;
            break;
        }
    }
    if (!type_ok) return std::nullopt;

    const int hi = detail::hexDigit(text[2]);
    const int lo = detail::hexDigit(text[3]);
    if (hi < 0 || lo < 0) return std::nullopt;

    return Token(group, type, static_cast<std::uint8_t>(hi * 16 + lo));
}

/// The block terminator, LGX_CLOSE.
constexpr Token blockClose() { return Token('L', 'X', 0x02); }

inline namespace literals {

/// Compile-time token literal: `"GW7D"_tok`.
///
/// A malformed token is a compile error rather than a nullopt discovered at
/// run time, which matters for the token constants wired into control
/// sequences -- a typo there would otherwise surface on a running line.
consteval Token operator""_tok(const char* text, std::size_t size) {
    const auto token = Token::parse(std::string_view(text, size));
    if (!token) throw "malformed GxNet token";
    return *token;
}

}  // namespace literals

}  // namespace gxnet

#endif  // GXNET_TOKEN_HPP
