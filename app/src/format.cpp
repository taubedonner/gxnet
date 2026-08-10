// SPDX-License-Identifier: MIT
#include "format.hpp"

#include <ctime>

namespace gxdemo {

wxString tokenName(Token token) {
    if (const auto info = Registry::builtin().find(token)) {
        return wx(info->name);
    }
    return {};
}

wxString describeToken(Token token) {
    const auto info = Registry::builtin().find(token);
    if (!info) return wx(token.str());

    wxString out = wx(token.str()) + "  " + wx(info->name);
    if (info->since.valid()) out += "  [" + wx(info->since.str()) + "]";
    if (info->reserved) out += "  (reserved)";
    return out;
}

wxString tokenMeaningText(Token token, std::size_t value_limit) {
    wxString out;
    const auto line = [&out](const wxString& text) {
        if (!out.empty()) out += "\n";
        out += text;
    };

    if (const auto meaning = tokenMeaning(token)) line(wx(*meaning));

    const auto doc = tokenDoc(token);
    if (doc && !doc->range.empty()) line("range: " + wx(doc->range));

    const auto values = tokenValues(token);
    std::size_t shown = 0;
    for (const TokenValue& value : values) {
        if (shown == value_limit) {
            line(wxString::Format("and %zu more", values.size() - shown));
            break;
        }
        line(wxString::Format("%d = %s", value.value, wx(value.text)));
        ++shown;
    }
    return out;
}

wxString describeValue(const Value& value) {
    if (isEmpty(value)) return "-";

    wxString out = wx(encodeValue(value));
    if (const auto* dimension = std::get_if<Dimension>(&value)) {
        out += wxString::Format("   = %.6g %s", dimension->toDouble(), wx(dimension->unit));
    }
    return out;
}

wxString clockText(std::chrono::system_clock::time_point at) {
    const std::time_t seconds = std::chrono::system_clock::to_time_t(at);
    std::tm parts{};
#ifdef _WIN32
    localtime_s(&parts, &seconds);
#else
    localtime_r(&seconds, &parts);
#endif
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(at.time_since_epoch()) % 1000;

    return wxString::Format("%02d:%02d:%02d.%03d", parts.tm_hour, parts.tm_min, parts.tm_sec,
                            static_cast<int>(millis.count()));
}

CalendarDate fromDeviceDate(std::int32_t value) {
    CalendarDate date;
    date.day = value / 10000;
    date.month = (value / 100) % 100;
    date.year = value % 100;
    return date;
}

std::int32_t toDeviceDate(const CalendarDate& date) { return date.day * 10000 + date.month * 100 + date.year; }

wxString formatDeviceDate(std::int32_t value) {
    const CalendarDate date = fromDeviceDate(value);
    return wxString::Format("%02d.%02d.%02d", date.day, date.month, date.year);
}

}  // namespace gxdemo
