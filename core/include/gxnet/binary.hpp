// SPDX-License-Identifier: MIT
#ifndef GXNET_BINARY_HPP
#define GXNET_BINARY_HPP

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gxnet/codec.hpp"
#include "gxnet/telegram.hpp"

namespace gxnet {

/// Codec for the compact binary form that appears in device communication logs.
///
/// The textual form (`A!GW7D|0`) is what the vendor server accepts from a
/// client; on the wire it re-encodes telegrams into this binary form. Being able
/// to read it turns a raw capture into something reviewable.
///
/// Layout, established by decoding captured traffic and cross-checking every
/// field against the subfunction reference:
///
///   [frame header: 4 bytes]  direction and bus addresses, carried opaquely
///   then, repeatedly:
///   [class: 1][index: 1][payload]
///
/// where class is `(group << 4) | type`, exactly as in the textual form, and
/// payload length follows from the type:
///
///   X  command     0 bytes
///   W  word        2 bytes, signed big endian
///   L  long        4 bytes, signed big endian
///   D  dimension   2 bytes unit/exponent field, then 4 bytes signed mantissa
///   T  text        2 bytes length, then that many bytes
///   V  block       2 bytes length, then that many bytes, the last two of which
///                  are the LX02 terminator
///
/// The dimension unit/exponent field packs a unit code in the upper ten bits and
/// a six-bit signed decimal exponent in the lower six. This was inferred from
/// captured weights, tare and prices and holds across every sample seen, but it
/// is not vendor-documented; confirm before relying on it for anything that
/// leaves a mark on a label.
namespace binary {

/// Maps numeric unit codes to the unit text used by the textual form.
using UnitTable = std::vector<std::pair<int, std::string>>;

/// Unit codes observed in captures, with the meaning inferred from context:
/// code 3 accompanies weights, code 0 accompanies prices. Provisional.
const UnitTable& inferredUnitTable();

struct Options {
    /// Unit code translation. When a code is absent the unit is rendered as
    /// "#<code>", which round-trips exactly.
    UnitTable units;

    /// Consume and emit the four leading frame bytes.
    bool frame_header = true;

    /// Require every block length field to end on an LX02. Captures agree with
    /// this; turn it off to be lenient with unfamiliar traffic.
    bool require_block_terminator = true;
};

/// A decoded frame: the opaque header bytes plus the token tree. Unlike the
/// textual form, values travel inline with their tokens.
struct Frame {
    std::array<std::uint8_t, 4> header{};
    bool has_header = true;
    std::vector<Node> nodes;
};

Result<Frame> parse(const std::uint8_t* data, std::size_t size, const Options& opts = {});
Result<Frame> parse(const std::vector<std::uint8_t>& data, const Options& opts = {});

Result<std::vector<std::uint8_t>> encode(const Frame& frame, const Options& opts = {});

/// Convenience wrappers for log lines, which are written as hexadecimal.
/// fromHex ignores whitespace and is case insensitive.
std::vector<std::uint8_t> fromHex(std::string_view hex);
std::string toHex(const std::vector<std::uint8_t>& bytes);
Result<Frame> parseHex(std::string_view hex, const Options& opts = {});

/// Flattens a decoded frame into the textual model, so the same validation and
/// encoding paths apply. The binary form carries no `A!` prefix, so the family
/// and access must be supplied by the caller.
Telegram toTelegram(const Frame& frame, Family family = Family::Automatic, Access access = Access::Write);

/// Packs and unpacks the dimension unit/exponent field.
std::uint16_t packUnitField(int unit_code, int exponent);
void unpackUnitField(std::uint16_t field, int& unit_code, int& exponent);

}  // namespace binary
}  // namespace gxnet

#endif  // GXNET_BINARY_HPP
