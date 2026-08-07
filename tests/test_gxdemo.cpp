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
#include <algorithm>
#include <chrono>
#include <cstdlib>
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
bool pump(Session& session, const std::function<bool()>& done,
          std::chrono::milliseconds budget = std::chrono::seconds{5}) {
    // Bounded by the clock, not by a tick count. A tick budget is a guess about
    // how fast the machine is, and the guess is wrong by an order of magnitude
    // between a native build and a sanitized one on a shared runner: the same
    // loop that returns in microseconds here spins for minutes there.
    //
    // The sleep is short but real. Yielding in a tight loop keeps a core busy
    // fighting the very thread the loop is waiting for.
    // Yield rather than sleep: every record here needs its own round trip
    // through the worker, driven by update(), and a sleep of even 200 us costs
    // more than the work because the platform rounds it up to a timer tick.
    // The clock bound is what keeps a yield loop honest when the work never
    // arrives.
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
        session.update();
        if (done()) return true;
        std::this_thread::yield();
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
/// The seeding happens between `connect()` and the first `update()`, and the
/// gap matters. `connect()` builds the transport and queues the open; the
/// version read is queued later still, by the open's own completion, which runs
/// on a call to `update()`. Seeding here therefore always precedes the read.
/// Seeding after a pump does not: the read may already have run and found
/// nothing, and Session does not ask twice.
bool connectMock(Session& session, const std::string& version) {
    session.settings() = mockSettings();
    session.connect();
    if (session.mock() == nullptr) return false;
    session.mock()->set("ST8D"_tok, Value{version});
    return pump(session, [&] { return session.connected() && session.deviceVersion().has_value(); });
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
    check(pump(session, [&] { return session.spontaneousCount() == 2; }),
          "a record posted after unsubscribing is still collected and logged");
    check(seen.empty(), "but the unsubscribed listener does not hear it");
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

/// Connect, work, disconnect, repeatedly, and watch what does not come back.
///
/// The failure this exists for is the one a unit test cannot see: a service
/// left running for months. Anything that grows once per cycle is invisible in
/// a single pass and fatal in a hundred thousand of them.
///
/// It asserts on the containers rather than on process memory, because resident
/// size is the wrong measurement -- an allocator claims arenas and keeps them,
/// so RSS rises and then plateaus even when nothing leaks. What must be flat is
/// what this code owns: the log against its ceiling, the listeners against
/// their subscriptions, the pending queue against zero.
///
/// GXNET_SOAK_CYCLES raises the count for a real soak; the default keeps the
/// suite under a second.
void testSoak() {
    group("soak");

    int cycles = 60;
#ifdef _MSC_VER
    // getenv is deprecated by MSVC rather than unsafe here: the result is read
    // immediately and never stored.
#pragma warning(suppress : 4996)
    const char* env = std::getenv("GXNET_SOAK_CYCLES");
#else
    const char* env = std::getenv("GXNET_SOAK_CYCLES");
#endif
    if (env != nullptr) {
        const int wanted = std::atoi(env);
        if (wanted > 0) cycles = wanted;
    }

    Session session;
    session.setMaxLogEntries(200);

    std::size_t listener_calls = 0;
    const std::size_t listener = session.listen([&](const link::Exchange&) { ++listener_calls; });

    bool clean = true;
    std::size_t log_high_water = 0;

    for (int cycle = 0; cycle < cycles && clean; ++cycle) {
        session.settings() = mockSettings();
        session.connect();
        if (!pump(session, [&] { return session.connected() && session.mock() != nullptr; })) {
            clean = false;
            break;
        }

        session.mock()->set("GW7D"_tok, Value{std::int16_t{0}});
        for (int i = 0; i < 4; ++i) {
            session.read("GW7D"_tok, [](link::LinkResult<Value>) {});
            session.mock()->postSpontaneous({"A!WV63|WW60|1|WW68|0|LX02"});
        }
        pump(session, [&] { return session.pending() == 0 && session.spontaneousCount() >= 4; });

        session.disconnect();
        session.update();

        // Every cycle ends where it started. A queue that does not drain, or a
        // request that is never retired, shows up here on the second lap rather
        // than after a month of running.
        if (session.pending() != 0) clean = false;
        if (session.connected()) clean = false;
        log_high_water = std::max(log_high_water, session.log().size());
    }

    check(clean, "every cycle ends idle and disconnected");
    check(log_high_water <= 200, "the log never exceeds its ceiling");
    check(listener_calls >= static_cast<std::size_t>(cycles), "the listener kept being called throughout");

    session.unlisten(listener);
    session.clearLog();
    checkEq(session.log().size(), std::size_t{0}, "and everything can still be released at the end");
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
    testSoak();

    std::cout << "\n" << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";
    return g_failures == 0 ? 0 : 1;
}
