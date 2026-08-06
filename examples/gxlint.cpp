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

#include <iostream>
#include <string>
#include <vector>

#include "gxnet/gxnet.hpp"

#include <regex>

namespace {

// Communication log lines look like:
//   04.08.2026  06:56:08::216 A-> 90770102D1060BB8
// Returns the hexadecimal payload, or an empty string when the line is not one.
std::string extractHex(const std::string& line) {
    static const std::regex re(R"((?:->|<-)\s*([0-9A-Fa-f]{8,})\s*$)");
    std::smatch m;
    if (std::regex_search(line, m, re)) return m[1].str();
    return {};
}

bool looksLikeHeader(const std::string& line) {
    if (line.empty()) return false;
    if (line[0] == '!' || line[0] == '?') return true;
    if (line.size() < 2) return false;
    const bool family = line[0] == 'A' || line[0] == 'I' || line[0] == 'G';
    return family && (line[1] == '!' || line[1] == '?');
}

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
    return out;
}

std::string renderValue(const gxnet::Value& v) {
    if (gxnet::isEmpty(v)) return "-";
    if (std::holds_alternative<gxnet::Dimension>(v)) {
        const auto& d = std::get<gxnet::Dimension>(v);
        return d.encode() + "  (= " + std::to_string(d.toDouble()) + " " + d.unit + ")";
    }
    return gxnet::encodeValue(v);
}

void printTree(const std::vector<gxnet::Node>& nodes, int depth) {
    for (const gxnet::Node& n : nodes) {
        std::cout << "  " << std::string(depth * 2, ' ') << describe(n.token) << "\n";
        if (!n.children.empty()) printTree(n.children, depth + 1);
    }
}

void printBinaryNodes(const std::vector<gxnet::Node>& nodes, int depth) {
    for (const gxnet::Node& n : nodes) {
        std::cout << "  " << std::string(depth * 2, ' ') << describe(n.token);
        if (!gxnet::isEmpty(n.value)) {
            std::cout << " = " << renderValue(n.value);
        }
        std::cout << "\n";
        if (!n.children.empty()) printBinaryNodes(n.children, depth + 1);
    }
}

}  // namespace

int main(int argc, char** argv) {
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
        } else if (arg == "--help" || arg == "-h") {
            std::cout <<
                R"(gxlint - read a GxNet capture and say what is in it.

usage: gxlint [--device MM.mm] [--quiet] < capture

Reads stdin and writes an annotated listing: every token expanded to its
symbolic name and the release it appeared in, payload values decoded, and
anything the target device would refuse reported as an error.

Two input shapes, mixed freely in one file:

  textual     a header line and, optionally, the data line under it
                A?ST8D
                A!ST8D|16.40.0002

  binary      lines from the server's own communication log, which carry the
              compact form as hexadecimal. Recognised by the -> or <- marker
              and the hex that follows it, so the timestamps around them do
              not have to be stripped:
                06.08.2026  13:09:00::339 A<- D077020141009660

The second is why this exists. `Bizerba._connect.BRAIN.Server_<date>.log` and
`CommU_<device>_<date>.commlog` are written in that form, and they are the only
record of what actually reached the device -- including, when a frame is
missing, what did not.

options
  --device MM.mm   firmware to validate against, e.g. --device 16.40. Without
                   it every subfunction is accepted whatever release it needs.
  --quiet          errors only, no listing.

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
        if (line.find(" -- ") != std::string::npos && extractHex(line).empty()) {
            if (!quiet) std::cout << line << "\n";
            continue;
        }

        // A communication log line carries the compact binary form.
        const std::string hex = extractHex(line);
        if (!hex.empty()) {
            gxnet::binary::Options bopts;
            bopts.units = gxnet::binary::inferredUnitTable();
            auto frame = gxnet::binary::parseHex(hex, bopts);
            std::cout << line.substr(0, line.size() - hex.size()) << "[" << hex.size() / 2 << " bytes]\n";
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
            if (!quiet) printBinaryNodes(frame->nodes, 0);
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

                std::vector<gxnet::Token> tokens = one->header.payloadTokens();
                const gxnet::Record& record = one->records.front();
                if (!quiet) {
                    printTree(one->header.nodes, 0);
                    for (std::size_t i = 0; i < record.size(); ++i) {
                        std::cout << "    " << tokens[i].str() << " = " << renderValue(record[i]) << "\n";
                    }
                }
                for (const gxnet::Diagnostic& d : validate(*one, opts)) {
                    std::cout << "  " << d.str() << "\n";
                    if (d.severity == gxnet::Severity::Error) ++errors;
                }
                std::cout << "\n";
                continue;
            }
            current = *header;
            have_header = true;
            ++telegrams;

            std::cout << line << "\n";
            if (!quiet) printTree(current.nodes, 0);

            for (const gxnet::Diagnostic& d : validate(current, opts)) {
                std::cout << "  " << d.str() << "\n";
                if (d.severity == gxnet::Severity::Error) ++errors;
            }
            std::cout << "\n";
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
        if (!quiet) {
            std::vector<gxnet::Token> tokens = current.payloadTokens();
            for (std::size_t i = 0; i < record->size(); ++i) {
                std::cout << "    " << tokens[i].str() << " = " << renderValue((*record)[i]) << "\n";
            }
        }
        std::cout << "\n";
    }

    std::cerr << telegrams << " telegram(s), " << errors << " error(s)\n";
    return errors == 0 ? 0 : 1;
}
