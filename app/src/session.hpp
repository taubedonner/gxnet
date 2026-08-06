// SPDX-License-Identifier: MIT
#pragma once

#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "gxnet/gxnet.hpp"
#include "gxnet/link/logging.hpp"
#include "gxnet/link/mock.hpp"
#include "gxnet/link/worker.hpp"

namespace gxdemo {

using namespace gxnet;

/// Everything the panels share: one connection, its settings, and the log.
///
/// The transport runs on its own thread (link::Worker); every callback handed
/// to the methods below is invoked from `update()`, i.e. on the UI thread, so
/// panels can write straight into their own state without locking.
class Session {
public:
    enum class Kind {
        Mock,  ///< in-memory device; works everywhere, including this Mac
        Bcs,   ///< the real thing, through _connect.BRAIN. Windows only.
    };

    struct Settings {
        Kind kind = Kind::Mock;
        /// The system name as configured in _connectConfig. No default: it
        /// is installation specific, and the panel remembers what is typed.
        std::string device;
        std::string user = "gxnet";
        std::string prog_id = "BCS.BCSComunnication.1";
        int timeout_ms = 3000;
        /// Neither is safe alongside another client, so both stay off unless the
        /// operator deliberately turns them on.
        bool spontaneous = false;
        bool exclusive = false;
        /// Call IsUnicodeDevice after opening. On, because it settles escaping
        /// before the first telegram rather than after the first mangled label.
        bool probe_text_mode = true;
        /// Use SendOne (header and data in one string) instead of Send (the two
        /// passed separately). Off; see Request::one_line.
        bool use_send_one = false;

        /// Poll the receive queue for records the device sent on its own.
        ///
        /// Independent of `spontaneous`, which is what the connection asks the
        /// server for. Nothing will arrive without that flag, but keeping the
        /// two apart means the listener can be turned off while the
        /// subscription stays, which is what you want when the log is being
        /// read and every poll is a line in it.
        bool listen = true;

        /// How long each poll waits inside ReceiveOne. Short on purpose: the
        /// transport has one thread and every request queues behind this.
        int listen_timeout_ms = 150;

        /// Queue to receive against. `DUSTBIN` is where the server files
        /// spontaneous records for a client that made no queue of its own.
        std::string listen_queue = link::Transport::kDefaultQueue;
    };

    struct LogEntry {
        enum class Kind { Sent, Received, Note, Error };

        Kind kind = Kind::Note;
        std::string text;
        /// Annotation shown beside the line: token names, decoded values.
        std::string detail;
        std::chrono::system_clock::time_point at{};
        std::chrono::milliseconds elapsed{0};
    };

    Session();
    ~Session();

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    // --- lifecycle --------------------------------------------------------

    /// Builds the transport named by the settings and opens it. Disconnects
    /// first if already connected.
    void connect();
    void disconnect();

    /// Delivers everything the transport thread has finished. Call once a frame.
    void update();

    [[nodiscard]] bool connected() const;
    [[nodiscard]] bool busy() const;
    [[nodiscard]] std::size_t pending() const;
    [[nodiscard]] const std::string& status() const { return status_; }
    [[nodiscard]] std::optional<TextMode> textMode() const { return text_mode_; }

    /// `SRT_GX_VERSION`, read once after the connection opens.
    ///
    /// Every registry entry carries the release that introduced it, and a device
    /// older than that answers `fremdes Kommando`. Knowing the firmware turns
    /// that into a sentence before anything is sent. Empty until the read comes
    /// back, and on a device whose version does not parse.
    [[nodiscard]] std::optional<Version> deviceVersion() const { return device_version_; }

    /// Whether this device is new enough for `token`, and why not when it is
    /// not. `ok == true` with an empty reason when the version is unknown: an
    /// unanswered question is not grounds for refusing to try.
    struct Supported {
        bool ok = true;
        std::string reason;
    };
    [[nodiscard]] Supported supports(Token token) const;

    [[nodiscard]] Settings& settings() { return settings_; }
    [[nodiscard]] const Settings& settings() const { return settings_; }

    /// The in-memory device, when that is what is connected. Panels use it to
    /// seed values so the UI can be exercised without a line.
    [[nodiscard]] link::MockTransport* mock() { return mock_; }

    // --- operations -------------------------------------------------------

    using ValueCallback = std::function<void(link::LinkResult<Value>)>;
    using ExchangeCallback = std::function<void(link::LinkResult<link::Exchange>)>;

    /// Reads one subfunction.
    void read(Token token, ValueCallback done);

    /// Writes one subfunction and reads it back. The callback reports what the
    /// device says afterwards, which is the only thing that establishes the
    /// write took.
    void write(Token token, Value value, ValueCallback done);

    /// Sends a telegram as built by the caller.
    void send(Telegram telegram, bool expect_reply, ExchangeCallback done);

    /// The same, with a timeout of its own rather than the connection's.
    ///
    /// For the case where the device is waiting on a person. The worker thread
    /// is occupied for the whole wait and everything else queues behind it.
    void send(Telegram telegram, bool expect_reply, std::chrono::milliseconds wait, ExchangeCallback done);

    /// Sends a raw wire line, as typed into the console.
    void sendRaw(const std::string& line, bool expect_reply, ExchangeCallback done);

    using CallCallback = std::function<void(link::LinkResult<link::CallResult>)>;

    /// Calls a method on the automation object directly, bypassing telegrams.
    ///
    /// For the undocumented parts of the BCS surface -- the FTP family above
    /// all. Logged like an exchange, because a file appearing or disappearing
    /// on the device is exactly the kind of thing that has to be in the record
    /// beside the telegrams that surround it.
    void call(std::string method, std::vector<link::CallArg> args, CallCallback done);

    // --- spontaneous records ----------------------------------------------

    /// Notified for every record the device sent of its own accord: a dialog
    /// result, a softkey press, a package record.
    ///
    /// Runs on the UI thread, from `update()`, like every other callback here.
    /// Subscribers are not told which record is "theirs" -- a listener wanting
    /// only its own telegram checks the leading token itself. Filtering
    /// centrally would mean deciding in advance what a device can send, and
    /// that is precisely what is not known.
    using SpontaneousCallback = std::function<void(const link::Exchange&)>;
    /// Returns a token to pass to `unlisten`.
    std::size_t listen(SpontaneousCallback callback);
    void unlisten(std::size_t token);

    /// Records collected since the connection opened. Shown in the UI so a
    /// silent channel is distinguishable from one nobody is watching.
    [[nodiscard]] std::size_t spontaneousCount() const { return spontaneous_count_; }
    /// Polls attempted, whether or not anything came back.
    [[nodiscard]] std::size_t pollCount() const { return poll_count_; }

    // --- log --------------------------------------------------------------

    /// Copy of the log, newest last. Copied rather than exposed because the
    /// transport thread appends to it.
    [[nodiscard]] std::vector<LogEntry> log() const;

    /// Everything appended since `cursor`, plus the cursor to pass next time.
    ///
    /// The view redraws several times a second; copying the whole log each time
    /// would make a long session progressively slower for no reason. Entries
    /// are numbered globally, so a cursor stays meaningful after old ones are
    /// dropped -- if it has fallen behind the window, what survives is returned.
    struct LogSlice {
        std::vector<LogEntry> entries;
        std::size_t cursor = 0;
    };
    [[nodiscard]] LogSlice logSince(std::size_t cursor) const;
    void clearLog();
    void note(std::string text, std::string detail = {});

    [[nodiscard]] std::size_t maxLogEntries() const { return max_log_; }
    void setMaxLogEntries(std::size_t count) { max_log_ = count; }

private:
    std::unique_ptr<link::Transport> buildTransport();
    void append(LogEntry entry);
    void onExchange(const link::LoggingTransport::Event& event);
    /// Posts one poll if the conditions for polling hold. Called from update().
    void pumpSpontaneous();
    void deliverSpontaneous(const link::Exchange& exchange);
    [[nodiscard]] std::chrono::milliseconds timeout() const { return std::chrono::milliseconds{settings_.timeout_ms}; }

    Settings settings_{};
    std::unique_ptr<link::Worker> worker_;
    /// Non-owning; valid while the mock transport is the connected one.
    link::MockTransport* mock_ = nullptr;

    std::string status_ = "not connected";
    std::optional<TextMode> text_mode_;
    std::optional<Version> device_version_;

    /// One poll in flight at a time. Without this, `update()` running once a
    /// frame would queue polls faster than a 150 ms receive can retire them and
    /// the queue would grow without bound behind every real request.
    bool poll_in_flight_ = false;
    std::size_t spontaneous_count_ = 0;
    std::size_t poll_count_ = 0;
    std::size_t next_listener_ = 1;
    std::vector<std::pair<std::size_t, SpontaneousCallback>> listeners_;

    mutable std::mutex log_mutex_;
    std::deque<LogEntry> log_;
    /// Global index of log_.front(); rises as old entries are dropped.
    std::size_t log_base_ = 0;
    std::size_t max_log_ = 2000;
};

/// One-line summary of a telegram: tokens with their symbolic names.
std::string describeTelegram(const Telegram& telegram);

/// Symbolic name of a token, or an empty string when the reference has none.
std::string tokenDescription(Token token);

}  // namespace gxdemo
