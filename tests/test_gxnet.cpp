// SPDX-License-Identifier: MIT
// Self-contained test suite; no external framework required.
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include "gxnet/gxnet.hpp"

namespace gxnet {
std::ostream& operator<<(std::ostream& os, const Version& v) { return os << v.str(); }
}  // namespace gxnet

namespace {

int g_checks = 0;
int g_failures = 0;
std::string g_group;

void group(const std::string& name) {
    g_group = name;
    std::cout << "\n== " << name << "\n";
}

void check(bool condition, const std::string& what) {
    ++g_checks;
    if (condition) {
        std::cout << "  ok   " << what << "\n";
    } else {
        ++g_failures;
        std::cout << "  FAIL " << what << "\n";
    }
}

template<typename A, typename B>
void checkEq(const A& actual, const B& expected, const std::string& what) {
    ++g_checks;
    if (actual == expected) {
        std::cout << "  ok   " << what << "\n";
    } else {
        ++g_failures;
        std::cout << "  FAIL " << what << "\n";
        std::cout << "         expected: " << expected << "\n";
        std::cout << "         actual:   " << actual << "\n";
    }
}

}  // namespace

using namespace gxnet;

static void testToken() {
    group("token decoding");

    auto gw7d = Token::parse("GW7D");
    check(gw7d.has_value(), "GW7D parses");
    checkEq(gw7d->str(), std::string("GW7D"), "GW7D round-trips");
    checkEq(static_cast<int>(*gw7d->classCode()), 0x01, "GW7D class code is 0x01");
    checkEq(gw7d->arity(), 1, "GW7D carries one payload field");

    auto xx13 = Token::parse("XX13");
    checkEq(static_cast<int>(*xx13->classCode()), 0xA0, "XX13 class code is 0xA0");
    checkEq(xx13->arity(), 0, "XX13 carries no payload");

    auto pv04 = Token::parse("PV04");
    checkEq(static_cast<int>(*pv04->classCode()), 0x36, "PV04 class code is 0x36");
    check(pv04->isBlock(), "PV04 is a block command");
    checkEq(pv04->arity(), 0, "block commands carry no payload field");

    auto lx02 = Token::parse("LX02");
    check(lx02->isBlockClose(), "LX02 is the block terminator");

    check(!Token::parse("QQ01").has_value(), "unknown group letter rejected");
    check(!Token::parse("GZ01").has_value(), "unknown type letter rejected");
    check(!Token::parse("GW7").has_value(), "short token rejected");
    check(!Token::parse("GWZZ").has_value(), "non-hex index rejected");
    check(Token::parse("gw7d").has_value() == false, "lower case group rejected");
}

static void testVersion() {
    group("version handling");

    auto v = Version::parse("16.40");
    check(v.has_value(), "16.40 parses");
    checkEq(v->str(), std::string("16.40"), "16.40 round-trips");

    auto build = Version::parse("16.40.1234");
    check(build.has_value(), "MM.mm.bbbb form parses");
    checkEq(*build, Version(16, 40), "build number ignored");

    check(Version(15, 20) < Version(16, 40), "15.20 precedes 16.40");
    check(Version(16, 40) >= Version(16, 40), "16.40 satisfies itself");
    check(Version(14, 0) < Version(15, 20), "14.00 precedes 15.20");
    check(!Version::parse("sixteen").has_value(), "garbage rejected");
}

static void testDimension() {
    group("dimensional values");

    auto d = Dimension::parse("KG;-3;2995");
    check(d.has_value(), "KG;-3;2995 parses");
    checkEq(d->unit, std::string("KG"), "unit extracted");
    checkEq(d->exponent, -3, "exponent extracted");
    checkEq(static_cast<long long>(d->mantissa), 2995LL, "mantissa extracted");
    check(d->toDouble() > 2.9949 && d->toDouble() < 2.9951, "2.995 kg");
    checkEq(d->encode(), std::string("KG;-3;2995"), "round-trips");

    auto price = Dimension::parse("EUR;-2;1990");
    check(price->toDouble() > 19.899 && price->toDouble() < 19.901, "19.90 EUR");

    auto legacy = Dimension::parse("KG|-3|1064", '|');
    check(legacy.has_value(), "legacy separator supported");
    checkEq(legacy->encode('|'), std::string("KG|-3|1064"), "legacy round-trips");

    check(!Dimension::parse("KG;-3").has_value(), "missing mantissa rejected");
    check(!Dimension::parse("KG;x;100").has_value(), "bad exponent rejected");
}

static void testEscaping() {
    group("escaping");

    checkEq(escapeText("A|B"), std::string("A@7CB"), "pipe escaped");
    checkEq(escapeText("a@b"), std::string("a@40b"), "at sign escaped");
    checkEq(escapeText("x\ny"), std::string("x@0Ay"), "line feed escaped");
    checkEq(escapeText("plain"), std::string("plain"), "plain text untouched");

    const std::string tricky = "line1\nvalue|with@signs\t.";
    auto back = unescapeText(escapeText(tricky));
    check(back.has_value(), "escaped text unescapes");
    checkEq(*back, tricky, "escape round-trips");

    check(!unescapeText("abc@1").has_value(), "truncated escape rejected");
    check(!unescapeText("abc@ZZ").has_value(), "non-hex escape rejected");
    check(needsEscaping("a|b"), "needsEscaping detects pipe");
    check(!needsEscaping("abc"), "needsEscaping passes plain text");
}

static void testUnicodeModes() {
    group("unicode text modes");

    // Both examples are taken verbatim from the vendor manual's conversion
    // helpers, which is the only authoritative statement of the two forms.
    const std::string funf = "f\xC3\xBCnf \xE2\x82\xAC";  // "funf EUR" in UTF-8

    auto codepage = EscapeOptions::forMode(TextMode::CodepageDevice);
    checkEq(escapeText(funf, codepage), std::string("f@C3@BCnf @E2@82@AC"),
            "codepage device: every non-ASCII byte escaped");

    auto unicode = EscapeOptions::forMode(TextMode::UnicodeDevice);
    const std::string at_sign = "characters \xE2\x82\xAC and @";
    checkEq(escapeText(at_sign, unicode), std::string("characters \xE2\x82\xAC and @40"),
            "unicode device: only the escape lead-in encoded");

    // Round-trip in both directions.
    auto back = unescapeText(escapeText(funf, codepage));
    check(back.has_value() && *back == funf, "codepage form round-trips");
    auto back2 = unescapeText(escapeText(funf, unicode));
    check(back2.has_value() && *back2 == funf, "unicode form round-trips");

    // Cyrillic: two bytes per character, so byte length is not text length.
    const std::string ru = "\xD0\x9F\xD0\xB0\xD1\x80\xD1\x82\xD0\xB8\xD1\x8F";  // "Partiya"
    check(isValidUtf8(ru), "cyrillic text is well-formed UTF-8");
    checkEq(*utf8Length(ru), std::size_t(6), "six characters, twelve bytes");
    checkEq(ru.size(), std::size_t(12), "byte count differs from char count");
    checkEq(escapeText(ru, unicode), ru, "cyrillic passes through untouched on a unicode device");

    // A stray CP1251 byte must be caught before it reaches the wire.
    const std::string cp1251 = "\xCF\xE0\xF0";
    check(!isValidUtf8(cp1251), "CP1251 bytes rejected as invalid UTF-8");
    check(!utf8Length(cp1251).has_value(), "length undefined for invalid UTF-8");
    check(!isValidUtf8("\xE2\x82"), "truncated sequence rejected");
    check(!isValidUtf8("\xC0\x80"), "over-long encoding rejected");
}

static void testRegistry() {
    group("registry");

    const Registry& reg = Registry::builtin();
    check(reg.size() > 1500, "registry is populated");

    auto gw7d = reg.find(*Token::parse("GW7D"));
    check(gw7d.has_value(), "GW7D is known");
    checkEq(std::string(gw7d->name), std::string("GGW_UNIQUE_DATEN"), "GW7D name");
    checkEq(gw7d->since, Version(15, 20), "GW7D introduced in 15.20");

    auto xx13 = reg.find(*Token::parse("XX13"));
    checkEq(std::string(xx13->name), std::string("XCX_DELETE_UNIQUE_DATA"), "XX13 name");

    auto sw9b = reg.find(*Token::parse("SW9B"));
    check(sw9b.has_value(), "SW9B is known");
    checkEq(sw9b->since, Version(16, 40), "SW9B introduced in 16.40");

    auto by_name = tokenByName("GGL_PLUNR");
    check(by_name.has_value(), "lookup by symbolic name works");
    checkEq(by_name->str(), std::string("GL19"), "GGL_PLUNR is GL19");
}

static void testCompileTimeRegistry() {
    group("compile-time lookups");

    // Everything below is resolved by the compiler; the run-time checks only
    // confirm that what it resolved to is what we meant. A failure here is a
    // build failure, not a test failure -- which is exactly the point.
    static_assert("GW7D"_tok == Token('G', 'W', 0x7D));
    static_assert("GW7D"_tok.arity() == 1, "word tokens carry one field");
    static_assert("XX13"_tok.arity() == 0, "command tokens carry none");
    static_assert(blockClose().isBlockClose());

    constexpr TokenInfo unique_data = knownToken("GW7D");
    static_assert(unique_data.name == "GGW_UNIQUE_DATEN");
    static_assert(unique_data.since == Version(15, 20));

    // The symbolic spelling resolves to the same entry.
    constexpr TokenInfo by_name = knownToken("GGW_UNIQUE_DATEN");
    static_assert(by_name.token == "GW7D"_tok);

    // A device running 16.40 has everything the unique-data sequence needs.
    constexpr Version kFirmware{16, 40};
    static_assert(knownToken("SW9B").since <= kFirmware, "SRW_UNIQUE_PCK_DATA_READY needs 16.40");
    static_assert(knownToken("ST93").since > kFirmware, "SRT_GX_RELEASE_DATE arrived in 17.00, after this firmware");

    check(true, "compile-time token lookups resolved");
    checkEq(std::string(unique_data.name), std::string("GGW_UNIQUE_DATEN"), "constexpr entry carries the name");
    checkEq(Registry::builtin().size(), std::size_t(1917), "1917 entries in the table");
}

static void testEncodeDocumentedExample() {
    group("documented example: PV04 record set");

    // Header and data taken verbatim from the vendor manual.
    const std::string header_line = "A!PV04|PW02|GW09|GL19|GL1A|GL16|PD00|GL2B|GL2C";
    const std::string data_line = "1|2|4711|0|1|KG;-3;100|20997|1545";

    auto header = parseHeader(header_line);
    check(header.ok(), "manual header parses");
    checkEq(encodeHeader(*header), header_line, "header round-trips");
    checkEq(header->payloadArity(), std::size_t(8), "eight payload fields");
    check(header->nodes.size() == 1, "one root node (the PV04 block)");
    check(!header->nodes[0].explicit_close, "block runs to end of header, as printed in the manual");

    auto record = parseRecord(*header, data_line);
    check(record.ok(), "manual data line parses");
    checkEq(record->size(), std::size_t(8), "eight values decoded");
    check(std::holds_alternative<Dimension>((*record)[5]), "PD00 decoded as a dimension");
    checkEq(std::get<Dimension>((*record)[5]).encode(), std::string("KG;-3;100"), "weight preserved");

    Telegram t;
    t.header = *header;
    t.records.push_back(*record);
    auto encoded = encodeRecord(*header, *record);
    check(encoded.ok(), "record re-encodes");
    checkEq(*encoded, data_line, "data line round-trips");
}

static void testBlocksAndOneLine() {
    group("blocks and the interleaved form");

    auto header = parseHeader("A!PV04|PW02|GL19|GL16|PD00|PD10|LX02");
    check(header.ok(), "explicitly closed block parses");
    check(header->nodes[0].explicit_close, "LX02 recorded as explicit");
    checkEq(encodeHeader(*header), std::string("A!PV04|PW02|GL19|GL16|PD00|PD10|LX02"),
            "explicit terminator round-trips");
    checkEq(header->nodes[0].children.size(), std::size_t(5), "five members inside the block");

    // Uses the R group, which belongs to the Ix family and is absent from the
    // bundled Gx table. It must still parse.
    auto nested = parseHeader("I!LV01|RX01|GT08|LX02");
    check(nested.ok(), "Ix registration header parses");

    auto one = parseOneLine("I!LV01|RX01|GT08|Scale1|LX02");
    check(one.ok(), "interleaved form parses");
    if (one.ok()) {
        checkEq(one->records.front().size(), std::size_t(1), "one payload value");
        checkEq(std::get<std::string>(one->records.front()[0]), std::string("Scale1"), "text value decoded");
        auto reencoded = encodeOneLine(*one);
        check(reencoded.ok(), "interleaved form re-encodes");
        if (reencoded.ok()) {
            checkEq(*reencoded, std::string("I!LV01|RX01|GT08|Scale1|LX02"), "interleaved round-trips");
        }
        // The unknown group is surfaced as a warning, never an error.
        auto diags = validate(*one, ValidateOptions{});
        check(hasNoErrors(diags), "unknown group is not an error");
        bool warned = false;
        for (const Diagnostic& d : diags) {
            if (d.code == "token.unknown_group") warned = true;
        }
        check(warned, "unknown group reported as a warning");
    }

    auto stray = parseHeader("A!GL19|LX02");
    check(!stray.ok(), "stray LX02 rejected");
}

static void testMultiRecord() {
    group("multi-record parsing");

    std::vector<std::string> lines = {
        "A!PV04|PW02|GW09|GL19|GL1A|GL16|PD00|GL2B|GL2C",
        "1|2|4711|0|1|KG;-3;100|20997|1545",
        "1|2|4711|0|2|KG;-3;100|20997|1545",
        "1|2|4711|0|3|KG;-3;100|20997|1545",
    };
    auto t = parseLines(lines);
    check(t.ok(), "header plus three records parses");
    checkEq(t->records.size(), std::size_t(3), "three records");

    auto out = encodeLines(*t);
    check(out.ok(), "re-encodes");
    checkEq(out->size(), std::size_t(4), "header plus three data lines");
    checkEq((*out)[0], lines[0], "header identical");
    checkEq((*out)[2], lines[2], "second record identical");

    // A record with the wrong field count must be rejected, not silently
    // shifted: a shifted record is how wrong data reaches a label.
    auto header = parseHeader(lines[0]);
    auto bad = parseRecord(*header, "1|2|4711");
    check(!bad.ok(), "short record rejected");

    auto surplus = parseRecord(*header, "1|2|4711|0|1|KG;-3;100|20997|1545|99");
    check(!surplus.ok(), "over-long record rejected");
}

static void testBuilder() {
    group("builder");

    Builder b(Family::Automatic, Access::Write);
    b.block("PV04")
        .word("PW02", 7)
        .long_("GL19", 1)
        .long_("GL16", 1)
        .dimension("PD00", Dimension("KG", -3, 1064))
        .end();
    check(b.ok(), "builder reports success");

    Telegram t = b.build();
    checkEq(encodeHeader(t.header), std::string("A!PV04|PW02|GL19|GL16|PD00|LX02"),
            "builder emits the expected header");

    auto data = encodeRecord(t.header, t.records.front());
    check(data.ok(), "builder record encodes");
    checkEq(*data, std::string("7|1|1|KG;-3;1064"), "builder data line");

    Builder wrong(Family::Automatic, Access::Write);
    wrong.word("GL19", 5);  // GL19 is a Long, not a Word
    check(!wrong.ok(), "type mismatch is caught at build time");

    Builder unclosed(Family::Automatic, Access::Write);
    unclosed.end();
    check(!unclosed.ok(), "unbalanced end() is caught");
}

static void testReadWithoutRecord() {
    group("read telegrams carry no data");

    // A header parsed off a console line has no record at all. For a read that
    // is the complete telegram, not a telegram missing its data -- encoding it
    // used to fail, which is how a hand-typed read reached the device as an
    // error message instead of a request.
    auto header = parseHeader("A?ST8D");
    check(header.ok(), "read header parses");
    if (header.ok()) {
        Telegram t;
        t.header = *header;
        check(t.records.empty(), "no record after parsing a bare header");

        auto line = encodeOneLine(t);
        check(line.ok(), "recordless read encodes");
        if (line.ok()) checkEq(*line, std::string("A?ST8D"), "round-trips as typed");
    }

    // Same in the GxTools form, where the family letter is absent. This is what
    // a device configured for the old header format expects.
    auto legacy = parseHeader("?ST8D");
    check(legacy.ok(), "legacy read header parses");
    if (legacy.ok()) {
        checkEq(static_cast<int>(legacy->family), static_cast<int>(Family::Legacy), "bare ? is the legacy family");
        Telegram t;
        t.header = *legacy;
        auto line = encodeOneLine(t);
        check(line.ok(), "legacy recordless read encodes");
        if (line.ok()) checkEq(*line, std::string("?ST8D"), "legacy form preserved");
    }

    // A block read, to be sure the rule holds past the first token.
    auto block = parseHeader("A?PV04|PW02|GL19|LX02");
    check(block.ok(), "block read header parses");
    if (block.ok()) {
        Telegram t;
        t.header = *block;
        auto line = encodeOneLine(t);
        check(line.ok(), "recordless block read encodes");
        if (line.ok()) {
            checkEq(*line, std::string("A?PV04|PW02|GL19|LX02"), "every token stands alone");
        }
    }

    // A write with no data is still an error: there the values are the point.
    auto write = parseHeader("A!GW7D");
    if (write.ok()) {
        Telegram t;
        t.header = *write;
        check(!encodeOneLine(t).ok(), "a recordless write is still rejected");
    }
}

static void testValidation() {
    group("validation");

    // The Unique reset sequence, checked against two device releases.
    Builder b(Family::Automatic, Access::Write);
    b.word("GW7D", 0);
    Telegram disable = b.build();

    ValidateOptions modern;
    modern.device_version = Version(16, 40);
    auto ok_diags = validate(disable, modern);
    check(hasNoErrors(ok_diags), "GW7D accepted on 16.40");

    ValidateOptions old_device;
    old_device.device_version = Version(14, 0);
    auto bad_diags = validate(disable, old_device);
    check(!hasNoErrors(bad_diags), "GW7D rejected on 14.00");
    bool found = false;
    for (const Diagnostic& d : bad_diags) {
        if (d.code == "version.too_new") found = true;
    }
    check(found, "diagnostic identifies the version gap");

    // SW9B needs 16.40 exactly.
    Builder r(Family::Automatic, Access::Read);
    r.word("SW9B", 0);
    Telegram probe = r.build();
    ValidateOptions v1620;
    v1620.device_version = Version(16, 20);
    check(!hasNoErrors(validate(probe, v1620)), "SW9B rejected on 16.20");
    check(hasNoErrors(validate(probe, modern)), "SW9B accepted on 16.40");

    // Arity mismatch between header and record.
    Telegram broken = disable;
    broken.records.front().push_back(Value(std::int16_t(1)));
    auto arity = validate(broken, modern);
    bool arity_found = false;
    for (const Diagnostic& d : arity) {
        if (d.code == "record.arity") arity_found = true;
    }
    check(arity_found, "record arity mismatch reported");

    // Unknown token warning.
    Header h;
    h.nodes.push_back(Node(Token('G', 'W', 0xFE)));
    auto unknown = validate(h, modern);
    bool unknown_found = false;
    for (const Diagnostic& d : unknown) {
        if (d.code == "token.unknown") unknown_found = true;
    }
    check(unknown_found, "unknown token reported as a warning");
    check(hasNoErrors(unknown), "unknown token is not an error");
}

static void testUniqueSequence() {
    group("worked example: Unique buffer reset");

    struct Step {
        const char* description;
        Telegram telegram;
    };

    auto make = [](Access access, const char* token, std::optional<std::int16_t> value) {
        Builder b(Family::Automatic, access);
        auto t = Token::parse(token);
        if (value) {
            b.word(token, *value);
        } else if (t && t->arity() == 0) {
            b.command(token);
        } else {
            b.query(token);
        }
        return b.build();
    };

    std::vector<Step> steps;
    steps.push_back({"read current state", make(Access::Read, "GW7D", std::nullopt)});
    steps.push_back({"disable Unique intake", make(Access::Write, "GW7D", 0)});
    steps.push_back({"clear the buffer", make(Access::Write, "XX13", std::nullopt)});
    steps.push_back({"re-enable Unique intake", make(Access::Write, "GW7D", 1)});
    steps.push_back({"probe buffer readiness", make(Access::Read, "SW9B", std::nullopt)});

    ValidateOptions opts;
    opts.device_version = Version(16, 40);
    opts.warn_payload_on_read = false;

    std::vector<std::string> wire;
    for (const Step& s : steps) {
        auto diags = validate(s.telegram, opts);
        check(hasNoErrors(diags), std::string("valid on 16.40: ") + s.description);
        auto line = encodeOneLine(s.telegram);
        check(line.ok(), std::string("encodes: ") + s.description);
        if (line.ok()) wire.push_back(*line);
    }

    checkEq(wire.size(), std::size_t(5), "five telegrams produced");
    checkEq(wire[1], std::string("A!GW7D|0"), "disable telegram matches");
    checkEq(wire[2], std::string("A!XX13"), "clear telegram matches");
    checkEq(wire[3], std::string("A!GW7D|1"), "enable telegram matches");

    std::cout << "\n  wire form:\n";
    for (const std::string& line : wire) std::cout << "    " << line << "\n";
}

static void testBinary() {
    group("binary wire format");

    using namespace gxnet::binary;

    Options opts;
    opts.units = inferredUnitTable();

    // A poll captured from a live line: MDW_GET_BUFF with a 3000 ms timeout.
    auto poll = parseHex("90770102D1060BB8", opts);
    check(poll.ok(), "poll frame parses");
    if (poll.ok()) {
        checkEq(poll->nodes.size(), std::size_t(1), "one token after the header");
        checkEq(poll->nodes[0].token.str(), std::string("MW06"), "MDW_GET_BUFF decoded");
        checkEq(std::get<std::int16_t>(poll->nodes[0].value), std::int16_t(3000), "timeout value 3000");
        auto again = encode(*poll, opts);
        check(again.ok(), "poll re-encodes");
        if (again.ok()) {
            checkEq(toHex(*again), std::string("90770102D1060BB8"), "poll round-trips byte for byte");
        }
    }

    // A package data response carrying two PSV_DATA blocks.
    //
    // The frame is a real one, byte for byte, with the payload replaced: PLU
    // numbers, unique codes and the plain texts that carry fragments of them
    // are fabricated. Every replacement is the same length as what it replaced,
    // so the structure this test exists to check -- block lengths that count
    // their own LX02, text padded to an even byte count without the pad being
    // counted, the packed unit/exponent field -- is exactly as the device
    // produced it.
    const char* kCapture =
        "D0710201D60801BE360400DA3102000301090000020600009F7A0207000078CE0219"
        "000003E8021A00000000021B0000000002160000000302170000000102540000002A"
        "028C00000002021400000000021500000000021800000000025500000000030200FD"
        "000000080310003E000003E80752001F303130303030303030303030303030313231"
        "3153414D504C1D39334141414100330000FD000002D23310003E000002D2330400FC"
        "00001C30022B00009F7A022C00000290076100063153414D504C0762000441414141"
        "0763000430303030079000003213000000003217000005E64002360400DA31020004"
        "01090000020600009F7A0207000078CE0219000003E8021A00000000021B00000000"
        "02160000000402170000000102540000002A028C0000000202140000000002150000"
        "0000021800000000025500000000030200FD000000080310003E000003E80752001F"
        "3031303030303030303030303030303132313253414D504C1D393342424242003300"
        "00FD000002DB3310003E000002DB330400FC00001C8B022B00009F7A022C00000290"
        "076100063253414D504C076200044242424207630004303030300790000032130000"
        "00003217000009CE40024002";

    auto frame = parseHex(kCapture, opts);
    check(frame.ok(), "captured package data parses");
    if (!frame.ok()) {
        std::cout << "         " << frame.error.message << "\n";
        return;
    }

    checkEq(frame->nodes.size(), std::size_t(1), "one MV08 sequence block");
    checkEq(frame->nodes[0].token.str(), std::string("MV08"), "MDV_SEQUENZ_END decoded");
    checkEq(frame->nodes[0].children.size(), std::size_t(2), "two package blocks inside");

    const Node& pkg = frame->nodes[0].children[0];
    checkEq(pkg.token.str(), std::string("PV04"), "PSV_DATA block");

    // Pull out the fields that matter operationally.
    std::int32_t plu = -1;
    std::int32_t numerator = -1;
    std::string unique_code;
    Dimension net;
    for (const Node& n : pkg.children) {
        const std::string t = n.token.str();
        if (t == "GL19") plu = std::get<std::int32_t>(n.value);
        if (t == "GL16") numerator = std::get<std::int32_t>(n.value);
        if (t == "GT52") unique_code = std::get<std::string>(n.value);
        if (t == "PD00") net = std::get<Dimension>(n.value);
    }
    checkEq(plu, 1000, "PLU number decoded");
    checkEq(numerator, 3, "label numerator decoded");
    checkEq(net.unit, std::string("KG"), "net weight unit");
    checkEq(net.exponent, -3, "net weight exponent");
    checkEq(static_cast<long long>(net.mantissa), 722LL, "net weight mantissa");
    check(unique_code.rfind("01", 0) == 0 && unique_code.find('\x1d') != std::string::npos,
          "GGT_CODE2 carries a GS1 element string: (01) GTIN, group separator, (93) tail");

    auto reencoded = encode(*frame, opts);
    check(reencoded.ok(), "capture re-encodes");
    if (reencoded.ok()) {
        std::string original = toHex(fromHex(kCapture));
        checkEq(toHex(*reencoded), original, "capture round-trips byte for byte");
    }

    // The unit/exponent packing is symmetric.
    int unit = 0, exp = 0;
    unpackUnitField(0x00FD, unit, exp);
    checkEq(unit, 3, "0x00FD unit code");
    checkEq(exp, -3, "0x00FD exponent");
    checkEq(static_cast<int>(packUnitField(3, -3)), 0x00FD, "field repacks");
    unpackUnitField(0x003E, unit, exp);
    checkEq(unit, 0, "0x003E unit code");
    checkEq(exp, -2, "0x003E exponent");

    // An unmapped unit still round-trips through the "#code" spelling.
    Options bare;
    auto no_table = parseHex("D0710201330000FD000002D2", bare);
    check(no_table.ok(), "parses without a unit table");
    if (no_table.ok()) {
        checkEq(std::get<Dimension>(no_table->nodes[0].value).unit, std::string("#3"), "unknown unit rendered as #3");
        auto back = encode(*no_table, bare);
        check(back.ok(), "#3 spelling re-encodes");
    }

    // A decoded frame flows into the textual model unchanged.
    Telegram t = toTelegram(*frame);
    auto diags = validate(t, ValidateOptions{});
    check(hasNoErrors(diags), "decoded capture validates as a telegram");
    std::cout << "\n  as text: " << encodeHeader(t.header).substr(0, 96) << "...\n";
}

/// The vendor's own test telegrams, from the FunctionsExample sample.
///
/// Worth having precisely because nobody here wrote them: they are what
/// Bizerba feeds its own parser, so a codec that disagrees with them is wrong
/// about something. Two of them exercise the case that is easy to get wrong --
/// a nested block in the interleaved form, where a value follows its token and
/// the closing LX02 comes after the value rather than before it.
void testVendorSamples() {
    group("the vendor's own test telegrams");

    // Header-plus-data, and the Ix registration telegram that first made the
    // parser accept an unknown group letter.
    auto split = parseLines({"I!LV01|RX01|GT08|LX02", "Scale_1"});
    check(split.ok(), "I!LV01|RX01|GT08|LX02 + Scale_1 parses");
    if (split) {
        checkEq(split->records.size(), std::size_t(1), "one record");
        checkEq(encodeLines(*split)->at(0), std::string("I!LV01|RX01|GT08|LX02"), "and re-encodes byte for byte");
    }

    // The same thing interleaved: the value sits between GT08 and the LX02
    // that closes the block it is in.
    auto one = parseOneLine("I!LV01|RX01|GT08|Scale_1|LX02");
    check(one.ok(), "the interleaved form parses");
    if (one) {
        checkEq(*encodeOneLine(*one), std::string("I!LV01|RX01|GT08|Scale_1|LX02"), "and round-trips");
    }

    // A memory-card telegram: a block inside a block, with a value in each.
    auto card = parseLines({"A!MV07|GT01|LV01|RX01|GT08|LX02|LX02", "test|Scale_1"});
    check(card.ok(), "the memocard telegram parses");
    if (card) {
        checkEq(encodeLines(*card)->at(0), std::string("A!MV07|GT01|LV01|RX01|GT08|LX02|LX02"), "header round-trips");
        checkEq(encodeLines(*card)->at(1), std::string("test|Scale_1"), "so does the data line");
    }

    auto card_one = parseOneLine("A!MV07|GT01|test|LV01|RX01|GT08|Scale_1|LX02|LX02");
    check(card_one.ok(), "and interleaved, nested, with a value before the inner block opens");
    if (card_one) {
        checkEq(*encodeOneLine(*card_one), std::string("A!MV07|GT01|test|LV01|RX01|GT08|Scale_1|LX02|LX02"),
                "round-trips");
    }

    // LGV_SEQUENZ batches several commands into one telegram, and the same
    // subfunction may appear more than once: this is three sum clears with
    // three different arguments. From the BccOcx sample.
    //
    // Worth knowing beyond the parsing: the reference gives a sequence
    // transactional semantics -- it stops at the first command that fails and
    // the negative acknowledgement names it. That is what the changeover
    // sequence wants.
    auto sequence = parseLines({"A!LV01|XW07|XW07|XW07|LX02", "1|2|10"});
    check(sequence.ok(), "a command sequence with a repeated subfunction parses");
    if (sequence) {
        checkEq(sequence->records.front().size(), std::size_t(3), "three values, one per repetition");
        checkEq(encodeLines(*sequence)->at(0), std::string("A!LV01|XW07|XW07|XW07|LX02"), "and re-encodes");
    }
}

int main() {
    testToken();
    testVersion();
    testDimension();
    testEscaping();
    testUnicodeModes();
    testRegistry();
    testCompileTimeRegistry();
    testEncodeDocumentedExample();
    testBlocksAndOneLine();
    testMultiRecord();
    testBuilder();
    testReadWithoutRecord();
    testValidation();
    testUniqueSequence();
    testBinary();
    testVendorSamples();

    std::cout << "\n" << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";
    return g_failures == 0 ? 0 : 1;
}
