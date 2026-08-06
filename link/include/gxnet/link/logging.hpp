// SPDX-License-Identifier: MIT
#ifndef GXNET_LINK_LOGGING_HPP
#define GXNET_LINK_LOGGING_HPP

#include <functional>
#include <memory>
#include <utility>

#include "gxnet/link/transport.hpp"

namespace gxnet::link {

/// Wraps a transport and reports every exchange to a sink.
///
/// A decorator rather than a hook inside each transport, so that what gets
/// logged is decided by whoever assembles the stack. It matters that this sits
/// at the transport level and not at the call site: `writeAndVerify` sends two
/// telegrams, and the read-back is exactly the one an operator needs to see in
/// the log. Logging at the call site would hide it.
///
/// The sink is called on the transport's thread. Guard whatever it touches.
class LoggingTransport final : public Transport {
public:
    struct Event {
        const Request& request;
        const Exchange* exchange;  ///< null when the call failed outright
        const LinkError& error;
    };

    using Sink = std::function<void(const Event&)>;
    /// Reports open/close/other state changes; optional.
    using NoteSink = std::function<void(const std::string&, const LinkError&)>;

    LoggingTransport(std::unique_ptr<Transport> inner, Sink sink) : inner_(std::move(inner)), sink_(std::move(sink)) {}

    void setNoteSink(NoteSink sink) { notes_ = std::move(sink); }

    /// The wrapped transport, for the things only it can answer.
    [[nodiscard]] Transport& inner() { return *inner_; }

    LinkError onThreadStart() override { return inner_->onThreadStart(); }
    void onThreadStop() override { inner_->onThreadStop(); }

    LinkError open(const Endpoint& endpoint) override {
        LinkError error = inner_->open(endpoint);
        if (notes_) notes_("open " + endpoint.device, error);
        return error;
    }

    void close() override {
        inner_->close();
        if (notes_) notes_("close", {});
    }

    [[nodiscard]] bool isOpen() const override { return inner_->isOpen(); }

    LinkResult<Exchange> execute(const Request& request) override {
        LinkResult<Exchange> result = inner_->execute(request);
        if (sink_) {
            const Event event{request, result.ok() ? &*result.value : nullptr, result.error};
            sink_(event);
        }
        return result;
    }

    /// Not logged: a poll runs several times a second and finds nothing almost
    /// every time. What does arrive is logged by the caller.
    ///
    /// Forwarding matters more than it looks. `Transport` gives this a default
    /// that reports a quiet channel, so a decorator that omits it compiles and
    /// runs, and answers "nothing waiting" for every transport it wraps.
    LinkResult<Exchange> receiveSpontaneous(const std::string& queue, std::chrono::milliseconds timeout) override {
        return inner_->receiveSpontaneous(queue, timeout);
    }

    LinkResult<CallResult> call(const std::string& method, const std::vector<CallArg>& args) override {
        return inner_->call(method, args);
    }

    [[nodiscard]] std::optional<TextMode> textMode() const override { return inner_->textMode(); }

    [[nodiscard]] std::string description() const override { return inner_->description(); }

private:
    std::unique_ptr<Transport> inner_;
    Sink sink_;
    NoteSink notes_;
};

}  // namespace gxnet::link

#endif  // GXNET_LINK_LOGGING_HPP
