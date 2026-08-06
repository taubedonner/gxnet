// SPDX-License-Identifier: MIT
#include "gxnet/binary.hpp"

#include <cstring>

namespace gxnet {
namespace binary {
namespace {

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

std::uint16_t readU16(const std::uint8_t* p) { return static_cast<std::uint16_t>((p[0] << 8) | p[1]); }

std::int32_t readI32(const std::uint8_t* p) {
    std::uint32_t v = (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
                      (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
    return static_cast<std::int32_t>(v);
}

void writeU16(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v >> 8));
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
}

void writeI32(std::vector<std::uint8_t>& out, std::int32_t v) {
    auto u = static_cast<std::uint32_t>(v);
    out.push_back(static_cast<std::uint8_t>(u >> 24));
    out.push_back(static_cast<std::uint8_t>(u >> 16));
    out.push_back(static_cast<std::uint8_t>(u >> 8));
    out.push_back(static_cast<std::uint8_t>(u));
}

std::string unitName(int code, const UnitTable& units) {
    for (const auto& e : units) {
        if (e.first == code) return e.second;
    }
    return "#" + std::to_string(code);
}

bool unitCode(const std::string& name, const UnitTable& units, int& out) {
    for (const auto& e : units) {
        if (e.second == name) {
            out = e.first;
            return true;
        }
    }
    if (name.size() >= 2 && name[0] == '#') {
        int v = 0;
        for (std::size_t i = 1; i < name.size(); ++i) {
            if (name[i] < '0' || name[i] > '9') return false;
            v = v * 10 + (name[i] - '0');
        }
        out = v;
        return true;
    }
    return false;
}

/// Decodes a run of tokens covering exactly `size` bytes.
bool decodeNodes(const std::uint8_t* data, std::size_t size, const Options& opts, std::vector<Node>& out,
                 std::string& error, std::size_t base_offset) {
    std::size_t pos = 0;
    while (pos < size) {
        if (pos + 2 > size) {
            error = "truncated token header";
            return false;
        }
        const std::uint8_t cls = data[pos];
        const std::uint8_t idx = data[pos + 1];
        pos += 2;

        const int group_nibble = (cls >> 4) & 0xF;
        const int type_nibble = cls & 0xF;

        // Reconstruct the textual token from the class code.
        static const char* kGroupLetters = "GA\0P\0LDEBSWXV\0M\0";
        // index:                           0123456789ABCDEF
        // 0:G 1:A 3:P 4:L 5:D 6:E 7:S 8:B 9:W A:X B:V D:M
        static const char kGroupByNibble[16] = {'G', 'A', 0, 'P', 'L', 'D', 'E', 'S', 'B', 'W', 'X', 'V', 0, 'M', 0, 0};
        (void)kGroupLetters;
        char group = kGroupByNibble[group_nibble];

        char type = 0;
        switch (type_nibble) {
            case 0: type = 'X'; break;
            case 1: type = 'W'; break;
            case 2: type = 'L'; break;
            case 3: type = 'D'; break;
            case 6: type = 'V'; break;
            case 7: type = 'T'; break;
            default: break;
        }
        if (type == 0) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "unknown data type nibble 0x%X in class byte 0x%02X", type_nibble, cls);
            error = buf;
            error += " at offset " + std::to_string(base_offset + pos - 2);
            return false;
        }
        if (group == 0) {
            // Keep going: an unmapped group nibble is still decodable because
            // the payload length depends only on the type.
            group = 'Z';
        }

        Node node(Token(group, type, idx));

        switch (type) {
            case 'X': break;
            case 'W': {
                if (pos + 2 > size) {
                    error = "truncated word payload";
                    return false;
                }
                node.value = static_cast<std::int16_t>(readU16(data + pos));
                pos += 2;
                break;
            }
            case 'L': {
                if (pos + 4 > size) {
                    error = "truncated long payload";
                    return false;
                }
                node.value = readI32(data + pos);
                pos += 4;
                break;
            }
            case 'D': {
                if (pos + 6 > size) {
                    error = "truncated dimension payload";
                    return false;
                }
                int unit = 0, exp = 0;
                unpackUnitField(readU16(data + pos), unit, exp);
                node.value = Dimension(unitName(unit, opts.units), exp, readI32(data + pos + 2));
                pos += 6;
                break;
            }
            case 'T': {
                if (pos + 2 > size) {
                    error = "truncated text length";
                    return false;
                }
                const std::size_t len = readU16(data + pos);
                pos += 2;
                if (pos + len > size) {
                    error = "truncated text payload";
                    return false;
                }
                node.value = std::string(reinterpret_cast<const char*>(data + pos), len);
                // Text payloads are padded to an even byte count; the pad byte
                // is not counted in the length field.
                pos += len + (len & 1u);
                break;
            }
            case 'V': {
                if (pos + 2 > size) {
                    error = "truncated block length";
                    return false;
                }
                const std::size_t len = readU16(data + pos);
                pos += 2;
                if (pos + len > size) {
                    error = "block length exceeds the frame";
                    return false;
                }

                std::size_t inner = len;
                if (opts.require_block_terminator) {
                    if (inner < 2 || data[pos + inner - 2] != 0x40 || data[pos + inner - 1] != 0x02) {
                        error = "block is not terminated by LX02";
                        return false;
                    }
                    inner -= 2;  // the terminator is implicit in the tree
                } else {
                    node.explicit_close = false;
                }
                if (!decodeNodes(data + pos, inner, opts, node.children, error, base_offset + pos)) {
                    return false;
                }
                pos += len;
                break;
            }
            default: break;
        }
        out.push_back(std::move(node));
    }
    return true;
}

bool encodeNodes(const std::vector<Node>& nodes, const Options& opts, std::vector<std::uint8_t>& out,
                 std::string& error) {
    for (const Node& n : nodes) {
        auto cls = n.token.classCode();
        if (!cls) {
            error = "token " + n.token.str() + " has no class code and cannot be encoded in binary form";
            return false;
        }
        out.push_back(*cls);
        out.push_back(n.token.index);

        auto type = n.token.type();
        switch (*type) {
            case DataType::Command: break;
            case DataType::Word: {
                if (!std::holds_alternative<std::int16_t>(n.value)) {
                    error = "missing word value for " + n.token.str();
                    return false;
                }
                writeU16(out, static_cast<std::uint16_t>(std::get<std::int16_t>(n.value)));
                break;
            }
            case DataType::Long: {
                if (!std::holds_alternative<std::int32_t>(n.value)) {
                    error = "missing long value for " + n.token.str();
                    return false;
                }
                writeI32(out, std::get<std::int32_t>(n.value));
                break;
            }
            case DataType::Dimension: {
                if (!std::holds_alternative<Dimension>(n.value)) {
                    error = "missing dimensional value for " + n.token.str();
                    return false;
                }
                const Dimension& d = std::get<Dimension>(n.value);
                int code = 0;
                if (!unitCode(d.unit, opts.units, code)) {
                    error = "no numeric unit code for '" + d.unit + "' on " + n.token.str();
                    return false;
                }
                writeU16(out, packUnitField(code, d.exponent));
                if (d.mantissa < INT32_MIN || d.mantissa > INT32_MAX) {
                    error = "mantissa out of range for " + n.token.str();
                    return false;
                }
                writeI32(out, static_cast<std::int32_t>(d.mantissa));
                break;
            }
            case DataType::Text: {
                if (!std::holds_alternative<std::string>(n.value)) {
                    error = "missing text value for " + n.token.str();
                    return false;
                }
                const std::string& s = std::get<std::string>(n.value);
                if (s.size() > 0xFFFF) {
                    error = "text too long for " + n.token.str();
                    return false;
                }
                writeU16(out, static_cast<std::uint16_t>(s.size()));
                out.insert(out.end(), s.begin(), s.end());
                if (s.size() & 1u) out.push_back(0x00);  // even alignment
                break;
            }
            case DataType::Block: {
                const std::size_t len_pos = out.size();
                writeU16(out, 0);  // patched once the body length is known
                if (!encodeNodes(n.children, opts, out, error)) return false;
                if (n.explicit_close) {
                    out.push_back(0x40);
                    out.push_back(0x02);
                }
                const std::size_t body = out.size() - len_pos - 2;
                if (body > 0xFFFF) {
                    error = "block too large for " + n.token.str();
                    return false;
                }
                out[len_pos] = static_cast<std::uint8_t>(body >> 8);
                out[len_pos + 1] = static_cast<std::uint8_t>(body & 0xFF);
                break;
            }
        }
    }
    return true;
}

}  // namespace

const UnitTable& inferredUnitTable() {
    static const UnitTable table = {{0, "EUR"}, {3, "KG"}};
    return table;
}

std::uint16_t packUnitField(int unit_code, int exponent) {
    const auto exp6 = static_cast<std::uint16_t>(exponent & 0x3F);
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(unit_code) << 6) | exp6);
}

void unpackUnitField(std::uint16_t field, int& unit_code, int& exponent) {
    unit_code = field >> 6;
    int exp = field & 0x3F;
    if (exp >= 32) exp -= 64;  // six-bit two's complement
    exponent = exp;
}

Result<Frame> parse(const std::uint8_t* data, std::size_t size, const Options& opts) {
    Frame frame;
    std::size_t pos = 0;
    if (opts.frame_header) {
        if (size < 4) return fail<Frame>("frame is shorter than its header");
        std::memcpy(frame.header.data(), data, 4);
        frame.has_header = true;
        pos = 4;
    } else {
        frame.has_header = false;
    }

    std::string error;
    if (!decodeNodes(data + pos, size - pos, opts, frame.nodes, error, pos)) {
        return fail<Frame>(std::move(error), pos);
    }
    return okResult(std::move(frame));
}

Result<Frame> parse(const std::vector<std::uint8_t>& data, const Options& opts) {
    return parse(data.data(), data.size(), opts);
}

Result<std::vector<std::uint8_t>> encode(const Frame& frame, const Options& opts) {
    std::vector<std::uint8_t> out;
    if (opts.frame_header && frame.has_header) {
        out.insert(out.end(), frame.header.begin(), frame.header.end());
    }
    std::string error;
    if (!encodeNodes(frame.nodes, opts, out, error)) {
        return fail<std::vector<std::uint8_t>>(std::move(error));
    }
    return okResult(std::move(out));
}

std::vector<std::uint8_t> fromHex(std::string_view hex) {
    std::vector<std::uint8_t> out;
    int hi = -1;
    for (char c : hex) {
        int v;
        if (c >= '0' && c <= '9')
            v = c - '0';
        else if (c >= 'A' && c <= 'F')
            v = c - 'A' + 10;
        else if (c >= 'a' && c <= 'f')
            v = c - 'a' + 10;
        else
            continue;  // skip whitespace and separators
        if (hi < 0) {
            hi = v;
        } else {
            out.push_back(static_cast<std::uint8_t>(hi * 16 + v));
            hi = -1;
        }
    }
    return out;
}

std::string toHex(const std::vector<std::uint8_t>& bytes) {
    static const char* kHex = "0123456789ABCDEF";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (std::uint8_t b : bytes) {
        out += kHex[(b >> 4) & 0xF];
        out += kHex[b & 0xF];
    }
    return out;
}

Result<Frame> parseHex(std::string_view hex, const Options& opts) { return parse(fromHex(hex), opts); }

Telegram toTelegram(const Frame& frame, Family family, Access access) {
    Telegram t;
    t.header.family = family;
    t.header.access = access;
    t.header.nodes = frame.nodes;

    Record record;
    forEachNode(t.header.nodes, [&record](const Node& n) {
        if (n.token.arity() > 0) record.push_back(n.value);
    });
    t.records.push_back(std::move(record));
    return t;
}

}  // namespace binary
}  // namespace gxnet
