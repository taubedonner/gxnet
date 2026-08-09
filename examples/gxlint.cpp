// SPDX-License-Identifier: MIT
//
// gxlint - annotate and validate GxNet telegrams read from stdin.
//
// Intended for working through a captured communication log: it expands every
// token to its symbolic name, decodes payload values and reports anything that
// would not be accepted by the target device.
//
//   gxlint [--device 16.40] [--quiet] < capture.txt
//
// Header lines are recognised by their A!/A?/I!/I?/G!/G?/!/? prefix; any line
// that follows a header and is not itself one is treated as a data record.

#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "gxnet/gxnet.hpp"

#include <regex>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

using namespace gxnet::literals;

// Text in a telegram is passed through byte for byte, so a label exported with
// Cyrillic field content arrives here as UTF-8 and leaves as UTF-8. A console
// left on its default OEM code page renders those bytes as something else
// entirely, which looks like a decoding bug in this tool and is not one.
//
// The previous code page goes back on the way out: this is one command in a
// shell session, not the owner of its settings. Everywhere but Windows the
// class is empty and the console is left alone.
class Utf8Console {
public:
    Utf8Console() {
#ifdef _WIN32
        previous_ = GetConsoleOutputCP();
        SetConsoleOutputCP(CP_UTF8);
#endif
    }

    ~Utf8Console() {
#ifdef _WIN32
        if (previous_ != 0) SetConsoleOutputCP(previous_);
#endif
    }

    Utf8Console(const Utf8Console&) = delete;
    Utf8Console& operator=(const Utf8Console&) = delete;

#ifdef _WIN32
private:
    UINT previous_ = 0;
#endif
};

// Communication log lines look like:
//   04.08.2026  06:56:08::216 A-> 90770102D1060BB8
struct HexLine {
    std::string hex;     ///< empty when the line is not one
    std::string prefix;  ///< what precedes the payload: timestamp and direction
};

// The prefix is taken from where the match starts rather than by subtracting
// the payload's length from the line's, which silently keeps part of the
// payload when the line ends in whitespace.
HexLine extractHex(const std::string& line) {
    static const std::regex re(R"((?:->|<-)\s*([0-9A-Fa-f]{8,})\s*$)");
    std::smatch m;
    if (!std::regex_search(line, m, re)) return {};
    return {m[1].str(), line.substr(0, static_cast<std::size_t>(m.position(1)))};
}

bool looksLikeHeader(const std::string& line) {
    if (line.empty()) return false;
    if (line[0] == '!' || line[0] == '?') return true;
    if (line.size() < 2) return false;
    const bool family = line[0] == 'A' || line[0] == 'I' || line[0] == 'G';
    return family && (line[1] == '!' || line[1] == '?');
}

// Set by --meaning. Off by default: a description per line triples the height
// of a listing, and most of the time the name is what is being looked for.
bool g_meaning = false;

std::string describe(const gxnet::Token& token) {
    auto info = gxnet::Registry::builtin().find(token);
    std::string out = token.str();
    if (info) {
        out += "  " + std::string(info->name);
        if (info->since.valid()) out += "  [since " + info->since.str() + "]";
        if (info->reserved) out += "  [reserved]";
    } else {
        out += "  (not in the bundled table)";
    }
    if (g_meaning) {
        if (const auto meaning = gxnet::tokenMeaning(token)) {
            out += "\n        " + std::string(*meaning);
        }
    }
    return out;
}

// The integer a value carries, whatever width it was parsed at.
std::optional<std::int32_t> asNumber(const gxnet::Value& value) {
    if (const auto* w = std::get_if<std::int16_t>(&value)) return *w;
    if (const auto* l = std::get_if<std::int32_t>(&value)) return *l;
    return std::nullopt;
}

// LGW_UFKENN says which subfunction supplies a field's content, as that
// subfunction's own numeric code. Without this a label export is a page of bare
// numbers where the interesting column is which field prints what.
std::string ufkennText(std::int32_t number) {
    // The cast to unsigned is the point: a class byte above 0x7F makes the
    // word negative.
    const auto raw = static_cast<std::uint16_t>(number);
    const auto referenced = gxnet::Token::fromClassCode(static_cast<std::uint8_t>(raw >> 8),
                                                        static_cast<std::uint8_t>(raw & 0xFF));
    if (!referenced) return {};

    std::string out = referenced->str();
    if (auto info = gxnet::Registry::builtin().find(*referenced)) {
        out += " ";
        out += std::string(info->name);
    }
    return out;
}

// What the number means, where the reference names it. Empty when the
// semantics table was never generated, which is a fresh checkout's normal
// state.
std::string annotate(const gxnet::Token& token, const gxnet::Value& value) {
    const auto number = asNumber(value);
    if (!number) return {};

    if (token == "LW02"_tok) return ufkennText(*number);

    if (const auto named = gxnet::tokenValueName(token, *number)) return std::string(*named);
    return {};
}

std::string renderValue(const gxnet::Value& v) {
    if (gxnet::isEmpty(v)) return "-";
    if (std::holds_alternative<gxnet::Dimension>(v)) {
        const auto& d = std::get<gxnet::Dimension>(v);
        return d.encode() + "  (= " + std::to_string(d.toDouble()) + " " + d.unit + ")";
    }
    return gxnet::encodeValue(v);
}

// Prints the node tree with each leaf's value beside its name.
//
// The three input shapes differ only in where the value is kept: the binary and
// interleaved forms fill the node, header-plus-data leaves the node empty and
// puts the values in a record, in header order. Taking the node first and
// falling back to the record covers all three, and `field` counts payload
// tokens across the whole tree because that is what a record is indexed by.
void printNodes(const std::vector<gxnet::Node>& nodes, int depth, const gxnet::Record* record, std::size_t& field) {
    for (const gxnet::Node& n : nodes) {
        std::cout << "  " << std::string(depth * 2, ' ') << describe(n.token);
        if (n.token.arity() > 0) {
            gxnet::Value value = n.value;
            if (gxnet::isEmpty(value) && record != nullptr && field < record->size()) {
                value = (*record)[field];
            }
            ++field;
            // A read carries no values at all, and printing a placeholder for
            // every one of its tokens buries the tokens.
            if (!gxnet::isEmpty(value)) {
                std::cout << " = " << renderValue(value);
                const std::string note = annotate(n.token, value);
                if (!note.empty()) std::cout << "   (" << note << ")";
            }
        }
        std::cout << "\n";
        if (!n.children.empty()) printNodes(n.children, depth + 1, record, field);
    }
}

void printNodes(const std::vector<gxnet::Node>& nodes, const gxnet::Record* record = nullptr) {
    std::size_t field = 0;
    printNodes(nodes, 0, record, field);
}

}  // namespace

int main(int argc, char** argv) {
    // Before anything is printed, --help included.
    const Utf8Console utf8_console;

    gxnet::ValidateOptions opts;
    bool quiet = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--device" && i + 1 < argc) {
            if (auto v = gxnet::Version::parse(argv[++i])) {
                opts.device_version = *v;
            } else {
                std::cerr << "gxlint: cannot parse device version\n";
                return 2;
            }
        } else if (arg == "--quiet") {
            quiet = true;
        } else if (arg == "--meaning") {
            g_meaning = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout <<
                R"(gxlint - read a GxNet capture and say what is in it.

usage: gxlint [--device MM.mm] [--quiet] [--meaning] < capture

Reads stdin and writes an annotated listing: every token expanded to its
symbolic name and the release it appeared in, each value printed beside the
token it belongs to, and anything the target device would refuse reported as
an error.

LGW_UFKENN values are resolved back to the subfunction they name, which is what
makes a label layout readable: a field carrying 12800 is a field printing
PSL_STCK_SUM.

Where the reference names a value, the name is shown beside the number. That
table is built from the vendor manual and is not part of a checkout; without
it the numbers are simply left bare.

Two input shapes, mixed freely in one file:

  textual     a header line and, optionally, the data line under it. Device
              exports use this shape, including layouts exported from BRAIN2:
                A?ST8D
                A!ST8D|16.40.0002

  binary      lines from the server's own communication log, which carry the
              compact form as hexadecimal. Recognised by the -> or <- marker
              and the hex that follows it, so the timestamps around them do
              not have to be stripped:
                06.08.2026  13:09:00::339 A<- D077020141009660

The second is why this exists. `Bizerba._connect.BRAIN.Server_<date>.log` and
`CommU_<device>_<date>.commlog` are written in that form, and they are the only
record of what actually reached the device, including, when a frame is missing,
what did not.

options
  --device MM.mm   firmware to validate against, e.g. --device 16.40. Without
                   it every subfunction is accepted whatever release it needs.
  --quiet          errors only, no listing.
  --meaning        also print what each subfunction is for. Needs the
                   semantics table; without it nothing is added.

exit status is 0 when nothing was reported, 1 when something was.

examples
  gxlint < examples/sample.commlog
  gxlint --device 16.40 --quiet < CommU_GLM_2026-08-06.commlog
)";
            return 0;
        } else {
            std::cerr << "gxlint: unknown option " << arg << "\n";
            return 2;
        }
    }

    std::string line;
    gxnet::Header current;
    bool have_header = false;
    int errors = 0;
    int telegrams = 0;

    // A header's tree is worth printing once, with the values in it. Whether
    // there are any depends on the line after it, which has not been read yet,
    // so the tree waits: a data record prints it with the values filled in, and
    // anything else prints it bare.
    bool tree_shown = false;
    const auto flushHeader = [&] {
        if (!have_header || tree_shown) return;
        tree_shown = true;
        if (quiet) return;
        printNodes(current.nodes);
        std::cout << "\n";
    };

    while (std::getline(std::cin, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
            line.pop_back();
        }
        if (line.empty()) continue;

        // Comments, so a capture can be annotated and still be readable by
        // this. A log pulled off a server has none; one prepared for somebody
        // else usually wants them, and the alternative was every explanatory
        // line being reported as a data record without a header.
        const std::size_t first = line.find_first_not_of(" \t");
        if (first != std::string::npos && (line[first] == ';' || line[first] == '#')) {
            if (!quiet) std::cout << line << "\n";
            continue;
        }

        // The server's own notes, which it marks `--` where a frame would have
        // `->` or `<-`: connection opened, licence sent, receiver started. A
        // real communication log is full of them and none is a telegram, so
        // parsing them produced one spurious error per line and buried the real
        // ones. Shown, because the timestamps around a gap are the point.
        if (line.find(" -- ") != std::string::npos && extractHex(line).hex.empty()) {
            if (!quiet) std::cout << line << "\n";
            continue;
        }

        // A communication log line carries the compact binary form.
        const HexLine frame_line = extractHex(line);
        if (!frame_line.hex.empty()) {
            flushHeader();
            const std::string& hex = frame_line.hex;
            gxnet::binary::Options bopts;
            bopts.units = gxnet::binary::inferredUnitTable();
            auto frame = gxnet::binary::parseHex(hex, bopts);
            std::cout << frame_line.prefix << "[" << hex.size() / 2 << " bytes]\n";
            if (!frame) {
                std::cout << "  ! binary parse error: " << frame.error.message << "\n\n";
                ++errors;
                continue;
            }
            ++telegrams;
            char hdr[32];
            std::snprintf(hdr, sizeof(hdr), "%02X%02X%02X%02X", frame->header[0], frame->header[1], frame->header[2],
                          frame->header[3]);
            std::cout << "  frame " << hdr << "\n";
            if (!quiet) printNodes(frame->nodes);
            gxnet::Telegram t = gxnet::binary::toTelegram(*frame);
            for (const gxnet::Diagnostic& d : validate(t, opts)) {
                std::cout << "  " << d.str() << "\n";
                if (d.severity == gxnet::Severity::Error) ++errors;
            }
            std::cout << "\n";
            have_header = false;
            continue;
        }

        if (looksLikeHeader(line)) {
            flushHeader();
            auto header = gxnet::parseHeader(line);
            if (!header) {
                // Not a plain header: most single commands are sent in the
                // interleaved form, where values sit between the tokens.
                auto one = gxnet::parseOneLine(line);
                if (!one) {
                    std::cout << line << "\n  ! parse error: " << header.error.message
                              << "\n  ! not the interleaved form either: " << one.error.message << "\n\n";
                    ++errors;
                    have_header = false;
                    continue;
                }

                ++telegrams;
                have_header = false;  // values were inline; no data line follows
                std::cout << line << "  (interleaved form)\n";

                if (!quiet) printNodes(one->header.nodes, &one->records.front());
                for (const gxnet::Diagnostic& d : validate(*one, opts)) {
                    std::cout << "  " << d.str() << "\n";
                    if (d.severity == gxnet::Severity::Error) ++errors;
                }
                std::cout << "\n";
                continue;
            }
            current = *header;
            have_header = true;
            tree_shown = false;
            ++telegrams;

            // No blank line here: the tree has not been printed yet, and the
            // separator belongs at the end of the telegram rather than the
            // middle of it.
            std::cout << line << "\n";
            for (const gxnet::Diagnostic& d : validate(current, opts)) {
                std::cout << "  " << d.str() << "\n";
                if (d.severity == gxnet::Severity::Error) ++errors;
            }
            continue;
        }

        if (!have_header) {
            std::cout << line << "\n  ! data line without a preceding header\n\n";
            ++errors;
            continue;
        }

        auto record = gxnet::parseRecord(current, line);
        if (!record) {
            std::cout << "  data: " << line << "\n  ! " << record.error.message << "\n\n";
            ++errors;
            continue;
        }

        std::cout << "  data: " << line << "\n";
        tree_shown = true;
        if (!quiet) printNodes(current.nodes, &*record);
        std::cout << "\n";
    }

    // A header on the last line of the file has no record after it, and its
    // tree is still worth printing.
    flushHeader();

    std::cerr << telegrams << " telegram(s), " << errors << " error(s)\n";
    return errors == 0 ? 0 : 1;
}
