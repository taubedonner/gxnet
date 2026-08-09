// SPDX-License-Identifier: MIT
#include "gxnet/link/bcs.hpp"

#ifndef _WIN32

namespace gxnet::link {

BcsTransport::~BcsTransport() = default;

bool BcsTransport::available() { return false; }

std::unique_ptr<BcsTransport> BcsTransport::create(Options) { return nullptr; }

std::unique_ptr<BcsTransport> BcsTransport::create() { return nullptr; }

}  // namespace gxnet::link

#else  // _WIN32

#include <algorithm>
#include <chrono>
#include <utility>

#include "dispatch.hpp"
#include "gxnet/link/operations.hpp"

namespace gxnet::link {
namespace {

using Clock = std::chrono::steady_clock;

/// Adds what the server's wording does not say out loud.
///
/// A device that is switched off or off the network does not produce a clean
/// "not reachable". The first thing the server does on a send is ask the device
/// about its Unicode setting, so that is what fails, and the message reads
///
///     Senden der Anfrage der Unicodeeinstellung des Geraetes fehlgeschlagen
///
/// which sounds like a text-encoding fault and is nothing of the kind. It is
/// the server's normal way of saying "nobody answered".
std::string explain(const std::string& message) {
    if (message.find("Unicodeeinstellung") != std::string::npos) {
        return message +
               "  [an unreachable device looks exactly like this: check that it "
               "is switched on and on the network]";
    }
    // "foreign command": the device does not know this subfunction. Almost
    // always a version gap -- the reference records when each one appeared, and
    // a device older than that simply has no such command. The server reports
    // it as error 4, which is LGW_RETURN 4 "third-party command".
    if (message.find("fremdes Kommando") != std::string::npos) {
        return message +
               "  [the device has no such subfunction: check the release it was "
               "introduced in against SRT_GX_VERSION (ST8D)]";
    }
    // Error 2154, and the number is the device's own LGW_RETURN rather than
    // anything of the server's: the command reached the device and the device
    // answered "communication error".
    if (message.find("Fehler bei Kommunikation") != std::string::npos) {
        return message +
               "  [LGW_RETURN 2154, from the device: the subfunction exists and "
               "the device refused it, which is not the same as not knowing it]";
    }
    return message;
}

/// BCS reports transfer state in lStatus; the return value of the call itself
/// is a separate thing and non-zero means the call failed.
Status statusFrom(std::int32_t value) {
    switch (value) {
        case 1: return Status::Timeout;
        case 2: return Status::MoreData;
        default: return Status::Ok;
    }
}

class BcsTransportImpl final : public BcsTransport {
public:
    explicit BcsTransportImpl(Options options) : options_(std::move(options)) {}

    ~BcsTransportImpl() override { close(); }

    LinkError onThreadStart() override { return apartment_.enter(); }

    void onThreadStop() override {
        close();
        object_.reset();
        apartment_.leave();
    }

    LinkError open(const Endpoint& endpoint) override {
        if (endpoint.device.empty()) {
            return {0, "no device name; use the system name from _connectConfig"};
        }
        if (open_) close();

        if (!object_.valid()) {
            if (LinkError error = object_.create(options_.prog_id)) return error;

            // Version .1 is the only complete interface. Finding out now beats
            // finding out when a send silently has nowhere to go.
            if (!object_.has("SendOne") || !object_.has("ReceiveOne")) {
                object_.reset();
                return {0, options_.prog_id +
                               " has no SendOne/ReceiveOne; this is one of the "
                               "stripped interface versions: use "
                               "BCS.BCSComunnication.1"};
            }
        }

        win::Args args;
        args.in(endpoint.user)
            .in(endpoint.device)
            .inShort(endpoint.spontaneous ? 1 : 0)
            .inShort(endpoint.exclusive ? 1 : 0)
            .inShort(endpoint.light_licence ? 1 : 0);

        std::int32_t result = 0;
        if (LinkError error = object_.call("Open", args, &result)) {
            error.message = explain(error.message);
            return error;
        }
        if (result != 0) {
            return {result, "Open(\"" + endpoint.device + "\") refused: " + serverError()};
        }

        endpoint_ = endpoint;
        open_ = true;

        text_mode_.reset();
        probe_error_.clear();
        if (options_.probe_text_mode) probeTextMode();

        return {};
    }

    void close() override {
        if (!open_ || !object_.valid()) {
            open_ = false;
            return;
        }
        win::Args args;
        std::int32_t result = 0;
        object_.call("Close", args, &result);
        open_ = false;
    }

    [[nodiscard]] bool isOpen() const override { return open_; }

    [[nodiscard]] std::optional<TextMode> textMode() const override { return text_mode_; }

    [[nodiscard]] std::string description() const override {
        if (!open_) return options_.prog_id + " (closed)";
        return options_.prog_id + " -> " + endpoint_.device;
    }

    std::string lastServerError() override {
        std::string out = serverError();
        if (!probe_error_.empty()) {
            if (!out.empty()) out += " | ";
            out += "IsUnicodeDevice probe: " + probe_error_;
        }
        return out;
    }

    LinkResult<Exchange> execute(const Request& request) override {
        if (!open_) {
            return LinkResult<Exchange>::fail({0, "transport is not open"});
        }

        const auto started = Clock::now();

        Exchange exchange;
        std::string handle;
        std::int32_t status = 0;
        std::int32_t result = 0;

        if (request.one_line) {
            // Header and data in one string, with the separator the server
            // splits on. Not the interleaved form: that has no boundary between
            // the two parts, which is exactly what SeperteHeaderData failed on.
            auto lines = encodeLines(request.telegram);
            if (!lines || lines->empty()) {
                return LinkResult<Exchange>::fail({0, "cannot encode telegram: " + lines.error.message});
            }
            std::string line = lines->front();
            for (std::size_t i = 1; i < lines->size(); ++i) {
                line += options_.send_one_separator;
                line += (*lines)[i];
            }
            exchange.sent.push_back(line);

            win::Args args;
            args.in(line).outString(handle).inLong(static_cast<std::int32_t>(request.timeout.count())).outLong(status);
            if (LinkError error = object_.call("SendOne", args, &result)) {
                error.message = explain(error.message);
                return LinkResult<Exchange>::fail(error);
            }
        } else {
            auto lines = encodeLines(request.telegram);
            if (!lines) {
                return LinkResult<Exchange>::fail({0, "cannot encode telegram: " + lines.error.message});
            }
            if (lines->empty()) {
                return LinkResult<Exchange>::fail({0, "nothing to send"});
            }
            exchange.sent = *lines;

            // Send takes header and data separately. Multi-record telegrams go
            // out as one data argument with the records newline separated,
            // which is the form the manual shows for file exports.
            std::string data;
            for (std::size_t i = 1; i < lines->size(); ++i) {
                if (i > 1) data += "\r\n";
                data += (*lines)[i];
            }
            if (data.empty()) {
                data = readPlaceholderData(request.telegram.header);
                if (!data.empty()) exchange.sent.push_back(data);
            }

            win::Args args;
            args.in(lines->front())
                .in(data)
                .outString(handle)
                .inLong(static_cast<std::int32_t>(request.timeout.count()))
                .outLong(status);
            if (LinkError error = object_.call("Send", args, &result)) {
                error.message = explain(error.message);
                return LinkResult<Exchange>::fail(error);
            }
        }

        if (result != 0) {
            return LinkResult<Exchange>::fail({result, "send failed: " + serverError()});
        }

        exchange.status = statusFrom(status);
        exchange.send_status = exchange.status;

        // A send that timed out has not necessarily failed: it is still on the
        // server's transmission list. The vendor's own sample asks again with
        // SendCheck and, if that times out too, cancels with Reset -- and the
        // cancelling matters. An abandoned request that the device answers
        // later has no send request left to belong to, so the answer arrives as
        // a spontaneous record instead, on a channel nobody here is reading.
        // Leaving those behind is how a long-running process accumulates state
        // on the server it cannot see.
        if (exchange.status == Status::Timeout && !handle.empty()) {
            recover(request, handle, exchange);
        }

        // `lStatus == 2` is the server saying there is something to receive, and
        // the vendor's own samples treat it as the only condition: the C++ one
        // writes `if (lStatus == 2) // data to receive`, and the C# one loops
        // `while (lState == NEXT)`. Both then receive, and otherwise do not.
        //
        // Ours also receives on `expect_reply` alone, and that is deliberate
        // rather than an oversight: reads work today, and if a read's send
        // reports 0 here, following the samples exactly would stop answering
        // them. What the samples do settle is the other direction -- data
        // waiting must be collected whatever the caller asked for, or it is
        // still in the queue when the next request reads its own answer.
        const bool waiting = exchange.status == Status::MoreData;
        if ((request.expect_reply || waiting) && exchange.status != Status::Timeout) {
            if (LinkError error = receiveAll(handle, request.timeout, exchange)) {
                return LinkResult<Exchange>::fail(error);
            }
        }

        exchange.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started);
        return LinkResult<Exchange>::of(std::move(exchange));
    }

    /// Collects one spontaneous record, by receiving against a queue name
    /// instead of a send handle.
    ///
    /// `ReceiveOne`'s `szHandle` reads as a send handle, but the description of
    /// `ReceiveOneWithoutAck` gives the same parameter a second meaning: a
    /// queue name. The two differ only in who acknowledges, so it means the
    /// same in both. See `docs/bcs-notes.md`, section 1 for the passages.
    ///
    /// `ReceiveOne` is the one used here because it acknowledges by itself: an
    /// unacknowledged record leaves the device waiting, and on a labelling line
    /// that is not a small thing.
    ///
    /// `CreateReceiveQueue` and `SetReceiveQueueFilter` would let us sort
    /// arrivals into queues by leading token. Deliberately not used: one queue
    /// that receives everything cannot lose a record to a filter written for the
    /// wrong token, and until we know what this device actually sends, not
    /// losing anything is worth more than sorting.
    LinkResult<Exchange> receiveSpontaneous(const std::string& queue, std::chrono::milliseconds timeout) override {
        if (!object_.valid()) {
            return LinkResult<Exchange>::fail({0, "not connected"});
        }
        if (!object_.has("ReceiveOne")) {
            return LinkResult<Exchange>::fail({0, options_.prog_id + " has no ReceiveOne"});
        }

        const auto started = Clock::now();
        Exchange exchange;
        if (LinkError error = receiveAll(queue.empty() ? std::string{kDefaultQueue} : queue, timeout, exchange)) {
            return LinkResult<Exchange>::fail(error);
        }
        exchange.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started);
        return LinkResult<Exchange>::of(std::move(exchange));
    }

    LinkResult<CallResult> call(const std::string& method, const std::vector<CallArg>& args) override {
        if (!object_.valid()) {
            return LinkResult<CallResult>::fail({0, "no automation object yet: connect first"});
        }
        if (!object_.has(method)) {
            return LinkResult<CallResult>::fail({0, options_.prog_id + " does not expose " + method});
        }

        // Out parameters are bound to storage that has to outlive the call, and
        // win::Args keeps pointers into it -- hence the pre-sized vectors
        // rather than anything that could reallocate while being filled.
        std::vector<std::string> text(args.size());
        std::vector<std::int32_t> number(args.size());
        std::vector<std::int16_t> word(args.size());

        win::Args list;
        for (std::size_t i = 0; i < args.size(); ++i) {
            switch (args[i].kind) {
                case CallArg::Kind::InText: list.in(args[i].text); break;
                case CallArg::Kind::InLong: list.inLong(args[i].number); break;
                case CallArg::Kind::InShort: list.inShort(static_cast<std::int16_t>(args[i].number)); break;
                case CallArg::Kind::OutText: list.outString(text[i]); break;
                case CallArg::Kind::OutLong: list.outLong(number[i]); break;
                case CallArg::Kind::OutShort: list.outShort(word[i]); break;
            }
        }

        std::int32_t returned = 0;
        if (LinkError error = object_.call(method, list, &returned)) {
            error.message = explain(error.message);
            const std::string server = serverError();
            if (!server.empty()) error.message += " | " + server;
            return LinkResult<CallResult>::fail(error);
        }

        CallResult out;
        out.result = returned;
        out.arguments.reserve(args.size());
        for (std::size_t i = 0; i < args.size(); ++i) {
            switch (args[i].kind) {
                case CallArg::Kind::InText: out.arguments.push_back(args[i].text); break;
                case CallArg::Kind::InLong:
                case CallArg::Kind::InShort: out.arguments.push_back(std::to_string(args[i].number)); break;
                case CallArg::Kind::OutText: out.arguments.push_back(text[i]); break;
                case CallArg::Kind::OutLong: out.arguments.push_back(std::to_string(number[i])); break;
                case CallArg::Kind::OutShort: out.arguments.push_back(std::to_string(word[i])); break;
            }
        }
        // Only when the call reported a failure. `Error` hands back whatever
        // the object recorded last, with no indication of when -- so asking
        // after a call that succeeded attaches a stale message from some
        // earlier failure to a result that had nothing wrong with it. A
        // non-zero return leaves its reason here and nowhere else, which is the
        // case worth asking in.
        if (returned != 0) out.server_error = serverError();
        return LinkResult<CallResult>::of(std::move(out));
    }

private:
    /// SendCheck, then Reset: what to do with a send that timed out.
    ///
    /// Best effort by design. Every outcome here is already a failure being
    /// reported, and a Reset that itself fails must not replace the timeout
    /// with something less informative.
    void recover(const Request& request, const std::string& handle, Exchange& exchange) {
        if (!object_.has("SendCheck")) return;

        // Not the request's own timeout. SendCheck asks whether a send that is
        // already pending has finished, which is a question about the server's
        // own state, not about the device -- and the request timeout can be
        // minutes: a dialog waits for a person. Spending that again, on the one
        // thread everything else queues behind, to recover from a timeout the
        // caller has already been told about, is worse than not recovering.
        const auto wait = std::min(request.timeout, std::chrono::milliseconds{1000});

        std::int32_t status = 0;
        std::int32_t result = 0;
        win::Args args;
        args.in(handle).inLong(static_cast<std::int32_t>(wait.count())).outLong(status);
        if (!object_.call("SendCheck", args, &result) && result == 0) {
            exchange.status = statusFrom(status);
            if (exchange.status != Status::Timeout) return;
        }

        if (!object_.has("Reset")) return;
        win::Args reset;
        reset.in(handle);
        std::int32_t reset_result = 0;
        object_.call("Reset", reset, &reset_result);
        exchange.reset_after_timeout = true;
    }

    /// Receives until the server stops reporting "more data".
    ///
    /// Status 2 is how a multi-record answer arrives -- a buffer read, a file
    /// export. Stopping at the first record would quietly truncate it.
    LinkError receiveAll(const std::string& handle, std::chrono::milliseconds timeout, Exchange& exchange) {
        for (int round = 0; round < options_.max_receive_rounds; ++round) {
            std::string payload;
            std::int32_t status = 0;
            std::int32_t result = 0;

            win::Args args;
            args.outString(payload).in(handle).inLong(static_cast<std::int32_t>(timeout.count())).outLong(status);
            if (LinkError error = object_.call("ReceiveOne", args, &result)) {
                error.message = explain(error.message);
                return error;
            }
            if (result != 0) {
                return {result, "receive failed: " + serverError()};
            }

            exchange.status = statusFrom(status);
            if (!payload.empty()) splitInto(payload, exchange.received);

            if (exchange.status == Status::Timeout) break;
            if (exchange.status != Status::MoreData) break;
        }

        if (!exchange.received.empty()) parseReply(exchange);
        return {};
    }

    /// Turns what ReceiveOne handed back into a telegram.
    ///
    /// The answer comes interleaved: header and values in one line, values
    /// following their tokens --
    ///
    ///     A?ST8D   ->   A!ST8D|00.00.0000
    ///
    /// which is what the manual means by szHeaderData being "a combination of
    /// header and payload data". Note the asymmetry with sending: we hand the
    /// two parts to Send separately, and get them back joined. Reading the
    /// answer as header-plus-data lines makes the parser take the version for
    /// a token and reject the whole reply.
    ///
    /// Multi-record answers still arrive as several lines, so that form is
    /// tried as well; whichever parses wins.
    static void parseReply(Exchange& exchange) {
        if (exchange.received.size() == 1) {
            auto one = parseOneLine(exchange.received.front());
            if (one) {
                exchange.reply = *one;
                return;
            }
            auto lines = parseLines(exchange.received);
            if (lines) {
                exchange.reply = *lines;
                return;
            }
            // Report the interleaved failure: that is the form we expect, so
            // its complaint is the informative one.
            exchange.reply_error = one.error;
            return;
        }

        auto lines = parseLines(exchange.received);
        if (lines) {
            exchange.reply = *lines;
            return;
        }
        auto one = parseOneLine(exchange.received.front());
        if (one) {
            exchange.reply = *one;
            return;
        }
        // Keep the raw lines and say why they did not parse: an answer we
        // cannot read is still evidence about the device.
        exchange.reply_error = lines.error;
    }

    static void splitInto(const std::string& text, std::vector<std::string>& out) {
        std::string current;
        for (const char ch : text) {
            if (ch == '\n' || ch == '\r') {
                if (!current.empty()) {
                    out.push_back(current);
                    current.clear();
                }
                continue;
            }
            current.push_back(ch);
        }
        if (!current.empty()) out.push_back(current);
    }

    void probeTextMode() {
        if (!object_.has("IsUnicodeDevice")) return;

        std::int16_t unicode = 0;
        win::Args args;
        args.outShort(unicode);
        std::int32_t result = 0;
        if (LinkError error = object_.call("IsUnicodeDevice", args, &result)) {
            // Not swallowed any more. When this probe went wrong it took the
            // next SendOne down with it, and a silent failure here made that
            // look like a fault in the send.
            probe_error_ = error.str();
            const std::string server = serverError();
            if (!server.empty()) probe_error_ += "; " + server;
            return;
        }

        // The out parameter is the answer; some builds report through the
        // return value instead, so a non-zero either way counts.
        const bool is_unicode = unicode != 0 || result == 1;
        text_mode_ = is_unicode ? TextMode::UnicodeDevice : TextMode::CodepageDevice;
    }

    /// Reads the server's own account of the last failure.
    ///
    /// `Error` returns nothing and reports through three out parameters; the
    /// PowerShell signature `void Error (int, int, string)` hides that.
    std::string serverError() {
        if (!object_.valid() || !object_.has("Error")) return {};

        std::int32_t code = 0;
        std::int32_t detail = 0;
        std::string text;

        win::Args args;
        args.outLong(code).outLong(detail).outString(text);
        if (object_.call("Error", args, nullptr)) return {};

        if (text.empty() && code == 0 && detail == 0) return {};
        std::string out = text.empty() ? std::string("server reported an error") : text;
        out += " [" + std::to_string(code) + "/" + std::to_string(detail) + "]";
        return out;
    }

    Options options_;
    Endpoint endpoint_{};
    win::Apartment apartment_;
    win::Dispatch object_;
    bool open_ = false;
    std::optional<TextMode> text_mode_;
    std::string probe_error_;
};

}  // namespace

BcsTransport::~BcsTransport() = default;

bool BcsTransport::available() { return true; }

std::unique_ptr<BcsTransport> BcsTransport::create(Options options) {
    return std::make_unique<BcsTransportImpl>(std::move(options));
}

std::unique_ptr<BcsTransport> BcsTransport::create() { return create(Options{}); }

}  // namespace gxnet::link

#endif  // _WIN32
