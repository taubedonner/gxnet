// SPDX-License-Identifier: MIT
#include "gxnet/codec.hpp"

#include <cstdlib>

namespace gxnet {
namespace {

const char* familyLetter(Family f) {
    switch (f) {
        case Family::Automatic: return "A";
        case Family::Industrial: return "I";
        case Family::Gx: return "G";
        case Family::Legacy: return "";
    }
    return "";
}

char accessChar(Access a) { return a == Access::Write ? '!' : '?'; }

template<typename T>
Result<T> fail(std::string message, std::size_t offset = 0) {
    Result<T> r;
    r.error.message = std::move(message);
    r.error.offset = offset;
    return r;
}

template<typename T>
Result<T> okResult(T value) {
    Result<T> r;
    r.value = std::move(value);
    return r;
}

/// Appends a node and its subtree to the header token list, materialising the
/// LX02 terminator after every block.
void flattenHeader(const std::vector<Node>& nodes, std::vector<std::string>& out) {
    for (const Node& n : nodes) {
        out.push_back(n.token.str());
        if (n.token.isBlock()) {
            flattenHeader(n.children, out);
            if (n.explicit_close) out.push_back(blockClose().str());
        }
    }
}

bool parseInt64(std::string_view s, std::int64_t& out) {
    if (s.empty()) return false;
    bool neg = false;
    std::size_t i = 0;
    if (s[0] == '-' || s[0] == '+') {
        neg = s[0] == '-';
        i = 1;
        if (s.size() == 1) return false;
    }
    std::int64_t v = 0;
    for (; i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
        if (v > (INT64_MAX - 9) / 10) return false;
        v = v * 10 + (s[i] - '0');
    }
    out = neg ? -v : v;
    return true;
}

/// Converts one wire field into a typed value according to its token.
bool decodeField(const Token& token, std::string_view field, char dim_sep, Value& out, std::string& error) {
    auto type = token.type();
    if (!type) {
        error = "unknown data type for token " + token.str();
        return false;
    }
    switch (*type) {
        case DataType::Word: {
            std::int64_t v = 0;
            if (!parseInt64(field, v)) {
                error = "field for " + token.str() + " is not an integer: " + std::string(field);
                return false;
            }
            if (v < kWordMin || v > kWordMax) {
                error = "value " + std::to_string(v) + " for " + token.str() + " is outside the 16-bit range";
                return false;
            }
            out = static_cast<std::int16_t>(v);
            return true;
        }
        case DataType::Long: {
            std::int64_t v = 0;
            if (!parseInt64(field, v)) {
                error = "field for " + token.str() + " is not an integer: " + std::string(field);
                return false;
            }
            if (v < INT32_MIN || v > INT32_MAX) {
                error = "value " + std::to_string(v) + " for " + token.str() + " is outside the 32-bit range";
                return false;
            }
            out = static_cast<std::int32_t>(v);
            return true;
        }
        case DataType::Dimension: {
            auto d = Dimension::parse(field, dim_sep);
            if (!d) {
                error = "malformed dimensional value for " + token.str() + ": " + std::string(field);
                return false;
            }
            out = *d;
            return true;
        }
        case DataType::Text: {
            auto text = unescapeText(field);
            if (!text) {
                error = "malformed escape sequence in text for " + token.str();
                return false;
            }
            out = *text;
            return true;
        }
        case DataType::Command:
        case DataType::Block: out = std::monostate{}; return true;
    }
    error = "unhandled data type";
    return false;
}

/// Builds the node tree from a flat token list, nesting blocks and consuming
/// their LX02 terminators.
/// Builds the node tree from a flat token list.
///
/// `closed` reports whether the current level ended on an explicit LX02. A
/// block that simply runs to the end of the header is accepted: the reference
/// shows that form in its own worked examples.
bool buildTree(const std::vector<Token>& tokens, std::size_t& pos, std::vector<Node>& out, bool inside_block,
               std::string& error, bool* closed = nullptr) {
    if (closed) *closed = false;
    while (pos < tokens.size()) {
        const Token& t = tokens[pos];
        if (t.isBlockClose()) {
            if (!inside_block) {
                error = "LX02 without a matching block command";
                return false;
            }
            ++pos;
            if (closed) *closed = true;
            return true;
        }
        ++pos;
        Node node(t);
        if (t.isBlock()) {
            bool child_closed = false;
            if (!buildTree(tokens, pos, node.children, true, error, &child_closed)) {
                return false;
            }
            node.explicit_close = child_closed;
        }
        out.push_back(std::move(node));
    }
    return true;
}

}  // namespace

std::vector<std::string_view> splitFields(std::string_view line) {
    std::vector<std::string_view> out;
    std::size_t start = 0;
    while (true) {
        std::size_t sep = line.find('|', start);
        if (sep == std::string_view::npos) {
            out.push_back(line.substr(start));
            break;
        }
        out.push_back(line.substr(start, sep - start));
        start = sep + 1;
    }
    return out;
}

// --- encoding -------------------------------------------------------------

std::string encodeHeader(const Header& header, const EncodeOptions& opts) {
    std::vector<std::string> tokens;
    flattenHeader(header.nodes, tokens);

    std::string out = familyLetter(header.family);
    out += accessChar(header.access);
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        if (i) out += '|';
        out += tokens[i];
    }
    if (opts.crlf) out += "\r\n";
    return out;
}

Result<std::string> encodeRecord(const Header& header, const Record& record, const EncodeOptions& opts) {
    std::vector<Token> tokens = header.payloadTokens();
    if (tokens.size() != record.size()) {
        return fail<std::string>("record has " + std::to_string(record.size()) + " fields but the header declares " +
                                 std::to_string(tokens.size()));
    }

    const char sep = header.dimensionSeparator();
    const bool is_read = header.access == Access::Read;

    // A read request names its subfunctions and carries no data line at all;
    // if every value is absent, the record encodes to the empty string.
    bool any_value = false;
    for (const Value& v : record) {
        if (!isEmpty(v)) {
            any_value = true;
            break;
        }
    }
    if (!any_value && (is_read || record.empty())) {
        std::string empty;
        if (opts.crlf) empty += "\r\n";
        return okResult(std::move(empty));
    }

    std::string out;
    for (std::size_t i = 0; i < record.size(); ++i) {
        if (i) out += '|';
        const Value& v = record[i];
        if (std::holds_alternative<std::string>(v)) {
            out += escapeText(std::get<std::string>(v), opts.escape);
        } else if (isEmpty(v)) {
            return fail<std::string>("no value supplied for " + tokens[i].str(), i);
        } else {
            out += encodeValue(v, sep);
        }
    }
    if (opts.crlf) out += "\r\n";
    return okResult(std::move(out));
}

Result<std::vector<std::string>> encodeLines(const Telegram& telegram, const EncodeOptions& opts) {
    std::vector<std::string> lines;
    lines.push_back(encodeHeader(telegram.header, opts));
    for (std::size_t i = 0; i < telegram.records.size(); ++i) {
        auto line = encodeRecord(telegram.header, telegram.records[i], opts);
        if (!line) {
            return fail<std::vector<std::string>>("record " + std::to_string(i) + ": " + line.error.message, i);
        }
        if (!line->empty()) lines.push_back(*line);
    }
    return okResult(std::move(lines));
}

Result<std::string> encodeOneLine(const Telegram& telegram, const EncodeOptions& opts) {
    // A read telegram is allowed to carry no record at all: `A?ST8D` is the
    // whole thing, and demanding a record of empty values to represent "no
    // data" was a needless obstacle -- a header parsed straight off a console
    // line has none, and that is a legitimate read request.
    static const Record kNoFields;
    const bool read_without_record = telegram.records.empty() && telegram.header.access == Access::Read;

    if (telegram.records.size() != 1 && !read_without_record) {
        return fail<std::string>("the interleaved form carries exactly one record, got " +
                                 std::to_string(telegram.records.size()));
    }

    const Header& header = telegram.header;
    const Record& record = read_without_record ? kNoFields : telegram.records.front();
    const char sep = header.dimensionSeparator();

    std::vector<std::string> tokens;
    flattenHeader(header.nodes, tokens);

    std::string out = familyLetter(header.family);
    out += accessChar(header.access);

    std::size_t field = 0;
    bool first = true;
    for (const std::string& token_text : tokens) {
        auto token = Token::parse(token_text);
        if (!first) out += '|';
        first = false;
        out += token_text;

        if (!token || token->arity() == 0) continue;
        if (field >= record.size()) {
            // On a read that is not an error, it is the normal shape: the
            // token names the subfunction and the device supplies the value.
            if (header.access == Access::Read) continue;
            return fail<std::string>("record is shorter than the header", field);
        }
        const Value& v = record[field++];
        // Read requests interleave nothing: the token stands alone.
        if (isEmpty(v) && header.access == Access::Read) continue;
        out += '|';
        if (std::holds_alternative<std::string>(v)) {
            out += escapeText(std::get<std::string>(v), opts.escape);
        } else if (isEmpty(v)) {
            return fail<std::string>("no value supplied for " + token_text, field - 1);
        } else {
            out += encodeValue(v, sep);
        }
    }
    if (field != record.size()) {
        return fail<std::string>("record has more fields than the header", field);
    }
    if (opts.crlf) out += "\r\n";
    return okResult(std::move(out));
}

// --- parsing --------------------------------------------------------------

namespace {

/// Reads the "A!" style prefix and returns the offset of the first token.
bool parsePrefix(std::string_view line, Family& family, Access& access, std::size_t& offset, std::string& error) {
    if (line.empty()) {
        error = "empty header line";
        return false;
    }
    std::size_t i = 0;
    switch (line[0]) {
        case 'A':
            family = Family::Automatic;
            i = 1;
            break;
        case 'I':
            family = Family::Industrial;
            i = 1;
            break;
        case 'G':
            family = Family::Gx;
            i = 1;
            break;
        case '!':
        case '?':
            family = Family::Legacy;
            i = 0;
            break;
        default: error = "header must start with A, I, G, '!' or '?'"; return false;
    }
    if (i >= line.size()) {
        error = "header prefix is truncated";
        return false;
    }
    if (line[i] == '!') {
        access = Access::Write;
    } else if (line[i] == '?') {
        access = Access::Read;
    } else {
        error = "header prefix must be followed by '!' or '?'";
        return false;
    }
    offset = i + 1;
    return true;
}

std::string_view trimEol(std::string_view line) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
        line.remove_suffix(1);
    }
    return line;
}

}  // namespace

Result<Header> parseHeader(std::string_view line) {
    line = trimEol(line);

    Header header;
    std::size_t offset = 0;
    std::string error;
    if (!parsePrefix(line, header.family, header.access, offset, error)) {
        return fail<Header>(std::move(error));
    }

    std::string_view body = line.substr(offset);
    if (body.empty()) return okResult(std::move(header));

    std::vector<Token> tokens;
    std::size_t index = 0;
    for (std::string_view field : splitFields(body)) {
        auto token = Token::parse(field);
        if (!token) {
            return fail<Header>("malformed token '" + std::string(field) + "'", index);
        }
        tokens.push_back(*token);
        ++index;
    }

    std::size_t pos = 0;
    if (!buildTree(tokens, pos, header.nodes, false, error)) {
        return fail<Header>(std::move(error), pos);
    }
    return okResult(std::move(header));
}

Result<Record> parseRecord(const Header& header, std::string_view line) {
    line = trimEol(line);

    std::vector<Token> tokens = header.payloadTokens();
    std::vector<std::string_view> fields = splitFields(line);

    // A header with no payload tokens pairs with an empty data line; splitting
    // "" yields one empty field, so normalise that case.
    if (tokens.empty() && fields.size() == 1 && fields[0].empty()) {
        return okResult(Record{});
    }
    if (fields.size() != tokens.size()) {
        return fail<Record>("data line has " + std::to_string(fields.size()) + " fields but the header declares " +
                            std::to_string(tokens.size()));
    }

    const char sep = header.dimensionSeparator();
    Record record;
    record.reserve(tokens.size());
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        Value v;
        std::string error;
        if (!decodeField(tokens[i], fields[i], sep, v, error)) {
            return fail<Record>(std::move(error), i);
        }
        record.push_back(std::move(v));
    }
    return okResult(std::move(record));
}

Result<Telegram> parseLines(const std::vector<std::string>& lines) {
    if (lines.empty()) return fail<Telegram>("no lines supplied");

    auto header = parseHeader(lines.front());
    if (!header) return fail<Telegram>(header.error.message, header.error.offset);

    Telegram telegram;
    telegram.header = *header;
    for (std::size_t i = 1; i < lines.size(); ++i) {
        std::string_view line = trimEol(lines[i]);
        if (line.empty() && telegram.header.payloadArity() > 0) continue;
        auto record = parseRecord(telegram.header, line);
        if (!record) {
            return fail<Telegram>("line " + std::to_string(i) + ": " + record.error.message, i);
        }
        telegram.records.push_back(*record);
    }
    return okResult(std::move(telegram));
}

Result<Telegram> parseOneLine(std::string_view line) {
    line = trimEol(line);

    Header header;
    std::size_t offset = 0;
    std::string error;
    if (!parsePrefix(line, header.family, header.access, offset, error)) {
        return fail<Telegram>(std::move(error));
    }

    std::vector<std::string_view> fields = splitFields(line.substr(offset));
    const char sep = header.dimensionSeparator();

    std::vector<Token> tokens;
    Record record;
    for (std::size_t i = 0; i < fields.size();) {
        auto token = Token::parse(fields[i]);
        if (!token) {
            return fail<Telegram>(
                "expected a token at field " + std::to_string(i) + ", got '" + std::string(fields[i]) + "'", i);
        }
        tokens.push_back(*token);
        ++i;

        if (token->arity() == 0) continue;
        if (i >= fields.size()) {
            return fail<Telegram>("token " + token->str() + " has no value in the interleaved form", i);
        }
        Value v;
        if (!decodeField(*token, fields[i], sep, v, error)) {
            return fail<Telegram>(std::move(error), i);
        }
        record.push_back(std::move(v));
        ++i;
    }

    std::size_t pos = 0;
    if (!buildTree(tokens, pos, header.nodes, false, error)) {
        return fail<Telegram>(std::move(error), pos);
    }

    Telegram telegram;
    telegram.header = std::move(header);
    telegram.records.push_back(std::move(record));
    return okResult(std::move(telegram));
}

}  // namespace gxnet
