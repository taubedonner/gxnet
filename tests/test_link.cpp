// SPDX-License-Identifier: MIT
// Transport-layer tests. Same shape as test_gxnet.cpp: plain asserts, no
// framework.
#include <array>
#include <chrono>
#include <iostream>
#include <string>
#include <vector>

#include "gxnet/gxnet.hpp"
#include "gxnet/link/logging.hpp"
#include "gxnet/link/mock.hpp"
#include "gxnet/link/operations.hpp"
#include "gxnet/link/worker.hpp"

using namespace gxnet;
using namespace gxnet::link;

namespace {

int g_checks = 0;
int g_failures = 0;

void group(const std::string& name) { std::cout << "\n== " << name << "\n"; }

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

Endpoint testEndpoint() {
    Endpoint endpoint;
    endpoint.device = "test device";
    return endpoint;
}

// --- the tokens the demo drives -------------------------------------------
// Resolved at compile time; a typo or a token missing from the reference is a
// build error rather than something discovered against a running line.
constexpr Token kUniqueData = knownToken("GGW_UNIQUE_DATEN").token;            // GW7D
constexpr Token kDeleteUnique = knownToken("XCX_DELETE_UNIQUE_DATA").token;    // XX13
constexpr Token kUniqueReady = knownToken("SRW_UNIQUE_PCK_DATA_READY").token;  // SW9B
constexpr Token kGxVersion = knownToken("SRT_GX_VERSION").token;               // ST8D
constexpr Token kDate1 = knownToken("GGL_DATUM1").token;                       // GL06
constexpr Token kSimpleText1 = knownToken("GGT_SIMPLE_TXT1").token;            // GT61

void testTokenConstants() {
    group("token constants");

    checkEq(kUniqueData.str(), std::string("GW7D"), "GGW_UNIQUE_DATEN is GW7D");
    checkEq(kDeleteUnique.str(), std::string("XX13"), "XCX_DELETE_UNIQUE_DATA is XX13");
    checkEq(kUniqueReady.str(), std::string("SW9B"), "SRW_UNIQUE_PCK_DATA_READY is SW9B");
    checkEq(kGxVersion.str(), std::string("ST8D"), "SRT_GX_VERSION is ST8D");
    checkEq(kDate1.str(), std::string("GL06"), "GGL_DATUM1 is GL06");
    checkEq(kSimpleText1.str(), std::string("GT61"), "GGT_SIMPLE_TXT1 is GT61");
}

void testOpenAndClose() {
    group("open and close");

    MockTransport device;
    check(!device.isOpen(), "starts closed");

    Endpoint no_name;
    check(static_cast<bool>(device.open(no_name)), "open without a device name fails");
    check(!device.isOpen(), "still closed after a failed open");

    check(!device.open(testEndpoint()), "open with a device name succeeds");
    check(device.isOpen(), "open afterwards");

    device.close();
    check(!device.isOpen(), "closed again");
}

void testReadAndWrite() {
    group("read and write");

    MockTransport device;
    device.open(testEndpoint());

    // Nothing was seeded, so the read fails rather than inventing a value.
    auto unseeded = readOne(device, kUniqueData);
    check(!unseeded, "reading an unseeded token fails");

    device.set(kUniqueData, Value(std::int16_t{1}));
    auto reading = readOne(device, kUniqueData);
    check(reading.ok(), "seeded read succeeds");
    if (reading) {
        checkEq(std::get<std::int16_t>(*reading), std::int16_t(1), "device reports 1");
    }
    checkEq(device.sent().back(), std::string("A?GW7D"), "read goes out as A?GW7D");

    // A write followed by a read-back, which is the only thing that
    // establishes the write took.
    auto written = writeAndVerify(device, kUniqueData, Value(std::int16_t{0}));
    check(written.ok(), "write and verify succeeds");
    checkEq(std::get<std::int16_t>(*device.get(kUniqueData)), std::int16_t(0), "model now holds 0");
    // Header and data go out as two separate lines: that is the shape
    // Send(szHeader, szData) takes, and the interleaved single-string form is
    // what the server rejected in SeperteHeaderData.
    checkEq(device.sent()[device.sent().size() - 3], std::string("A!GW7D"), "write header goes out on its own");
    checkEq(device.sent()[device.sent().size() - 2], std::string("0"), "the value follows as the data line");

    // Text survives the round trip, Cyrillic included.
    const std::string ru = "\xD0\x9F\xD0\xB0\xD1\x80\xD1\x82\xD0\xB8\xD1\x8F";
    auto text = writeAndVerify(device, kSimpleText1, Value(ru));
    check(text.ok(), "simple text round-trips");
    if (text) checkEq(std::get<std::string>(*text), ru, "Cyrillic preserved");
}

void testWriteThatDoesNotTake() {
    group("a write that does not take");

    MockTransport device;
    device.open(testEndpoint());

    // A device that refuses to change: it accepts the write and keeps reporting
    // the old value. This is the failure the read-back exists to catch, and it
    // is the reason a successful send is never treated as confirmation.
    device.handle(kUniqueData, [](const Node&, Access access) -> std::optional<Value> {
        if (access == Access::Read) return Value(std::int16_t{1});
        return std::nullopt;  // swallow the write
    });

    auto result = writeAndVerify(device, kUniqueData, Value(std::int16_t{0}));
    check(!result, "write-and-verify reports the mismatch");
    check(result.error.message.find("did not take") != std::string::npos, "error names the problem");
}

void testTimeoutAndFailure() {
    group("timeouts and transport failures");

    MockTransport device;
    device.open(testEndpoint());
    device.set(kUniqueData, Value(std::int16_t{1}));

    device.timeoutNext();
    auto timed_out = readOne(device, kUniqueData, std::chrono::milliseconds{250});
    check(!timed_out, "a timeout is a failed read");
    check(timed_out.error.message.find("250 ms") != std::string::npos, "error states the timeout that elapsed");

    device.failNext({17194, "PAE_MC_DATA_CODEPAGEDATA_TO_UNICODE"});
    auto failed = readOne(device, kUniqueData);
    check(!failed, "a transport error is a failed read");
    checkEq(failed.error.code, 17194, "the device error code survives");

    // The next call is unaffected: both injections are one-shot.
    auto fine = readOne(device, kUniqueData);
    check(fine.ok(), "the following read works");
}

void testUniqueDataSequence() {
    group("unique-data changeover, end to end");

    MockTransport device;
    device.open(testEndpoint());

    // The buffer the sequence is there to clear. XX13 empties it; the readiness
    // flag follows the buffer.
    int codes_in_buffer = 4200;
    device.handle(kDeleteUnique, [&](const Node&, Access) -> std::optional<Value> {
        codes_in_buffer = 0;
        return std::nullopt;
    });
    // SW9B is undocumented -- flag or remaining count, nobody knows. The test
    // states which reading it exercises rather than the mock deciding.
    device.handle(kUniqueReady, [&](const Node&, Access access) -> std::optional<Value> {
        if (access != Access::Read) return std::nullopt;
        return Value(static_cast<std::int16_t>(codes_in_buffer > 0 ? 1 : 0));
    });
    device.set(kUniqueData, Value(std::int16_t{1}));

    // Disable intake *before* touching the buffer: with intake enabled the
    // machine swallows a dropped file immediately, on top of the old codes.
    auto disabled = writeAndVerify(device, kUniqueData, Value(std::int16_t{0}));
    check(disabled.ok(), "intake disabled and confirmed");

    Builder clear(Family::Automatic, Access::Write);
    clear.command(kDeleteUnique.str());
    Request clear_request;
    clear_request.telegram = clear.build();
    clear_request.expect_reply = false;
    auto cleared = device.execute(clear_request);
    check(cleared.ok(), "buffer cleared");
    checkEq(codes_in_buffer, 0, "the buffer really is empty");

    auto ready = readOne(device, kUniqueReady);
    check(ready.ok(), "readiness probed");
    if (ready) {
        checkEq(std::get<std::int16_t>(*ready), std::int16_t(0), "nothing buffered after the clear");
    }

    auto enabled = writeAndVerify(device, kUniqueData, Value(std::int16_t{1}));
    check(enabled.ok(), "intake re-enabled and confirmed");

    // The whole exchange, in order, is what an operator would see in the log.
    const std::vector<std::string> expected = {
        "A!GW7D", "0",  // disable: header, then the data line
        "A?GW7D",       // read back
        "A!XX13",       // clear -- a command, so no data line
        "A?SW9B",       // probe
        "A!GW7D", "1",  // re-enable
        "A?GW7D",       // read back
    };
    checkEq(device.sent().size(), expected.size(), "eight lines went out");
    bool same = device.sent().size() == expected.size();
    for (std::size_t i = 0; same && i < expected.size(); ++i) {
        same = device.sent()[i] == expected[i];
    }
    check(same, "in the documented order");
    if (!same) {
        for (const std::string& line : device.sent()) {
            std::cout << "         sent: " << line << "\n";
        }
    }
}

/// The wire form of the composite telegrams, pinned down.
///
/// These are transcriptions of the reference's field order, and the field order
/// is the part that cannot be checked against a device cheaply: a block put
/// together wrong does not fail, it addresses something else.
void testCompositeTelegrams() {
    group("composite telegrams");

    // A full PLU change -- XCV_DBTAB_DATASET carrying nothing but the PLU
    // number. Every parameter of XV00 is optional, and with only GGL_PLUNR
    // present the machine does the PLU change and no other import.
    auto plu = encodeLines(pluChange(1622));
    check(plu.ok(), "PLU change encodes");
    if (plu) {
        checkEq(plu->size(), std::size_t(2), "header and one data line");
        checkEq(plu->at(0), std::string("A!XV00|GL19|LX02"), "PLU change header");
        checkEq(plu->at(1), std::string("1622"), "PLU change data");
    }

    // The customer number is the second database key: a value set is held per
    // (PLU, customer) pair.
    auto with_customer = encodeLines(pluChange(1622, 7));
    check(with_customer.ok(), "PLU change with a customer number encodes");
    if (with_customer) {
        checkEq(with_customer->at(0), std::string("A!XV00|GL19|GL1A|LX02"), "both keys in the header");
        checkEq(with_customer->at(1), std::string("1622|7"), "both keys in the data");
    }

    // WZW_SDD_TYP = 8, one element of WZW_SDD_ELEM_TYP = 5. The pairing is the
    // reference's, from its table of permitted combinations.
    auto confirm = encodeLines(confirmDialog(1, "Check", "Continue?", /*with_element_count=*/true));
    check(confirm.ok(), "confirmation dialog encodes");
    if (confirm) {
        checkEq(confirm->at(0), std::string("A!WV60|WW60|WW61|WW62|WT60|WV62|WW63|WT62|WW65|LX02|LX02"),
                "confirmation header, inner block closed before the outer one");
        checkEq(confirm->at(1), std::string("1|8|1|Check|5|Continue?|0"),
                "handle, type 8, one element, headline, then the element");
    }

    // WZW_SDD_TYP = 7 with one WZV_SDD_DATA per entry. WZW_SDD_ELEM_ACTIVE is a
    // position among those records, one based -- not an id.
    const std::array<DialogItem, 2> items{{{"A", 10}, {"B", 11}}};
    auto selection = encodeLines(selectionDialog(1, "Pick", items, 2, /*with_element_count=*/true));
    check(selection.ok(), "selection dialog encodes");
    if (selection) {
        checkEq(selection->at(0),
                std::string("A!WV60|WW60|WW61|WW62|WW69|WT60|WV62|WW63|WT62|WW64|"
                            "LX02|WV62|WW63|WT62|WW64|LX02|LX02"),
                "selection header, one block per entry");
        checkEq(selection->at(1), std::string("1|7|2|2|Pick|4|A|10|4|B|11"),
                "count, active position, headline, then the entries");
    }

    // Out of range is clamped rather than sent: the device's behaviour for a
    // position that names no record is not documented.
    auto clamped = encodeLines(selectionDialog(1, "Pick", items, 9, /*with_element_count=*/true));
    if (clamped) {
        checkEq(clamped->at(1), std::string("1|7|2|1|Pick|4|A|10|4|B|11"),
                "an active position past the end falls back to the first entry");
    }

    // A read, not a write. Sent as a write the device answers LGW_RETURN 2154;
    // a polling client asks. Nothing else about the telegram differs.
    auto poll = encodeLines(bufferPoll(3000));
    check(poll.ok(), "the buffer poll encodes");
    if (poll) {
        checkEq(poll->at(0), std::string("A?MW06"), "buffer poll is a read");
        checkEq(poll->at(1), std::string("3000"), "carrying the timeout in ms");
    }

    // Without the deduced field: the shape to try when the device answers an
    // internal error and draws an empty box.
    // The default, and the only form the device accepts: measured on 15.61,
    // a dialog carrying WW62 is refused and one without it renders.
    auto without = encodeLines(confirmDialog(1, "Check", "Continue?"));
    if (without) {
        checkEq(without->at(0), std::string("A!WV60|WW60|WW61|WT60|WV62|WW63|WT62|WW65|LX02|LX02"),
                "WW62 can be left out entirely");
        checkEq(without->at(1), std::string("1|8|Check|5|Continue?|0"), "and the data line follows it");
    }

    // Escaping is not optional here: the entries are operator-facing text, and
    // a '|' in one would otherwise be read as a field separator.
    const std::array<DialogItem, 1> awkward{{{"a|b@c", 1}}};
    auto escaped = encodeLines(selectionDialog(1, "H", awkward, 1, /*with_element_count=*/true));
    if (escaped) {
        checkEq(escaped->at(1), std::string("1|7|1|1|H|4|a@7Cb@40c|1"),
                "a pipe and an at sign in a label survive as escapes");
    }
}

/// The knobs that exist because the device refuses the telegram and the reason
/// is not known. Each one is a shape to try, and each has to encode to what it
/// claims to -- an experiment that varies two things at once settles nothing.
void testDialogVariants() {
    group("dialog variants");

    // The reference's own table of permitted combinations.
    checkEq(pairedElementType(8), std::int16_t(5), "type 8 takes display elements");
    checkEq(pairedElementType(9), std::int16_t(5), "so does type 9");
    checkEq(pairedElementType(7), std::int16_t(4), "type 7 takes selections");
    checkEq(pairedElementType(1), std::int16_t(1), "type 1 takes numeric input");
    checkEq(pairedElementType(0), std::int16_t(0), "and a type the table does not list pairs with nothing");

    DialogSpec spec;
    spec.type = 9;  // display only: no confirmation, the least the device can refuse
    spec.headline = "H";
    spec.elements.push_back({"Text", 0, 1});
    // These checks predate the finding that WW62 must not be sent; they pin the
    // wire form of each knob, so they keep asking for it explicitly.
    spec.with_element_count = true;

    auto display = encodeLines(dialog(spec));
    check(display.ok(), "a display-only dialog encodes");
    if (display) {
        checkEq(display->at(0), std::string("A!WV60|WW60|WW61|WW62|WT60|WV62|WW63|WT62|WW65|LX02|LX02"),
                "same shape as a confirmation");
        checkEq(display->at(1), std::string("1|9|1|H|5|Text|1"),
                "type 9, and the element attribute is the caller's, not a fixed zero");
    }

    // WZW_SDD_ELEM_ACTIVE is optional in the reference and was being sent for
    // selections only. It is a field like any other now.
    spec.with_active = true;
    auto with_active = encodeLines(dialog(spec));
    if (with_active) {
        checkEq(with_active->at(0), std::string("A!WV60|WW60|WW61|WW62|WW69|WT60|WV62|WW63|WT62|WW65|LX02|LX02"),
                "WW69 sits between the count and the headline");
    }
    spec.with_active = false;

    // Not marked optional by the reference, which is what makes leaving it out
    // an experiment rather than a supported form.
    spec.with_headline = false;
    auto headless = encodeLines(dialog(spec));
    if (headless) {
        checkEq(headless->at(0), std::string("A!WV60|WW60|WW61|WW62|WV62|WW63|WT62|WW65|LX02|LX02"),
                "the headline can be left out entirely");
        checkEq(headless->at(1), std::string("1|9|1|5|Text|1"), "and its field goes with it");
    }
    spec.with_headline = true;

    // Both forms are legal: the reference's own worked example never closes its
    // block. Worth being able to send, since the device's complaint is about
    // the telegram's construction.
    spec.close_blocks = false;
    auto unclosed = encodeLines(dialog(spec));
    if (unclosed) {
        checkEq(unclosed->at(0), std::string("A!WV60|WW60|WW61|WW62|WT60|WV62|WW63|WT62|WW65"),
                "no LGX_CLOSE at all, blocks running to the end of the header");
        checkEq(unclosed->at(1), std::string("1|9|1|H|5|Text|1"), "the data line is unaffected");
    }
    spec.close_blocks = true;

    // A pairing outside the table. The device may well refuse it -- that is the
    // point; what must not happen is the builder quietly correcting it.
    spec.element_type = 4;
    auto mismatched = encodeLines(dialog(spec));
    if (mismatched) {
        checkEq(mismatched->at(0), std::string("A!WV60|WW60|WW61|WW62|WT60|WV62|WW63|WT62|WW64|LX02|LX02"),
                "an overridden element type brings its own tail field, WW64 rather than WW65");
    }
}

/// The other way to ask the operator something, and on this device the one that
/// is not refused.
void testRemoteSoftkeys() {
    group("remote softkeys");

    SoftkeySpec spec;
    spec.number = 3;
    spec.label = "Partie OK";

    auto button = encodeLines(remoteSoftkey(spec));
    check(button.ok(), "a push-button softkey encodes");
    if (button) {
        checkEq(button->at(0), std::string("A!WV04|WW06|WW08|WW07|WW09|WT00|LX02"),
                "number, attribute, type and digits, then the caption");
        checkEq(button->at(1), std::string("3|1|0|0|Partie OK"), "active, type 0 = push button");
    }

    // The reference's own note: leaving the number out is not a no-op, it
    // applies the properties to every remote softkey at once.
    spec.number = std::nullopt;
    auto all = encodeLines(remoteSoftkey(spec));
    if (all) {
        checkEq(all->at(0), std::string("A!WV04|WW08|WW07|WW09|WT00|LX02"), "no WW06 addresses every key");
        checkEq(all->at(1), std::string("1|0|0|Partie OK"), "and the remaining fields keep their order");
    }

    // The type is optional and the digit count goes with it.
    spec.number = 3;
    spec.type = std::nullopt;
    auto typeless = encodeLines(remoteSoftkey(spec));
    if (typeless) {
        checkEq(typeless->at(0), std::string("A!WV04|WW06|WW08|WT00|LX02"), "no type means no digit count either");
    }

    auto numeric = SoftkeySpec{};
    numeric.number = 5;
    numeric.type = SoftkeyType::Numeric;
    numeric.digits = 4;
    numeric.label = "Menge";
    auto entry = encodeLines(remoteSoftkey(numeric));
    if (entry) {
        checkEq(entry->at(1), std::string("5|1|2|4|Menge"), "a numeric key carries its digit count");
    }

    // Deleting says nothing about type or caption, and the reference makes both
    // optional -- sending them would be setting properties on a key being
    // removed.
    auto cleared = encodeLines(clearSoftkey(3));
    if (cleared) {
        checkEq(cleared->at(0), std::string("A!WV04|WW06|WW08|LX02"), "a delete is the number and the attribute");
        checkEq(cleared->at(1), std::string("3|-1"), "attribute -1");
    }
    auto cleared_all = encodeLines(clearSoftkey());
    if (cleared_all) {
        checkEq(cleared_all->at(0), std::string("A!WV04|WW08|LX02"), "and with no number it clears every key");
    }

    // The answer. A press may arrive on the request or spontaneously; the
    // parser is not told which and does not need to be.
    auto pressed = parseLines({"A!WV05|WW06|WW07|LX02", "3|0"});
    check(pressed.ok(), "a softkey press parses");
    if (pressed) {
        const auto input = parseSoftkeyInput(*pressed);
        check(input.has_value(), "and is recognised as one");
        if (input) {
            checkEq(input->number, std::int16_t(3), "the key that was pressed");
            checkEq(input->type, std::int16_t(0), "a push button");
            check(!input->value.has_value(), "which carries no value");
        }
    }

    auto typed = parseLines({"A!WV05|WW06|WW07|WL0A|LX02", "5|2|1234"});
    if (typed) {
        const auto input = parseSoftkeyInput(*typed);
        check(input.has_value(), "a numeric entry parses too");
        if (input && input->value) checkEq(*input->value, std::int32_t(1234), "and carries what was typed");
    }

    // Anything else is not a press, and must not be reported as one.
    auto ack = parseLines({"A!LW00", "38400"});
    if (ack) check(!parseSoftkeyInput(*ack).has_value(), "an acknowledgement is not a softkey press");
}

/// Internal error codes, taken apart by the rule the reference publishes for
/// them rather than by the table it prints incompletely.
void testInternalErrorCodes() {
    group("internal error codes");

    // The one that matters: what the device answers to WZV_SDD_START. The
    // appendix jumps from 17702 to 17715, so 17705 is not printed -- but the
    // input-tool group's base is 17700 and +5 is "function not available".
    checkEq(internalErrorText(17705), std::string("input tools: function not available"),
            "0x4529 decodes, and it says the function is not there");

    // The two entries the appendix does print for that group, which is what
    // makes the base defensible.
    checkEq(internalErrorText(17701), std::string("input tools: overflow"), "17701 matches PAE_EWZ_OVF");
    checkEq(internalErrorText(17702), std::string("input tools: underflow"), "17702 matches PAE_EWZ_UVF");
    checkEq(internalErrorText(17715), std::string("input tools: memory manager error"), "17715 matches PAE_EWZ_MEM");

    checkEq(internalErrorText(17205), std::string("connection layer: function not available"),
            "the same scheme in another group");
    checkEq(internalErrorText(15600 + 19), std::string("printer: invalid parameter"), "+19 is a parameter error");

    // Groups the appendix numbers freely from a base that is not a round
    // hundred. Guessing at those would produce confident nonsense.
    check(internalErrorText(17351).empty(), "a code outside the tabulated groups stays undecoded");
    check(internalErrorText(0).empty(), "and so does zero");
    check(internalErrorText(17750).empty(), "an offset past the coded range is not invented");
}

/// Two reads that replace a walk to the terminal.
void testDeviceQueries() {
    group("device queries");

    auto text = encodeLines(errorTextQuery(17705));
    check(text.ok(), "the error-text query encodes");
    if (text) {
        checkEq(text->at(0), std::string("A?WV4A|LW03|LX02"), "WZV_META_ERROR_TEXT asking by LGW_DEBUG");
        checkEq(text->at(1), std::string("17705"), "with the code on the data line");
    }

    auto addon = encodeLines(addonPsvPckQuery());
    check(addon.ok(), "the outgoing-lines query encodes");
    if (addon) {
        checkEq(addon->at(0), std::string("A?SV5B|LX02"), "a bare read of SRV_NET_KONF_ADDON_PSV_PCK");
    }
}

/// The data line BCS insists on for a read.
///
/// The codec is right and the server is picky: a read is a header alone by the
/// reference, but the BCS parser rejects an empty field for a D, L or W command
/// and warns that it will stop tolerating it.
void testReadPlaceholder() {
    group("read placeholder data");

    const auto headerOf = [](const std::string& line) {
        auto parsed = parseHeader(line);
        return parsed ? *parsed : Header{};
    };

    checkEq(readPlaceholderData(headerOf("A?SW9B")), std::string("0"),
            "a word read gets a zero rather than an empty field");
    checkEq(readPlaceholderData(headerOf("A?GL06")), std::string("0"), "so does a long read");
    checkEq(readPlaceholderData(headerOf("A?GT61")), std::string(),
            "a text read needs nothing: the server's complaint names D, L and W");
    checkEq(readPlaceholderData(headerOf("A!XX13")), std::string(), "a command has no data field at all");

    // Mixed headers keep one field per payload token, in header order, so the
    // fields still line up with the tokens they belong to.
    checkEq(readPlaceholderData(headerOf("A?GW7D|GT61|GL06")), std::string("0||0"),
            "one field per payload token, text left empty in the middle");
}

/// The spontaneous channel: records that belong to no request.
///
/// This is the path a dialog answer has to travel. `execute` cannot carry it --
/// it receives against the handle its own Send returned, and a record the device
/// sent unasked has no such handle. The test pins the distinction rather than
/// the mechanism: a queued record must not appear in an exchange, and must
/// appear in a spontaneous receive.
void testSpontaneous() {
    group("spontaneous records");

    MockTransport device;
    Endpoint endpoint;
    endpoint.device = "GLM";
    endpoint.spontaneous = true;
    device.open(endpoint);

    auto quiet = device.receiveSpontaneous(Transport::kDefaultQueue, std::chrono::milliseconds{10});
    check(quiet.ok(), "polling an empty queue is not an error");
    if (quiet) {
        check(quiet->status == Status::Timeout, "an empty queue reports a timeout");
        check(quiet->received.empty(), "and hands back nothing");
    }

    device.set("SW9B"_tok, Value{std::int16_t{1}});
    device.postSpontaneous({"A!WV63|WW60|1|WW68|0|WW64|11|WT62|Second|LX02"});

    // A request must not pick the record up. If it did, the answer to a dialog
    // would attach itself to whatever unrelated read happened to be in flight.
    auto unrelated = readOne(device, "SW9B"_tok);
    check(unrelated.ok(), "an ordinary read still works with a record waiting");
    checkEq(device.spontaneousPending(), std::size_t{1}, "and left the record where it was");

    auto got = device.receiveSpontaneous(Transport::kDefaultQueue, std::chrono::milliseconds{10});
    check(got.ok(), "the record comes back from a spontaneous receive");
    if (got) {
        check(got->reply.has_value(), "and it parsed");
        if (got->reply) {
            auto result = parseDialogResult(*got->reply);
            check(result.has_value(), "as the dialog answer it is");
            if (result) check(result->confirmed(), "the operator confirmed");
        }
    }

    auto again = device.receiveSpontaneous(Transport::kDefaultQueue, std::chrono::milliseconds{10});
    check(again.ok() && again->received.empty(), "a collected record is not handed back twice");

    // Two waiting: the first receive says there is more, which is how a caller
    // knows to come round again rather than waiting for the next poll.
    device.postSpontaneous({"A!WV63|WW60|1|WW68|1|LX02"});
    device.postSpontaneous({"A!WV05|WW71|3|LX02"});
    auto first = device.receiveSpontaneous(Transport::kDefaultQueue, std::chrono::milliseconds{10});
    check(first.ok() && first->status == Status::MoreData, "status 2 while another record waits");
    auto second = device.receiveSpontaneous(Transport::kDefaultQueue, std::chrono::milliseconds{10});
    check(second.ok() && second->status == Status::Ok, "status 0 on the last one");

    // Through the logging decorator. `Transport` gives receiveSpontaneous a
    // default that reports a quiet channel, so a wrapper that forgets to
    // forward it compiles, runs, and answers "nothing waiting" forever.
    {
        auto inner = std::make_unique<MockTransport>();
        MockTransport* raw = inner.get();
        LoggingTransport logging(std::move(inner), nullptr);
        logging.open(endpoint);
        raw->postSpontaneous({"A!WV05|WW06|3|LX02"});

        auto through = logging.receiveSpontaneous(Transport::kDefaultQueue, std::chrono::milliseconds{10});
        check(through.ok() && !through->received.empty(), "the decorator forwards a spontaneous receive");
    }

    // A transport with no spontaneous channel answers like a quiet one rather
    // than failing, so a listener needs no special case for it.
    struct Silent final : Transport {
        LinkError open(const Endpoint&) override { return {}; }
        void close() override {}
        [[nodiscard]] bool isOpen() const override { return true; }
        LinkResult<Exchange> execute(const Request&) override { return LinkResult<Exchange>::of(Exchange{}); }
        [[nodiscard]] std::string description() const override { return "silent"; }
    };
    Silent silent;
    auto none = silent.receiveSpontaneous(Transport::kDefaultQueue, std::chrono::milliseconds{10});
    check(none.ok() && none->status == Status::Timeout, "the default implementation reports a quiet channel");
}

/// Decoding the numbers that say nothing on their own.
void testAnnotations() {
    group("value annotations");

    // SRL_NET_CHANNEL_BITMAP as it comes back from SV5B and SV5C: bit 1 is
    // channel A, bit 6 is channel F.
    checkEq(annotateValue("SL8C"_tok, Value{std::int32_t{2}}), std::string("A"), "bitmap 2 is channel A");
    checkEq(annotateValue("SL8C"_tok, Value{std::int32_t{64}}), std::string("F"), "bitmap 64 is channel F");
    checkEq(annotateValue("SL8C"_tok, Value{std::int32_t{66}}), std::string("A, F"), "bitmap 66 is both");
    checkEq(annotateValue("SL8C"_tok, Value{std::int32_t{1}}), std::string("intern"), "bit 0 is the internal channel");
    checkEq(annotateValue("SL8C"_tok, Value{std::int32_t{0}}), std::string("no channel"), "zero says so");

    // LGW_UFKENN is the token's own code, and the encoding is the thing that
    // was got wrong once: class = (group << 4) | type, and group G is 0, so
    // GT63 is 0x0763 and not 0x7763.
    checkEq(annotateValue("LW02"_tok, Value{std::int32_t{1891}}), std::string("GT63 GGT_SIMPLE_TXT3"),
            "1891 decodes to GT63");
    check(annotateValue("LW02"_tok, Value{std::int32_t{0x7763}}).find("GT63") == std::string::npos,
          "and 0x7763 is a different token entirely");
    check(annotateValue("LW02"_tok, Value{std::int32_t{38499}}).find("WV63") == 0, "38499 decodes to WV63");

    // PSL_PCK_ERR_FLAGS: bit 24 is the one the whole marking automation turns
    // on, so it is worth a test of its own rather than trust in a table.
    check(annotateValue("PL13"_tok, Value{std::int32_t{1 << 24}}).find("no unique data") != std::string::npos,
          "bit 24 is 'no unique data available'");
    check(annotateValue("PL13"_tok, Value{std::int32_t{1 << 13}}).find("read-back") != std::string::npos,
          "bit 13 is the code read-back");
    checkEq(annotateValue("PL13"_tok, Value{std::int32_t{0}}), std::string("no errors"), "zero flags says so");

    // WZW_MODE reads as an ASCII level in the low byte. 53 is '5', which is
    // what the line reported, and it is not TERMINAL.
    check(annotateValue("WW0C"_tok, Value{std::int16_t{53}}).find("'5'") != std::string::npos, "53 is level '5'");
    check(annotateValue("WW0C"_tok, Value{std::int16_t{53}}).find("TERMINAL") == std::string::npos,
          "and level 5 is not TERMINAL");
    check(annotateValue("WW0C"_tok, Value{std::int16_t{57}}).find("TERMINAL") != std::string::npos,
          "57 is '9', TERMINAL");

    // A token with nothing to say stays silent rather than inventing a gloss.
    check(annotateValue("GL19"_tok, Value{std::int32_t{1521}}).empty(), "a PLU number decodes to nothing");
    check(annotateValue("GT63"_tok, Value{std::string("1234")}).empty(), "and so does a text");
}

/// Reading a value out of one block rather than the first one in the telegram.
void testScopedValueOf() {
    group("scoped value lookup");

    // The shape a buffer read actually arrives in: one interleaved line, which
    // `parseOneLine` turns into a node tree with empty values plus one record.
    // Looking only at the nodes finds every package and reads every field as
    // absent -- which is exactly what the Buffer tab did.
    auto reply = parseOneLine(
        "A!MV08"
        "|PV04|GL19|322|GD00|KG;-3;500|LX02"
        "|PV04|GL19|1521|GD00|KG;-3;750|LX02");
    check(reply.ok(), "a two-package buffer answer parses");
    if (!reply) return;

    checkEq(reply->records.size(), std::size_t{1}, "values arrived in the record, not on the nodes");

    std::vector<std::int32_t> plus;
    forEachNode(reply->header.nodes, [&](const Node& node) {
        if (node.token != "PV04"_tok) return;
        if (const auto value = valueOf(*reply, node, "GL19"_tok)) {
            if (const auto* number = std::get_if<std::int32_t>(&*value)) plus.push_back(*number);
        }
    });

    checkEq(plus.size(), std::size_t{2}, "one PLU per package");
    if (plus.size() == 2) {
        checkEq(plus[0], std::int32_t{322}, "the first package keeps its own PLU");
        checkEq(plus[1], std::int32_t{1521}, "and the second is not the first one again");
    }

    // The unscoped form is the one that would have been wrong here, and it is
    // still right for its own job: the first match in the telegram.
    if (const auto first = valueOf(*reply, "GL19"_tok)) {
        check(std::get<std::int32_t>(*first) == 322, "the unscoped lookup finds the first, as documented");
    }
}

/// Asking for the answer rather than waiting for it.
void testDialogQueries() {
    group("dialog answer queries");

    auto by_handle = encodeOneLine(dialogResultQuery(7));
    check(by_handle.ok(), "the handle query encodes");
    if (by_handle) checkEq(*by_handle, std::string("A?WV63|WW60|7|LX02"), "as a read of WV63 carrying WW60");

    auto exit_only = encodeOneLine(dialogExitQuery());
    check(exit_only.ok(), "the WZW_EXIT query encodes");
    if (exit_only) checkEq(*exit_only, std::string("A?WW68|0"), "as a plain read of WW68");

    auto press = encodeOneLine(softkeyPressQuery(3));
    check(press.ok(), "the softkey press query encodes");
    if (press) checkEq(*press, std::string("A?WV05|WW06|3|LX02"), "as a read of WV05 carrying the key number");

    auto any_press = encodeOneLine(softkeyPressQuery());
    check(any_press.ok(), "and without a number");
    if (any_press) checkEq(*any_press, std::string("A?WV05|LX02"), "the block alone");

    auto info = encodeOneLine(softkeyInfoQuery());
    check(info.ok(), "the softkey info query encodes");
    if (info) checkEq(*info, std::string("A?WVA6|LX02"), "as a read of WVA6");
}

/// Reading a dialog answer, in both shapes a reply can arrive in.
void testDialogResult() {
    group("dialog result");

    // What ReceiveOne hands back: one interleaved line, values after tokens.
    auto interleaved = parseOneLine("A!WV63|WW60|1|WW68|0|WW64|11|WT62|Second|LX02");
    check(interleaved.ok(), "an interleaved result parses");
    if (interleaved) {
        auto result = parseDialogResult(*interleaved);
        check(result.has_value(), "and is recognised as a dialog result");
        if (result) {
            checkEq(result->handle, std::int16_t(1), "handle came back");
            check(result->confirmed(), "WZW_EXIT 0 means the operator confirmed");
            check(result->id.has_value() && *result->id == 11, "the chosen id");
            check(result->label.has_value() && *result->label == "Second", "and its label");
        }
    }

    // The other shape: a header line followed by a data line.
    auto split = parseLines({"A!WV63|WW60|WW68|LX02", "3|1"});
    check(split.ok(), "a header-plus-data result parses");
    if (split) {
        auto result = parseDialogResult(*split);
        check(result.has_value(), "and is recognised too");
        if (result) {
            checkEq(result->handle, std::int16_t(3), "handle out of the record");
            check(!result->confirmed(), "WZW_EXIT 1 is cancel with HOME");
            check(!result->id.has_value(), "a confirmation carries no id");
        }
    }

    // An acknowledgement is not a dialog result, and must not be read as one --
    // "confirmed" would otherwise be invented out of an unrelated telegram.
    auto ack = parseOneLine("A!LW00|0");
    if (ack) {
        check(!parseDialogResult(*ack).has_value(), "an acknowledgement is not mistaken for an answer");
    }

    // LGW_RETURN, as an XV00 acknowledgement carries it.
    auto quit = parseOneLine("A!LV00|LW01|2650|LX02");
    check(quit.ok(), "a negative acknowledgement parses");
    if (quit) {
        auto code = returnCodeOf(*quit);
        check(code.has_value() && *code == 2650, "LGW_RETURN read back");
        checkEq(std::string(returnCodeText(2650)),
                std::string("data record not available: no such PLU in the database"), "and decoded");
        check(returnCodeText(4).find("third-party") != std::string_view::npos,
              "code 4 is the 'fremdes Kommando' a device answers for an unknown "
              "subfunction");
        check(returnCodeText(31337).empty(), "an unlisted code decodes to nothing");

        // Codes the English edition of the reference does not have. Its
        // LGW_RETURN table stops at 2658 and skips 14 to 24 entirely; the
        // German one is the same table, further along. Worth pinning down,
        // because these are the ones that say why an input tool said no.
        check(returnCodeText(17).find("mandatory") != std::string_view::npos,
              "17 is a missing mandatory parameter, not an unknown code");
        check(returnCodeText(19).find("input tool") != std::string_view::npos, "19 is 'an input tool is open'");
        check(returnCodeText(2700).find("already occupied") != std::string_view::npos,
              "2700 is 'input is already occupied'");
        check(returnCodeText(2157).find("does not exist") != std::string_view::npos,
              "2157 is a missing file, which is what an FTP operation needs to be able to say");
    }
}

void testReplayFromCapture() {
    group("replaying a capture");

    // A frame lifted from the line, decoded by the binary reader, then used to
    // seed the model. The values a read reports are then the values the device
    // actually reported, not something written into a fixture by hand.
    const char* kPoll = "D0710201021900000656";  // GL19 GGL_PLUNR = 1622
    binary::Options binary_options;
    binary_options.units = binary::inferredUnitTable();
    auto frame = binary::parseHex(kPoll, binary_options);
    check(frame.ok(), "capture decodes");
    if (!frame.ok()) return;

    MockTransport device;
    device.open(testEndpoint());
    device.seedFrom(binary::toTelegram(*frame));

    constexpr Token kPlu = knownToken("GGL_PLUNR").token;  // GL19
    auto plu = readOne(device, kPlu);
    check(plu.ok(), "PLU reads back from the seeded capture");
    if (plu) {
        checkEq(std::get<std::int32_t>(*plu), 1622, "PLU 1622, as captured");
    }
}

void testWorkerThreading() {
    group("worker thread");

    auto transport = std::make_unique<MockTransport>();
    MockTransport* device = transport.get();
    Worker worker(std::move(transport));

    LinkError open_error{0, "not called"};
    worker.open(testEndpoint(), [&](LinkError error) { open_error = error; });

    // Nothing has been delivered yet: completions wait for drain().
    while (worker.pending() > 0) std::this_thread::yield();
    checkEq(open_error.message, std::string("not called"), "completion waits for drain");

    worker.drain();
    check(!open_error, "open succeeded, delivered on the calling thread");
    check(worker.connected(), "worker reports the connection");

    device->set(kGxVersion, Value(std::string("16.40.0003")));

    Builder read(Family::Automatic, Access::Read);
    read.query(kGxVersion.str());
    Request request;
    request.telegram = read.build();

    std::string version;
    worker.request(request, [&](LinkResult<Exchange> result) {
        if (!result || !result->reply) return;
        for (const Record& record : result->reply->records) {
            if (!record.empty()) version = std::get<std::string>(record.front());
        }
    });

    while (worker.pending() > 0) std::this_thread::yield();
    worker.drain();
    checkEq(version, std::string("16.40.0003"), "version came back through the worker");

    // The firmware the sequence needs is the firmware reported.
    auto parsed = Version::parse(version);
    check(parsed.has_value(), "reported version parses");
    if (parsed) {
        check(*parsed >= knownToken("SRW_UNIQUE_PCK_DATA_READY").since, "device is new enough for SW9B");
    }
}

}  // namespace

int main() {
    std::cout << "gxnet transport tests\n";

    testTokenConstants();
    testOpenAndClose();
    testReadAndWrite();
    testWriteThatDoesNotTake();
    testTimeoutAndFailure();
    testUniqueDataSequence();
    testCompositeTelegrams();
    testDialogVariants();
    testRemoteSoftkeys();
    testInternalErrorCodes();
    testDeviceQueries();
    testReadPlaceholder();
    testSpontaneous();
    testAnnotations();
    testScopedValueOf();
    testDialogQueries();
    testDialogResult();
    testReplayFromCapture();
    testWorkerThreading();

    std::cout << "\n" << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";
    return g_failures == 0 ? 0 : 1;
}
