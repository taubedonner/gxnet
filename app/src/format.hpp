// SPDX-License-Identifier: MIT
#pragma once

#include <wx/string.h>

#include <chrono>
#include <cstddef>
#include <string>

#include "gxnet/gxnet.hpp"

namespace gxdemo {

using namespace gxnet;

/// std::string in this program is always UTF-8 -- it comes off the wire that
/// way and goes back the same. FromUTF8 is therefore the only correct
/// conversion; wxString(const char*) would decode in the current locale and
/// mangle every Cyrillic label text on a Russian Windows.
inline wxString wx(const std::string& utf8) { return wxString::FromUTF8(utf8); }
inline wxString wx(std::string_view utf8) { return wxString::FromUTF8(utf8.data(), utf8.size()); }
inline std::string utf8(const wxString& text) {
    const auto buffer = text.utf8_str();
    return std::string(buffer.data(), buffer.length());
}

/// "GW7D  GGW_UNIQUE_DATEN [15.20]", or just the token when the reference has
/// no entry for it.
wxString describeToken(Token token);

/// Symbolic name alone, empty when unknown.
wxString tokenName(Token token);

/// What the reference says a subfunction is for, with its value range and the
/// values it names. Empty when the optional table of meanings was not
/// generated, so every caller has to tolerate that.
wxString tokenMeaningText(Token token, std::size_t value_limit = 12);

/// Value in wire form, with the decoded quantity appended for a dimension.
wxString describeValue(const Value& value);

/// hh:mm:ss.mmm
wxString clockText(std::chrono::system_clock::time_point at);

/// GGL_DATUM1 and friends hold DDMMYY packed into a long.
struct CalendarDate {
    int day = 1;
    int month = 1;
    int year = 0;  ///< two digits, as the device stores it
};

CalendarDate fromDeviceDate(std::int32_t value);
std::int32_t toDeviceDate(const CalendarDate& date);
wxString formatDeviceDate(std::int32_t value);

}  // namespace gxdemo
