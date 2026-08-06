// SPDX-License-Identifier: MIT
// gxdemo's own logic, tested without a window server.
//
// Session is the application's model: the connection, the log, the spontaneous
// listeners, the firmware gate. It includes no wxWidgets header, which is what
// makes this file possible -- it links one source file out of app/ against the
// mock transport and runs anywhere.
//
// The shape is deliberately the same as the other two suites: plain asserts, no
// framework, so a failure reads as a sentence rather than as a stack trace.
//
// The panels are not covered here. They need a running wxApp, which is a
// separate target with a separate set of problems.
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

#include "session.hpp"

using namespace gxdemo;

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

/// Drives the session until `done` or the budget runs out.
///
/// Everything in Session is delivered from `update()`, which is normally a
/// timer tick. Here it is a loop with a ceiling, so a callback that never fires
/// fails the test instead of hanging the suite.
bool pump(Session& session, const std::function<bool()>& done, int max_ticks = 2000) {
    for (int tick = 0; tick < max_ticks; ++tick) {
        session.update();
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    session.update();
    return done();
}

Session::Settings mockSettings() {
    Session::Settings settings;
    settings.kind = Session::Kind::Mock;
    settings.device = "test device";
    settings.spontaneous = true;
    return settings;
}

/// A connected session on the in-memory device, with the version seeded so the
/// firmware gate has something to compare against.
///
/// Seeded *before* connecting: Session reads SRT_GX_VERSION itself as soon as
/// the connection opens, and a mock that cannot answer would leave the version
/// unknown -- which is the case where the gate deliberately does nothing.
bool connectMock(Session& session, const std::string& version) {
    session.settings() = mockSettings();
    session.connect();
    if (!pump(session, [&] { return session.mock() != nullptr && session.connected(); })) return false;
    session.mock()->set("ST8D"_tok, Value{version});
    // Reconnect so the version read happens against the seeded device.
    session.disconnect();
    session.connect();
    if (!pump(session, [&] { return session.connected(); })) return false;
    session.mock()->set("ST8D"_tok, Value{version});
    return pump(session, [&] { return session.deviceVersion().has_value(); });
}

// --- tests ----------------------------------------------------------------

void testConnectAndVersion() {
    group("connect, and the version that follows");

    Session session;
    session.settings() = mockSettings();
    session.connect();
    check(pump(session, [&] { return session.connected(); }), "the mock connects");
    check(session.mock() != nullptr, "and the in-memory device is reachable for seeding");

    // Nothing seeded: the version read fails, and that is not an error. An
    // unknown firmware must leave the gate open rather than closed.
    check(!session.deviceVersion().has_value(), "an unanswerable version read leaves the version unknown");
    check(session.supports("SW9B"_tok).ok, "and with no version, every token is allowed through");

    session.disconnect();
    check(!session.connected(), "and it disconnects");
}

void testFirmwareGate() {
    group("firmware gate");

    Session session;
    check(connectMock(session, "15.20.0001"), "connected with a version the mock will answer");
    check(session.deviceVersion().has_value(), "the version came back");
    if (session.deviceVersion()) {
        checkEq(session.deviceVersion()->str(), std::string("15.20"), "and parsed");
    }

    // SRW_UNIQUE_PCK_DATA_READY is 16.40; a device below that answers
    // `fremdes Kommando` and nothing else.
    const Session::Supported ready = session.supports("SW9B"_tok);
    check(!ready.ok, "SW9B is refused on an older release");
    check(ready.reason.find("16.40") != std::string::npos, "the reason names the release it needs");
    check(ready.reason.find("15.20") != std::string::npos, "and the release the device reports");

    // And the refusal happens here rather than at the device.
    const std::size_t before = session.mock()->requests().size();
    bool called = false;
    bool failed = false;
    session.read("SW9B"_tok, [&](link::LinkResult<Value> result) {
        called = true;
        failed = !result.ok();
    });
    pump(session, [&] { return called; });
    check(called, "the callback still runs");
    check(failed, "with a failure");
    checkEq(session.mock()->requests().size(), before, "and nothing went to the device");

    // A token the firmware is new enough for goes through as usual.
    session.mock()->set("GW7D"_tok, Value{std::int16_t{0}});
    bool read_ok = false;
    session.read("GW7D"_tok, [&](link::LinkResult<Value> result) { read_ok = result.ok(); });
    check(pump(session, [&] { return read_ok; }), "GGW_UNIQUE_DATEN, 15.20, is not gated");
}

void testSpontaneousDelivery() {
    group("spontaneous records reach the listeners");

    Session session;
    check(connectMock(session, "15.20.0001"), "connected");

    std::vector<std::string> seen;
    const std::size_t token = session.listen([&](const link::Exchange& exchange) {
        for (const std::string& line : exchange.received) seen.push_back(line);
    });

    session.mock()->postSpontaneous({"A!WV63|WW60|1|WW68|0|WW64|11|WT62|Second|LX02"});
    check(pump(session, [&] { return !seen.empty(); }), "a queued record is collected by the idle poll");
    if (!seen.empty()) {
        check(seen.front().find("WV63") != std::string::npos, "and handed to the listener intact");
    }
    check(session.spontaneousCount() == 1, "counted once");
    check(session.pollCount() > 0, "and the poll counter moved");

    // Unsubscribing has to work from outside a callback and from inside one;
    // this is the outside case, and the inside case is what the dialog panel
    // does when its answer arrives.
    session.unlisten(token);
    seen.clear();
    session.mock()->postSpontaneous({"A!WV05|WW06|3|LX02"});
    pump(session, [&] { return false; }, 40);
    check(seen.empty(), "an unsubscribed listener hears nothing");
    check(session.spontaneousCount() == 2, "though the record was still collected and logged");
}

void testLogAnnotations() {
    group("the log says what the numbers mean");

    Session session;
    check(connectMock(session, "15.20.0001"), "connected");

    session.clearLog();
    session.mock()->postSpontaneous({"A!SV5B|SV5E|LW02|1891|SL8C|2|LX02|LX02"});
    check(pump(session, [&] { return session.spontaneousCount() > 0; }), "the record arrives");

    std::string detail;
    for (const Session::LogEntry& entry : session.log()) {
        if (entry.detail.find("LW02") != std::string::npos) detail = entry.detail;
    }
    check(!detail.empty(), "and its log entry carries a decoded detail");
    check(detail.find("GT63") != std::string::npos, "LGW_UFKENN 1891 shows as GT63");
    check(detail.find("GGT_SIMPLE_TXT3") != std::string::npos, "with its symbolic name");
    check(detail.find("SL8C = A") != std::string::npos, "and the channel bitmap as a channel letter");
}

void testLogWindow() {
    group("the log is bounded");

    Session session;
    session.setMaxLogEntries(10);
    for (int i = 0; i < 50; ++i) session.note("line " + std::to_string(i));

    const auto entries = session.log();
    checkEq(entries.size(), std::size_t{10}, "old entries are dropped");
    check(entries.back().text == "line 49", "and the newest is kept");

    // A cursor that has fallen behind the window must still return what
    // survives rather than nothing: the log view redraws from one.
    const Session::LogSlice slice = session.logSince(0);
    checkEq(slice.entries.size(), std::size_t{10}, "a stale cursor gets what is left");
    checkEq(slice.cursor, std::size_t{50}, "and a cursor that is now current");

    const Session::LogSlice nothing = session.logSince(slice.cursor);
    check(nothing.entries.empty(), "and asking again returns nothing new");
}

void testDescribeTelegram() {
    group("telegram descriptions");

    auto parsed = parseOneLine("A!GW7D|0");
    check(parsed.ok(), "a telegram parses");
    if (parsed) {
        const std::string described = describeTelegram(*parsed);
        check(described.find("GW7D") != std::string::npos, "the token appears");
        check(described.find("GGW_UNIQUE_DATEN") != std::string::npos, "with its symbolic name");
        check(described.find("15.20") != std::string::npos, "and the release it came from");
    }

    checkEq(tokenDescription("GL19"_tok).find("GGL_PLUNR"), std::size_t{0}, "a known token names itself");
    check(tokenDescription("WW62"_tok).empty(), "and one the reference does not name says nothing");
}

}  // namespace

int main() {
    std::cout << "gxdemo tests\n";

    testConnectAndVersion();
    testFirmwareGate();
    testSpontaneousDelivery();
    testLogAnnotations();
    testLogWindow();
    testDescribeTelegram();

    std::cout << "\n" << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";
    return g_failures == 0 ? 0 : 1;
}
