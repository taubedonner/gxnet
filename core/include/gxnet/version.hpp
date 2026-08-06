// SPDX-License-Identifier: MIT
#ifndef GXNET_VERSION_HPP
#define GXNET_VERSION_HPP

#include <compare>
#include <optional>
#include <string>
#include <string_view>

namespace gxnet {

/// A Gx software version of the form "MM.mm" (e.g. "16.40").
///
/// The vendor reference tags every subfunction with the release in which it
/// first appeared. Comparing that tag against the release actually running on
/// the device is the cheapest way to catch "command silently ignored" bugs
/// before they reach the line.
struct Version {
    int major = 0;
    int minor = 0;

    constexpr Version() = default;
    constexpr Version(int maj, int min) : major(maj), minor(min) {}

    /// Parses "MM.mm"; also tolerates the "MM.mm.bbbb" build form reported by
    /// SRT_GX_VERSION (ST8D), ignoring the build number.
    static constexpr std::optional<Version> parse(std::string_view text);

    std::string str() const;

    constexpr bool valid() const { return major > 0 || minor > 0; }

    /// Member order is the comparison order: major first, then minor.
    friend constexpr auto operator<=>(Version, Version) = default;
    friend constexpr bool operator==(Version, Version) = default;
};

constexpr std::optional<Version> Version::parse(std::string_view text) {
    const std::size_t dot = text.find('.');
    if (dot == std::string_view::npos || dot == 0) return std::nullopt;

    constexpr auto toInt = [](std::string_view s) -> std::optional<int> {
        if (s.empty() || s.size() > 4) return std::nullopt;
        int v = 0;
        for (char c : s) {
            if (c < '0' || c > '9') return std::nullopt;
            v = v * 10 + (c - '0');
        }
        return v;
    };

    const auto maj = toInt(text.substr(0, dot));
    if (!maj) return std::nullopt;

    std::string_view rest = text.substr(dot + 1);
    if (const std::size_t dot2 = rest.find('.'); dot2 != std::string_view::npos) {
        rest = rest.substr(0, dot2);
    }

    const auto min = toInt(rest);
    if (!min) return std::nullopt;

    return Version{*maj, *min};
}

}  // namespace gxnet

#endif  // GXNET_VERSION_HPP
