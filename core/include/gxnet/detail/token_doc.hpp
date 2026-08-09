// SPDX-License-Identifier: MIT
#ifndef GXNET_DETAIL_TOKEN_DOC_HPP
#define GXNET_DETAIL_TOKEN_DOC_HPP

#include <cstdint>
#include <string_view>

#include "gxnet/token.hpp"

namespace gxnet {

/// One value of a subfunction together with the name the reference gives it.
struct TokenValue {
    std::int32_t value;
    std::string_view text;
};

/// What the reference says about a subfunction beyond its name.
///
/// Kept apart from TokenInfo because it comes from a separate, optional table
/// that is not part of the repository: a checkout without it still builds, and
/// every accessor reports nothing.
///
/// Values are indices into one flat array shared by the table rather than a
/// container per entry, which keeps the whole thing constexpr.
struct TokenDoc {
    Token token;
    std::string_view description;  ///< empty when the reference gives none
    std::string_view range;        ///< value range as printed
    std::uint16_t first_value = 0;
    std::uint16_t value_count = 0;
};

}  // namespace gxnet

#endif  // GXNET_DETAIL_TOKEN_DOC_HPP
