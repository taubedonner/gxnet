// SPDX-License-Identifier: MIT
#include "gxnet/gxnet_c.h"

#include <cstring>
#include <string>

#include "gxnet/gxnet.hpp"

namespace {

bool copyOut(const std::string& src, char* dst, std::size_t capacity) {
    if (!dst || capacity == 0) return false;
    if (src.size() + 1 > capacity) {
        // Truncate but stay NUL terminated so the caller can still log it.
        std::memcpy(dst, src.data(), capacity - 1);
        dst[capacity - 1] = '\0';
        return false;
    }
    std::memcpy(dst, src.data(), src.size());
    dst[src.size()] = '\0';
    return true;
}

void fillInfo(const gxnet::TokenInfo& in, gxnet_token_info_t* out) {
    std::memset(out, 0, sizeof(*out));
    const std::string token = in.token.str();
    std::strncpy(out->token, token.c_str(), sizeof(out->token) - 1);
    std::strncpy(out->name, std::string(in.name).c_str(), sizeof(out->name) - 1);
    if (in.since.valid()) {
        std::strncpy(out->since, in.since.str().c_str(), sizeof(out->since) - 1);
    }
    out->reserved = in.reserved ? 1 : 0;
    auto group = in.token.group();
    auto type = in.token.type();
    out->group_code = group ? static_cast<int>(*group) : -1;
    out->type_code = type ? static_cast<int>(*type) : -1;
    out->arity = in.token.arity();
}

std::string joinDiagnostics(const std::vector<gxnet::Diagnostic>& diags) {
    std::string out;
    for (const gxnet::Diagnostic& d : diags) {
        out += d.str();
        out += '\n';
    }
    return out;
}

}  // namespace

extern "C" {

int gxnet_registry_size(void) { return static_cast<int>(gxnet::Registry::builtin().size()); }

int gxnet_lookup_token(const char* token, gxnet_token_info_t* out) {
    if (!token || !out) return GXNET_ERR_ARGUMENT;
    auto parsed = gxnet::Token::parse(token);
    if (!parsed) return GXNET_ERR_PARSE;
    auto info = gxnet::Registry::builtin().find(*parsed);
    if (!info) return GXNET_ERR_NOT_FOUND;
    fillInfo(*info, out);
    return GXNET_OK;
}

int gxnet_lookup_name(const char* name, gxnet_token_info_t* out) {
    if (!name || !out) return GXNET_ERR_ARGUMENT;
    auto info = gxnet::Registry::builtin().findByName(name);
    if (!info) return GXNET_ERR_NOT_FOUND;
    fillInfo(*info, out);
    return GXNET_OK;
}

int gxnet_validate(const char* header_line, const char* data_line, const char* device_version, char* diagnostics,
                   size_t diagnostics_capacity) {
    if (!header_line) return GXNET_ERR_ARGUMENT;

    auto header = gxnet::parseHeader(header_line);
    if (!header) {
        copyOut("error [header.parse]: " + header.error.message, diagnostics, diagnostics_capacity);
        return GXNET_ERR_PARSE;
    }

    gxnet::Telegram telegram;
    telegram.header = *header;
    if (data_line && data_line[0] != '\0') {
        auto record = gxnet::parseRecord(telegram.header, data_line);
        if (!record) {
            copyOut("error [record.parse]: " + record.error.message, diagnostics, diagnostics_capacity);
            return GXNET_ERR_PARSE;
        }
        telegram.records.push_back(*record);
    }

    gxnet::ValidateOptions opts;
    if (device_version && device_version[0] != '\0') {
        if (auto v = gxnet::Version::parse(device_version)) {
            opts.device_version = *v;
        } else {
            copyOut("error [version.parse]: cannot parse device version", diagnostics, diagnostics_capacity);
            return GXNET_ERR_ARGUMENT;
        }
    }

    auto diags = validate(telegram, opts);
    copyOut(joinDiagnostics(diags), diagnostics, diagnostics_capacity);

    int errors = 0;
    for (const gxnet::Diagnostic& d : diags) {
        if (d.severity == gxnet::Severity::Error) ++errors;
    }
    return errors;
}

int gxnet_to_one_line(const char* header_line, const char* data_line, char* out, size_t capacity) {
    if (!header_line || !out) return GXNET_ERR_ARGUMENT;

    auto header = gxnet::parseHeader(header_line);
    if (!header) {
        copyOut(header.error.message, out, capacity);
        return GXNET_ERR_PARSE;
    }

    gxnet::Telegram telegram;
    telegram.header = *header;
    auto record = gxnet::parseRecord(telegram.header, data_line ? data_line : "");
    if (!record) {
        copyOut(record.error.message, out, capacity);
        return GXNET_ERR_PARSE;
    }
    telegram.records.push_back(*record);

    auto line = gxnet::encodeOneLine(telegram);
    if (!line) {
        copyOut(line.error.message, out, capacity);
        return GXNET_ERR_PARSE;
    }
    return copyOut(*line, out, capacity) ? GXNET_OK : GXNET_ERR_BUFFER;
}

int gxnet_split_one_line(const char* one_line, char* header_out, size_t header_capacity, char* data_out,
                         size_t data_capacity) {
    if (!one_line || !header_out || !data_out) return GXNET_ERR_ARGUMENT;

    auto telegram = gxnet::parseOneLine(one_line);
    if (!telegram) {
        copyOut(telegram.error.message, header_out, header_capacity);
        return GXNET_ERR_PARSE;
    }

    const std::string header = gxnet::encodeHeader(telegram->header);
    auto data = gxnet::encodeRecord(telegram->header, telegram->records.front());
    if (!data) {
        copyOut(data.error.message, data_out, data_capacity);
        return GXNET_ERR_PARSE;
    }

    bool ok = copyOut(header, header_out, header_capacity);
    ok = copyOut(*data, data_out, data_capacity) && ok;
    return ok ? GXNET_OK : GXNET_ERR_BUFFER;
}

int gxnet_escape(const char* text, char* out, size_t capacity) {
    if (!text || !out) return GXNET_ERR_ARGUMENT;
    return copyOut(gxnet::escapeText(text), out, capacity) ? GXNET_OK : GXNET_ERR_BUFFER;
}

int gxnet_unescape(const char* text, char* out, size_t capacity) {
    if (!text || !out) return GXNET_ERR_ARGUMENT;
    auto result = gxnet::unescapeText(text);
    if (!result) return GXNET_ERR_PARSE;
    return copyOut(*result, out, capacity) ? GXNET_OK : GXNET_ERR_BUFFER;
}

}  // extern "C"
