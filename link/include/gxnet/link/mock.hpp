// SPDX-License-Identifier: MIT
#ifndef GXNET_LINK_MOCK_HPP
#define GXNET_LINK_MOCK_HPP

#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "gxnet/link/transport.hpp"

namespace gxnet::link {

/// A device that exists only in memory.
///
/// It is not a simulation of a labeller and does not pretend to be one. It
/// models exactly one thing, which happens to be the thing worth testing
/// against: a device remembers what was written to it and reports that back on
/// a read. Sequences built on write-then-verify can therefore be exercised end
/// to end -- including the failure paths -- without a line, and the same code
/// runs unchanged against `BcsTransport`.
///
/// What it deliberately does not do:
///
///   * Invent acknowledgements. The shape of a reply to a write is not
///     documented, so a write answers with nothing at all.
///   * Invent values. A read of a token that was never seeded and never written
///     fails, rather than returning a plausible zero. A gap that shows up as an
///     error in a test is worth more than one papered over with a default.
///
/// The one assumption it does make is recorded at `Options::echo_header`: that
/// a read is answered by the requested header plus the data. That follows the
/// two-argument `Receive(out header, out data, ...)` in the manual, but it has
/// not been confirmed against the device. When it is, this is the single place
/// to correct.
class MockTransport final : public Transport {
public:
    struct Options {
        /// Answer a read with the request's own header followed by the values.
        /// See the class comment: an assumption, isolated here on purpose.
        bool echo_header = true;

        /// Artificial delay per exchange, for exercising timeout handling.
        std::chrono::milliseconds latency{0};

        /// Reported by `textMode()`. The user's device runs with "Device uses
        /// unicode" enabled.
        std::optional<TextMode> text_mode = TextMode::UnicodeDevice;

        std::string device_description = "mock device";
    };

    MockTransport() = default;
    explicit MockTransport(Options options) : options_(std::move(options)) {}

    // --- seeding ----------------------------------------------------------

    /// Gives the device a value to report for `token`.
    void set(Token token, Value value);
    [[nodiscard]] std::optional<Value> get(Token token) const;

    /// Seeds every value carried by a telegram -- the natural way to load a
    /// decoded capture into the model, so reads return what the real device
    /// actually reported at that moment.
    void seedFrom(const Telegram& telegram);

    /// Takes over one token entirely. The handler sees each node addressed to
    /// it along with the access direction, and returns the value to report
    /// (reads) or nullopt (writes, and reads it wants to fail).
    ///
    /// This is how an undocumented subfunction is stood in for: `SW9B` is
    /// either a readiness flag or a remaining-code count, nobody knows which,
    /// so a test states which behaviour it is exercising instead of the mock
    /// picking one.
    using Handler = std::function<std::optional<Value>(const Node&, Access)>;
    void handle(Token token, Handler handler);

    /// Replays a recorded answer verbatim for a given request header, ignoring
    /// the model. For driving tests off a real capture.
    void replay(std::string header_line, std::vector<std::string> reply_lines);

    /// Makes the next exchange fail, once. For the error paths.
    void failNext(LinkError error);
    /// Makes the next exchange time out, once.
    void timeoutNext();

    /// Queues a record for the device to send of its own accord.
    ///
    /// The next `receiveSpontaneous` hands it back; the one after that reports a
    /// timeout again, because a queue that keeps replaying the same record would
    /// make a listener look like it works when it is only being fed.
    ///
    /// The only way to exercise a listener: the real path depends on a device
    /// deciding to send something, which no test can arrange.
    void postSpontaneous(std::vector<std::string> lines);
    /// The same from a telegram, encoded the way the server hands answers back:
    /// one interleaved line, values following their tokens.
    void postSpontaneous(const Telegram& telegram);
    [[nodiscard]] std::size_t spontaneousPending() const { return spontaneous_.size(); }

    // --- inspection -------------------------------------------------------

    /// Every line handed to the transport, in order.
    [[nodiscard]] const std::vector<std::string>& sent() const { return sent_; }
    /// Every request, parsed, in order.
    [[nodiscard]] const std::vector<Request>& requests() const { return requests_; }
    /// Tokens written to, in order, with duplicates -- the write history is
    /// what a sequence test asserts against.
    [[nodiscard]] const std::vector<std::pair<Token, Value>>& writes() const { return writes_; }
    void clearHistory();

    [[nodiscard]] Options& options() { return options_; }

    // --- Transport --------------------------------------------------------

    LinkError open(const Endpoint& endpoint) override;
    void close() override;
    [[nodiscard]] bool isOpen() const override { return open_; }
    LinkResult<Exchange> execute(const Request& request) override;
    LinkResult<Exchange> receiveSpontaneous(const std::string& queue, std::chrono::milliseconds timeout) override;
    [[nodiscard]] std::optional<TextMode> textMode() const override { return options_.text_mode; }
    [[nodiscard]] std::string description() const override;

private:
    LinkResult<Exchange> answerRead(const Request& request, const std::vector<std::string>& sent);
    LinkResult<Exchange> answerWrite(const Request& request, const std::vector<std::string>& sent);

    Options options_{};
    Endpoint endpoint_{};
    bool open_ = false;

    std::map<Token, Value> values_;
    std::map<Token, Handler> handlers_;
    std::map<std::string, std::vector<std::string>> replays_;

    std::vector<std::string> sent_;
    std::vector<Request> requests_;
    std::vector<std::pair<Token, Value>> writes_;

    std::optional<LinkError> fail_next_;
    bool timeout_next_ = false;

    std::deque<std::vector<std::string>> spontaneous_;
};

}  // namespace gxnet::link

#endif  // GXNET_LINK_MOCK_HPP
