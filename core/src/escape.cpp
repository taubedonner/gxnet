// SPDX-License-Identifier: MIT
#include "gxnet/escape.hpp"

namespace gxnet {
namespace {

const char* kHex = "0123456789ABCDEF";

bool mustEscape(unsigned char c, const EscapeOptions& opts) {
    if (c < 0x20 || c == 0x7F) return true;  // control characters
    if (c == '@' || c == '|') return true;   // escape lead-in and separator
    if (opts.escape_semicolon && c == ';') return true;
    if (opts.escape_high_bytes && c >= 0x80) return true;
    return false;
}

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

}  // namespace

std::string escapeText(std::string_view text, EscapeOptions opts) {
    std::string out;
    out.reserve(text.size());
    for (char ch : text) {
        auto c = static_cast<unsigned char>(ch);
        if (mustEscape(c, opts)) {
            out += '@';
            out += kHex[(c >> 4) & 0xF];
            out += kHex[c & 0xF];
        } else {
            out += ch;
        }
    }
    return out;
}

std::optional<std::string> unescapeText(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '@') {
            out += text[i];
            continue;
        }
        if (i + 2 >= text.size()) return std::nullopt;  // truncated escape
        int hi = hexDigit(text[i + 1]);
        int lo = hexDigit(text[i + 2]);
        if (hi < 0 || lo < 0) return std::nullopt;
        out += static_cast<char>(hi * 16 + lo);
        i += 2;
    }
    return out;
}

bool isValidUtf8(std::string_view text) {
    std::size_t i = 0;
    while (i < text.size()) {
        auto c = static_cast<unsigned char>(text[i]);
        int extra;
        unsigned int cp;
        if (c < 0x80) {
            ++i;
            continue;
        } else if ((c & 0xE0) == 0xC0) {
            extra = 1;
            cp = c & 0x1Fu;
        } else if ((c & 0xF0) == 0xE0) {
            extra = 2;
            cp = c & 0x0Fu;
        } else if ((c & 0xF8) == 0xF0) {
            extra = 3;
            cp = c & 0x07u;
        } else
            return false;  // continuation byte or 5+ byte form

        if (i + static_cast<std::size_t>(extra) >= text.size()) return false;
        for (int k = 1; k <= extra; ++k) {
            auto cc = static_cast<unsigned char>(text[i + static_cast<std::size_t>(k)]);
            if ((cc & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (cc & 0x3Fu);
        }
        // Reject over-long forms, surrogates and out-of-range code points.
        if (extra == 1 && cp < 0x80) return false;
        if (extra == 2 && cp < 0x800) return false;
        if (extra == 3 && cp < 0x10000) return false;
        if (cp > 0x10FFFF) return false;
        if (cp >= 0xD800 && cp <= 0xDFFF) return false;
        i += static_cast<std::size_t>(extra) + 1;
    }
    return true;
}

std::optional<std::size_t> utf8Length(std::string_view text) {
    if (!isValidUtf8(text)) return std::nullopt;
    std::size_t count = 0;
    for (char ch : text) {
        if ((static_cast<unsigned char>(ch) & 0xC0) != 0x80) ++count;
    }
    return count;
}

bool needsEscaping(std::string_view text, EscapeOptions opts) {
    for (char ch : text) {
        if (mustEscape(static_cast<unsigned char>(ch), opts)) return true;
    }
    return false;
}

}  // namespace gxnet
