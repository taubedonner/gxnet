// SPDX-License-Identifier: MIT
#include "session.hpp"

#include <cstdio>
#include <utility>

#include "gxnet/link/bcs.hpp"
#include "gxnet/link/operations.hpp"

namespace gxdemo {
namespace {

std::chrono::system_clock::time_point now() { return std::chrono::system_clock::now(); }

}  // namespace

std::string tokenDescription(Token token) {
    if (const auto info = Registry::builtin().find(token)) {
        std::string out(info->name);
        if (info->since.valid()) out += " [" + info->since.str() + "]";
        if (info->reserved) out += " (reserved)";
        return out;
    }
    return {};
}

std::string describeTelegram(const Telegram& telegram) {
    std::string out;
    forEachNode(telegram.header.nodes, [&out](const Node& node) {
        if (!out.empty()) out += "  ";
        out += node.token.str();
        const std::string name = tokenDescription(node.token);
        if (!name.empty()) out += " " + name;
    });
    return out;
}

namespace {

/// Fields whose number means something, spelled out.
///
/// A channel bitmap of 66 and a `LGW_UFKENN` of 1891 are both correct and both
/// unreadable; `intern, A, F` and `GT63 GGT_SIMPLE_TXT3` are the same facts.
/// `link::annotateValue` knows the readings, this walks a telegram and applies
/// them.
///
/// Capped, because a `SRV_NET_KONF_*` answer carries a hundred and forty of
/// these and a log line that long is not a log line. The cap is stated rather
/// than silent -- a truncated list that does not say it is truncated is worse
/// than no list.
std::string describeAnnotations(const Telegram& telegram) {
    constexpr std::size_t kMax = 8;

    const Record* record = telegram.records.empty() ? nullptr : &telegram.records.front();
    std::size_t field = 0;
    std::size_t shown = 0;
    std::size_t total = 0;
    std::string out;

    forEachNode(telegram.header.nodes, [&](const Node& node) {
        if (node.token.arity() == 0) return;
        Value value = node.value;
        const std::size_t index = field++;
        if (isEmpty(value) && record != nullptr && index < record->size()) value = (*record)[index];

        const std::string text = link::annotateValue(node.token, value);
        if (text.empty()) return;
        ++total;
        if (shown >= kMax) return;
        ++shown;
        out += "  |  " + node.token.str() + " = " + text;
    });

    if (total > shown) out += "  |  ... and " + std::to_string(total - shown) + " more decoded";
    return out;
}

/// What a refusal actually said, appended to the description of the telegram
/// that carried it.
///
/// Both codes are numbers by the time they reach a log, and both are lookups
/// somebody would otherwise do by hand at the moment they least want to: the
/// return code against the reference's table, and the internal code against a
/// rule the reference prints two hundred pages away from the table it explains.
std::string describeCodes(const Telegram& reply) {
    std::string out;

    // LGW_RETURN is what makes this a refusal, so it is also what says the
    // other codes are being reported rather than merely carried.
    const auto code = link::returnCodeOf(reply);
    if (!code) return out;

    out += "  |  LGW_RETURN " + std::to_string(*code);
    const auto text = link::returnCodeText(*code);
    if (!text.empty()) out += " (" + std::string(text) + ")";

    // LGW_DEBUG is an ordinary parameter in several telegrams -- the query that
    // asks WZV_META_ERROR_TEXT for the text of an error number echoes the
    // number back in this very field. Decoding that as "the device reported
    // error N" would read as a failure where there was an answer.
    static constexpr Token kDebug = knownToken("LGW_DEBUG").token;  // LW03
    if (const auto debug = link::valueOf(reply, kDebug)) {
        if (const auto* word = std::get_if<std::int16_t>(&*debug)) {
            // Printed both ways on purpose: the server's log gives it in hex
            // and the reference's appendix in decimal.
            const auto value = static_cast<std::int32_t>(static_cast<std::uint16_t>(*word));
            char hex[8] = {};
            std::snprintf(hex, sizeof hex, "0x%04X", static_cast<unsigned>(value));
            out += "  |  LGW_DEBUG " + std::to_string(value) + " " + hex;
            const std::string decoded = link::internalErrorText(value);
            if (!decoded.empty()) out += " (" + decoded + ")";
        }
    }
    return out;
}

}  // namespace

Session::Session() = default;

Session::~Session() { worker_.reset(); }

std::unique_ptr<link::Transport> Session::buildTransport() {
    if (settings_.kind == Kind::Bcs) {
        link::BcsTransport::Options options;
        options.prog_id = settings_.prog_id;
        options.probe_text_mode = settings_.probe_text_mode;
        auto bcs = link::BcsTransport::create(options);
        if (!bcs) {
            status_ = "this build has no BCS transport (Windows only)";
            return nullptr;
        }
        mock_ = nullptr;
        return bcs;
    }

    auto device = std::make_unique<link::MockTransport>();
    mock_ = device.get();
    return device;
}

void Session::connect() {
    disconnect();

    auto transport = buildTransport();
    if (!transport) {
        mock_ = nullptr;
        append({LogEntry::Kind::Error, status_, {}, now(), {}});
        return;
    }

    auto logging = std::make_unique<link::LoggingTransport>(
        std::move(transport), [this](const link::LoggingTransport::Event& event) { onExchange(event); });
    logging->setNoteSink([this](const std::string& what, const link::LinkError& error) {
        if (error) {
            append({LogEntry::Kind::Error, what + " failed", error.str(), now(), {}});
        } else {
            append({LogEntry::Kind::Note, what, {}, now(), {}});
        }
    });

    worker_ = std::make_unique<link::Worker>(std::move(logging));
    spontaneous_count_ = 0;
    poll_count_ = 0;

    link::Endpoint endpoint;
    endpoint.user = settings_.user;
    endpoint.device = settings_.device;
    endpoint.spontaneous = settings_.spontaneous;
    endpoint.exclusive = settings_.exclusive;

    status_ = "connecting to " + settings_.device + "...";

    worker_->open(std::move(endpoint), [this](link::LinkError error) {
        if (error) {
            status_ = "not connected: " + error.str();
            return;
        }
        status_ = "connected";
        // Ask the server how the device wants text, so escaping is right from
        // the first telegram rather than after the first mangled label.
        worker_->post([this](link::Transport& transport) { text_mode_ = transport.textMode(); });

        // And ask the device its firmware, once, so nothing here has to send a
        // command the device is too old for and read the refusal to find out.
        static constexpr Token kVersion = knownToken("SRT_GX_VERSION").token;  // ST8D
        read(kVersion, [this](link::LinkResult<Value> result) {
            if (!result) return;
            if (const auto* text = std::get_if<std::string>(&*result)) {
                device_version_ = Version::parse(*text);
            }
        });
    });
}

Session::Supported Session::supports(Token token) const {
    const auto info = Registry::builtin().find(token);
    if (!info || !info->since.valid() || !device_version_) return {};
    if (*device_version_ >= info->since) return {};

    std::string reason = token.str();
    reason += " ";
    reason += info->name;
    reason += " needs firmware " + info->since.str() + "; this device reports " + device_version_->str();
    return {false, std::move(reason)};
}

void Session::disconnect() {
    // Before the early return as well: a poll queued against a worker that is
    // about to be dropped never runs its completion, and the flag would then
    // block every poll on the next connection.
    poll_in_flight_ = false;

    if (!worker_) {
        mock_ = nullptr;
        return;
    }
    worker_->close();
    // Drain what is left so the closing note reaches the log, then drop the
    // thread; the destructor joins it.
    worker_->drain();
    worker_.reset();
    mock_ = nullptr;
    text_mode_.reset();
    device_version_.reset();
    status_ = "not connected";
}

void Session::update() {
    if (!worker_) return;
    worker_->drain();
    pumpSpontaneous();
}

void Session::pumpSpontaneous() {
    if (!settings_.listen || poll_in_flight_ || !worker_->connected()) return;

    // Only when the line is otherwise idle. A poll occupies the transport
    // thread for its whole timeout, and delaying an operator's request behind a
    // speculative receive is the wrong trade in a bench: the records this
    // catches arrive on the device's schedule, not ours, and waiting one more
    // frame for them costs nothing.
    if (worker_->pending() > 0) return;

    poll_in_flight_ = true;
    ++poll_count_;

    auto slot = std::make_shared<link::LinkResult<link::Exchange>>();
    const std::string queue = settings_.listen_queue;
    const auto wait = std::chrono::milliseconds{settings_.listen_timeout_ms};
    worker_->post(
        [slot, queue, wait](link::Transport& transport) { *slot = transport.receiveSpontaneous(queue, wait); },
        [this, slot]() {
            poll_in_flight_ = false;
            if (!slot->ok()) {
                // Report once and stop, rather than repeating the same failure
                // every frame for as long as the connection lasts.
                settings_.listen = false;
                append({LogEntry::Kind::Error, "spontaneous listener stopped", slot->error.str(), now(), {}});
                return;
            }
            if ((*slot)->received.empty()) return;
            deliverSpontaneous(**slot);
        });
}

void Session::deliverSpontaneous(const link::Exchange& exchange) {
    ++spontaneous_count_;

    std::string text;
    for (const auto& line : exchange.received) {
        if (!text.empty()) text += " / ";
        text += line;
    }

    std::string detail = "spontaneous";
    if (exchange.reply) {
        detail += " | " + describeTelegram(*exchange.reply);
        const std::string codes = describeCodes(*exchange.reply);
        if (!codes.empty()) detail += " | " + codes;
        detail += describeAnnotations(*exchange.reply);
    } else if (exchange.reply_error) {
        detail += " | unparsed: " + exchange.reply_error->message;
    }
    append({LogEntry::Kind::Received, text, detail, now(), exchange.elapsed});

    // Copied before iterating: a listener is free to unsubscribe from inside its
    // own callback, which is exactly what a panel does once its answer arrives.
    const auto snapshot = listeners_;
    for (const auto& [id, callback] : snapshot) {
        (void)id;
        if (callback) callback(exchange);
    }
}

std::size_t Session::listen(SpontaneousCallback callback) {
    const std::size_t id = next_listener_++;
    listeners_.emplace_back(id, std::move(callback));
    return id;
}

void Session::unlisten(std::size_t token) {
    for (auto it = listeners_.begin(); it != listeners_.end(); ++it) {
        if (it->first == token) {
            listeners_.erase(it);
            return;
        }
    }
}

bool Session::connected() const { return worker_ && worker_->connected(); }

bool Session::busy() const { return worker_ && worker_->busy(); }

std::size_t Session::pending() const { return worker_ ? worker_->pending() : 0; }

void Session::read(Token token, ValueCallback done) {
    if (!worker_) {
        if (done) done(link::LinkResult<Value>::fail({0, "not connected"}));
        return;
    }

    // Refused here rather than by the device. The panel that polls SW9B after
    // every write produced six identical `fremdes Kommando` exceptions in one
    // session on a device four releases too old for it, and the message the
    // program printed told the reader to go and compare the release against
    // SRT_GX_VERSION -- a comparison the program is holding both sides of.
    //
    // Only for reads, and only when the version is known. The console must stay
    // able to send anything at all: refusing to try is how a wrong "since" in
    // the table would become unfalsifiable.
    if (const Supported supported = supports(token); !supported.ok) {
        append({LogEntry::Kind::Note, "not sent: " + token.str(), supported.reason, now(), {}});
        if (done) done(link::LinkResult<Value>::fail({0, supported.reason}));
        return;
    }
    auto slot = std::make_shared<link::LinkResult<Value>>();
    const auto wait = timeout();
    worker_->post([slot, token, wait](link::Transport& transport) { *slot = link::readOne(transport, token, wait); },
                  [slot, done = std::move(done)]() mutable {
                      if (done) done(std::move(*slot));
                  });
}

void Session::write(Token token, Value value, ValueCallback done) {
    if (!worker_) {
        if (done) done(link::LinkResult<Value>::fail({0, "not connected"}));
        return;
    }
    auto slot = std::make_shared<link::LinkResult<Value>>();
    const auto wait = timeout();
    worker_->post([slot, token, value = std::move(value),
                   wait](link::Transport& transport) { *slot = link::writeAndVerify(transport, token, value, wait); },
                  [slot, done = std::move(done)]() mutable {
                      if (done) done(std::move(*slot));
                  });
}

void Session::send(Telegram telegram, bool expect_reply, ExchangeCallback done) {
    send(std::move(telegram), expect_reply, timeout(), std::move(done));
}

void Session::send(Telegram telegram, bool expect_reply, std::chrono::milliseconds wait, ExchangeCallback done) {
    if (!worker_) {
        if (done) done(link::LinkResult<link::Exchange>::fail({0, "not connected"}));
        return;
    }
    link::Request request;
    request.telegram = std::move(telegram);
    request.timeout = wait;
    request.expect_reply = expect_reply;
    request.one_line = settings_.use_send_one;
    worker_->request(std::move(request), std::move(done));
}

void Session::call(std::string method, std::vector<link::CallArg> args, CallCallback done) {
    if (!worker_) {
        if (done) done(link::LinkResult<link::CallResult>::fail({0, "not connected"}));
        return;
    }

    // The call as it goes out, for the log. Out parameters are shown as such
    // rather than as empty strings: which of them was an output is the question
    // being asked, so it belongs in the record of the attempt.
    std::string shown = method + "(";
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i > 0) shown += ", ";
        switch (args[i].kind) {
            case link::CallArg::Kind::InText: shown += "\"" + args[i].text + "\""; break;
            case link::CallArg::Kind::InLong:
            case link::CallArg::Kind::InShort: shown += std::to_string(args[i].number); break;
            case link::CallArg::Kind::OutText: shown += "out string"; break;
            case link::CallArg::Kind::OutLong: shown += "out long"; break;
            case link::CallArg::Kind::OutShort: shown += "out short"; break;
        }
    }
    shown += ")";

    auto slot = std::make_shared<link::LinkResult<link::CallResult>>();
    worker_->post([slot, method = std::move(method),
                   args = std::move(args)](link::Transport& transport) { *slot = transport.call(method, args); },
                  [this, slot, shown = std::move(shown), done = std::move(done)]() mutable {
                      append({LogEntry::Kind::Sent, shown, "automation call, not a telegram", now(), {}});

                      if (!slot->ok()) {
                          append({LogEntry::Kind::Error, shown, slot->error.str(), now(), {}});
                      } else {
                          std::string detail = "returned " + std::to_string((*slot)->result);
                          for (std::size_t i = 0; i < (*slot)->arguments.size(); ++i) {
                              detail += "  |  [" + std::to_string(i) + "] " + (*slot)->arguments[i];
                          }
                          if (!(*slot)->server_error.empty()) detail += "  |  " + (*slot)->server_error;
                          append({LogEntry::Kind::Received, shown, std::move(detail), now(), {}});
                      }
                      if (done) done(std::move(*slot));
                  });
}

void Session::sendRaw(const std::string& line, bool expect_reply, ExchangeCallback done) {
    // A console line is the interleaved form: header and values on one line.
    auto parsed = parseOneLine(line);
    if (!parsed) {
        // Fall back to a header with no data, which is what a read looks like.
        auto header = parseHeader(line);
        if (!header) {
            append({LogEntry::Kind::Error, line, header.error.message, now(), {}});
            if (done) {
                done(link::LinkResult<link::Exchange>::fail({0, "cannot parse: " + header.error.message}));
            }
            return;
        }
        Telegram telegram;
        telegram.header = *header;
        send(std::move(telegram), expect_reply, std::move(done));
        return;
    }
    send(*parsed, expect_reply, std::move(done));
}

void Session::onExchange(const link::LoggingTransport::Event& event) {
    const auto stamp = now();

    if (event.exchange != nullptr) {
        // The Send's own lStatus goes on the first sent line and nowhere else.
        // It is the one number that settles whether this code should follow the
        // vendor's samples and receive only while the server says 2.
        std::string sent_detail = describeTelegram(event.request.telegram);
        {
            const auto sent_status = event.exchange->send_status;
            sent_detail += "   [Send lStatus " + std::to_string(static_cast<int>(sent_status)) + " " +
                           link::statusName(sent_status) + "]";
        }
        bool first = true;
        for (const std::string& line : event.exchange->sent) {
            append({LogEntry::Kind::Sent, line, first ? sent_detail : describeTelegram(event.request.telegram), stamp,
                    event.exchange->elapsed});
            first = false;
        }
        for (const std::string& line : event.exchange->received) {
            std::string detail;
            if (event.exchange->reply) {
                detail = describeTelegram(*event.exchange->reply) + describeCodes(*event.exchange->reply) +
                         describeAnnotations(*event.exchange->reply);
            } else if (event.exchange->reply_error) {
                detail = "unparsed: " + event.exchange->reply_error->message;
            }
            append({LogEntry::Kind::Received, line, std::move(detail), stamp, event.exchange->elapsed});
        }
        if (event.exchange->status == link::Status::Timeout) {
            // A timeout here is not evidence that the telegram failed, and the
            // old wording said it was. BCS `Send` waits for an *answer from the
            // device*, not for delivery -- so a write that produces no
            // application-level answer burns the whole timeout and reports one
            // even though it arrived. Measured: a WZV_SDD_START whose Send
            // timed out appears in the server's own log acknowledged 3 ms after
            // it went out, with the dialog on the terminal.
            std::string detail =
                "BCS Send waits for an answer from the device, not for delivery: a telegram that produces "
                "no answer times out even though it arrived";
            if (event.exchange->reset_after_timeout) {
                // Worth its own clause: Reset takes the request off the
                // server's transmission list, so a late answer becomes a
                // spontaneous record belonging to nothing.
                detail += "; the request was cancelled with Reset";
            }
            append({event.request.expect_reply ? LogEntry::Kind::Error : LogEntry::Kind::Note,
                    "no answer within the timeout", std::move(detail), stamp, event.exchange->elapsed});
        }
        return;
    }

    auto lines = encodeLines(event.request.telegram);
    const std::string what = lines && !lines->empty() ? lines->front() : "(unencodable)";
    append({LogEntry::Kind::Error, what, event.error.str(), stamp, {}});
}

void Session::append(LogEntry entry) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    log_.push_back(std::move(entry));
    while (log_.size() > max_log_) {
        log_.pop_front();
        ++log_base_;
    }
}

Session::LogSlice Session::logSince(std::size_t cursor) const {
    std::lock_guard<std::mutex> lock(log_mutex_);

    LogSlice slice;
    slice.cursor = log_base_ + log_.size();

    // A cursor from before the oldest surviving entry means the view missed
    // some; give it everything still held rather than silently skipping.
    const std::size_t from = cursor < log_base_ ? log_base_ : cursor;
    for (std::size_t i = from; i < log_base_ + log_.size(); ++i) {
        slice.entries.push_back(log_[i - log_base_]);
    }
    return slice;
}

void Session::note(std::string text, std::string detail) {
    append({LogEntry::Kind::Note, std::move(text), std::move(detail), now(), {}});
}

std::vector<Session::LogEntry> Session::log() const {
    std::lock_guard<std::mutex> lock(log_mutex_);
    return {log_.begin(), log_.end()};
}

void Session::clearLog() {
    std::lock_guard<std::mutex> lock(log_mutex_);
    log_base_ += log_.size();
    log_.clear();
}

}  // namespace gxdemo
