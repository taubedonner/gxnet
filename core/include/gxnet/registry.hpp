// SPDX-License-Identifier: MIT
#ifndef GXNET_REGISTRY_HPP
#define GXNET_REGISTRY_HPP

#include <algorithm>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

#include "gxnet/detail/registry_table.hpp"
#include "gxnet/token.hpp"
#include "gxnet/version.hpp"

namespace gxnet {

/// Lookup table built from the vendor subfunction reference.
///
/// The table carries names and introducing versions only. Parameter semantics
/// are deliberately absent: for a number of subfunctions the reference itself
/// documents nothing beyond the name, and inventing ranges would be worse than
/// admitting the gap.
///
/// The whole table is constexpr, so a lookup written against a literal token is
/// resolved by the compiler and costs nothing at run time. See knownToken()
/// below for the compile-time form.
class Registry {
public:
    constexpr explicit Registry(std::span<const TokenInfo> entries) : entries_(entries) {}

    /// The table compiled into the library.
    static constexpr Registry builtin() { return Registry{detail::kRegistryTable}; }

    constexpr std::optional<TokenInfo> find(Token token) const {
        const auto it = std::lower_bound(entries_.begin(), entries_.end(), token,
                                         [](const TokenInfo& e, const Token& t) { return e.token < t; });
        if (it == entries_.end() || it->token != token) return std::nullopt;
        return *it;
    }

    constexpr std::optional<TokenInfo> findByName(std::string_view name) const {
        for (const TokenInfo& e : entries_) {
            if (e.name == name) return e;
        }
        return std::nullopt;
    }

    /// Every entry, ordered by token.
    constexpr std::span<const TokenInfo> entries() const { return entries_; }

    constexpr std::size_t size() const { return entries_.size(); }

private:
    std::span<const TokenInfo> entries_;
};

/// Convenience wrappers over Registry::builtin().
constexpr std::optional<std::string_view> tokenName(Token token) {
    const auto info = Registry::builtin().find(token);
    if (!info) return std::nullopt;
    return info->name;
}

constexpr std::optional<Token> tokenByName(std::string_view name) {
    const auto info = Registry::builtin().findByName(name);
    if (!info) return std::nullopt;
    return info->token;
}

/// Compile-time lookup. A token that is well formed but absent from the table
/// fails to compile, which is the point: control sequences name their tokens as
/// string literals, and `"GW7E"_tok` instead of `"GW7D"_tok` would otherwise be
/// found out on a running line.
///
///     constexpr Token kUniqueData = knownToken("GGW_UNIQUE_DATEN");
///
/// Both spellings are accepted, the four character token and the symbolic name,
/// because the reference and the engineers who answer questions about it use
/// different ones.
consteval TokenInfo knownToken(std::string_view text) {
    if (const auto token = Token::parse(text)) {
        if (const auto info = Registry::builtin().find(*token)) return *info;
        throw "token is well formed but absent from the registry";
    }
    if (const auto info = Registry::builtin().findByName(text)) return *info;
    throw "neither a valid token nor a known symbolic name";
}

// The table must be sorted for find() to work, and it is emitted sorted by
// token text. That happens to match Token's member-wise ordering, but checking
// beats trusting: a future reference revision could introduce a token whose
// letters break the assumption.
static_assert(std::is_sorted(detail::kRegistryTable.begin(), detail::kRegistryTable.end(),
                             [](const TokenInfo& a, const TokenInfo& b) { return a.token < b.token; }),
              "registry table must be sorted by token");

}  // namespace gxnet

#endif  // GXNET_REGISTRY_HPP
