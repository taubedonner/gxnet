// SPDX-License-Identifier: MIT
#ifndef GXNET_CODEC_HPP
#define GXNET_CODEC_HPP

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gxnet/escape.hpp"
#include "gxnet/telegram.hpp"

namespace gxnet {

/// Failure of a parse or encode step.
struct CodecError {
    std::string message;
    std::size_t offset = 0;  ///< field index, or byte offset where meaningful

    explicit operator bool() const { return !message.empty(); }
};

/// Result carrying either a value or an error.
template<typename T>
struct Result {
    std::optional<T> value;
    CodecError error;

    bool ok() const { return value.has_value(); }
    explicit operator bool() const { return ok(); }
    const T& operator*() const { return *value; }
    const T* operator->() const { return &*value; }
};

struct EncodeOptions {
    EscapeOptions escape;
    /// Append CR/LF to each produced line.
    bool crlf = false;
};

// --- encoding -------------------------------------------------------------

/// Encodes the header line, e.g. "A!PV04|PW02|GL19|LX02".
std::string encodeHeader(const Header& header, const EncodeOptions& opts = {});

/// Encodes one data record, e.g. "7|1".
Result<std::string> encodeRecord(const Header& header, const Record& record, const EncodeOptions& opts = {});

/// Encodes header and records as separate lines, for the two-argument Send
/// form. Element 0 is the header.
Result<std::vector<std::string>> encodeLines(const Telegram& telegram, const EncodeOptions& opts = {});

/// Encodes a single-record telegram in the interleaved SendOne form, where
/// each payload token is immediately followed by its value:
/// "A!PV04|PW02|7|GL19|1|LX02".
Result<std::string> encodeOneLine(const Telegram& telegram, const EncodeOptions& opts = {});

// --- parsing --------------------------------------------------------------

/// Parses a header line into its token tree. Blocks are nested and the
/// terminating LX02 is consumed into the tree structure.
Result<Header> parseHeader(std::string_view line);

/// Parses one data record against a header layout, converting each field to
/// the type demanded by its token.
Result<Record> parseRecord(const Header& header, std::string_view line);

/// Parses a header line followed by zero or more record lines.
Result<Telegram> parseLines(const std::vector<std::string>& lines);

/// Parses the interleaved SendOne form into a single-record telegram.
Result<Telegram> parseOneLine(std::string_view line);

/// Splits on '|' without interpreting escapes. Exposed because callers
/// sometimes need to inspect a malformed line field by field.
std::vector<std::string_view> splitFields(std::string_view line);

}  // namespace gxnet

#endif  // GXNET_CODEC_HPP
