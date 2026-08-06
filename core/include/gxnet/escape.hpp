// SPDX-License-Identifier: MIT
#ifndef GXNET_ESCAPE_HPP
#define GXNET_ESCAPE_HPP

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace gxnet {

/// How the target device accepts text.
///
/// The vendor tooling exposes this as two conversion pairs, and the difference
/// is visible on the wire:
///
///   UnicodeDevice   "hello, ... EUR sign and @"  ->  "... EUR sign and @40"
///                   non-ASCII characters travel as themselves; only the
///                   escape lead-in and separators are encoded.
///
///   CodepageDevice  "fuenf EUR"                  ->  "f@C3@BCnf @E2@82@AC"
///                   the text is UTF-8 encoded and every byte above 0x7F is
///                   written as an escape, keeping the transport 7-bit clean.
///
/// Ask the device which it is: SRW_UNICODE_DEVICE (SW85, from 13.00) returns 1
/// for a Unicode device, or call IsUnicodeDevice on the vendor interface.
/// Sending the wrong form is a diagnosable error on the device side
/// (17194 PAE_MC_DATA_CODEPAGEDATA_TO_UNICODE and its mirror image 17195).
enum class TextMode {
    UnicodeDevice,
    CodepageDevice,
};

/// Escaping options for text payloads.
///
/// Text is held as UTF-8 throughout this library. On Windows that means a
/// single conversion at the COM boundary with CP_UTF8 -- never CP_ACP, which
/// silently mangles anything outside the active code page.
struct EscapeOptions {
    /// Escape ';' as well. Not required by the reference, but a text field that
    /// sits next to dimensional values in the same record is safer without it.
    bool escape_semicolon = false;
    /// Escape bytes >= 0x80, i.e. emit the CodepageDevice form.
    bool escape_high_bytes = false;

    static EscapeOptions forMode(TextMode mode) {
        EscapeOptions opts;
        opts.escape_high_bytes = (mode == TextMode::CodepageDevice);
        return opts;
    }
};

/// Escapes control characters (< 0x20 and 0x7F), '@' and '|' as '@' followed by
/// two upper case hexadecimal digits: LF becomes "@0A", '@' becomes "@40".
std::string escapeText(std::string_view text, EscapeOptions opts = {});

/// Reverses escapeText. Returns nullopt on a truncated or malformed escape.
std::optional<std::string> unescapeText(std::string_view text);

/// True if the text can be transmitted without escaping.
bool needsEscaping(std::string_view text, EscapeOptions opts = {});

/// True if the bytes form well-formed UTF-8. Worth checking before sending to
/// a Unicode device: a stray CP1251 byte that survives to the wire produces a
/// label with the wrong text and no error anywhere upstream.
bool isValidUtf8(std::string_view text);

/// Number of Unicode code points, not bytes.
///
/// The reference states text limits in characters (WZT_REMOTE_DISPLAY_TEXT is
/// 180, WZT_REMOTE_SOFTKEY_TEXT is 20). With Cyrillic text in UTF-8 the byte
/// count is roughly double, so checking size() against those limits rejects
/// valid text. Returns nullopt if the input is not well-formed UTF-8.
std::optional<std::size_t> utf8Length(std::string_view text);

}  // namespace gxnet

#endif  // GXNET_ESCAPE_HPP
