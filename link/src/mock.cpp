// SPDX-License-Identifier: MIT
#include "gxnet/link/mock.hpp"

#include <thread>

namespace gxnet::link {
namespace {

/// Encodes a request the way the transport would put it on the wire.
Result<std::vector<std::string>> encodeRequest(const Request& request) {
    if (request.one_line) {
        auto line = encodeOneLine(request.telegram);
        if (!line) return {std::nullopt, line.error};
        return {std::vector<std::string>{*line}, {}};
    }
    return encodeLines(request.telegram);
}

/// Pairs each payload token with the value carried for it, taking the record
/// when there is one and falling back to the value sitting on the node. Builder
/// fills both; a parsed telegram fills one or the other depending on the form.
std::vector<std::pair<Token, Value>> payloadPairs(const Telegram& telegram) {
    std::vector<std::pair<Token, Value>> out;
    const std::vector<Token> tokens = telegram.header.payloadTokens();
    const Record* record = telegram.records.empty() ? nullptr : &telegram.records.front();

    std::size_t index = 0;
    forEachNode(telegram.header.nodes, [&](const Node& node) {
        if (node.token.arity() == 0) return;
        Value value = node.value;
        if (isEmpty(value) && record && index < record->size()) {
            value = (*record)[index];
        }
        out.emplace_back(node.token, std::move(value));
        ++index;
    });
    (void)tokens;
    return out;
}

}  // namespace

void MockTransport::set(Token token, Value value) { values_[token] = std::move(value); }

std::optional<Value> MockTransport::get(Token token) const {
    const auto it = values_.find(token);
    if (it == values_.end()) return std::nullopt;
    return it->second;
}

void MockTransport::seedFrom(const Telegram& telegram) {
    for (auto& [token, value] : payloadPairs(telegram)) {
        if (!isEmpty(value)) values_[token] = std::move(value);
    }
}

void MockTransport::handle(Token token, Handler handler) { handlers_[token] = std::move(handler); }

void MockTransport::replay(std::string header_line, std::vector<std::string> reply_lines) {
    replays_[std::move(header_line)] = std::move(reply_lines);
}

void MockTransport::failNext(LinkError error) { fail_next_ = std::move(error); }

void MockTransport::timeoutNext() { timeout_next_ = true; }

void MockTransport::postSpontaneous(std::vector<std::string> lines) {
    if (lines.empty()) return;
    spontaneous_.push_back(std::move(lines));
}

void MockTransport::postSpontaneous(const Telegram& telegram) {
    auto line = encodeOneLine(telegram);
    if (!line) return;
    spontaneous_.push_back({*line});
}

LinkResult<Exchange> MockTransport::receiveSpontaneous(const std::string& queue, std::chrono::milliseconds timeout) {
    (void)queue;
    (void)timeout;

    Exchange exchange;
    if (!open_) {
        return LinkResult<Exchange>::fail({0, "not connected"});
    }
    if (spontaneous_.empty()) {
        exchange.status = Status::Timeout;
        return LinkResult<Exchange>::of(std::move(exchange));
    }

    exchange.received = std::move(spontaneous_.front());
    spontaneous_.pop_front();
    exchange.status = spontaneous_.empty() ? Status::Ok : Status::MoreData;

    if (exchange.received.size() == 1) {
        auto one = parseOneLine(exchange.received.front());
        if (one) {
            exchange.reply = *one;
        } else {
            exchange.reply_error = one.error;
        }
    } else {
        auto lines = parseLines(exchange.received);
        if (lines) {
            exchange.reply = *lines;
        } else {
            exchange.reply_error = lines.error;
        }
    }
    return LinkResult<Exchange>::of(std::move(exchange));
}

void MockTransport::clearHistory() {
    sent_.clear();
    requests_.clear();
    writes_.clear();
}

LinkError MockTransport::open(const Endpoint& endpoint) {
    if (endpoint.device.empty()) {
        return {0, "no device name given"};
    }
    endpoint_ = endpoint;
    open_ = true;
    return {};
}

void MockTransport::close() { open_ = false; }

std::string MockTransport::description() const {
    if (!open_) return options_.device_description + " (closed)";
    return options_.device_description + " [" + endpoint_.device + "]";
}

LinkResult<Exchange> MockTransport::execute(const Request& request) {
    if (!open_) {
        return LinkResult<Exchange>::fail({0, "transport is not open"});
    }

    auto encoded = encodeRequest(request);
    if (!encoded) {
        return LinkResult<Exchange>::fail({0, "cannot encode request: " + encoded.error.message});
    }

    sent_.insert(sent_.end(), encoded->begin(), encoded->end());
    requests_.push_back(request);

    if (fail_next_) {
        LinkError error = *fail_next_;
        fail_next_.reset();
        return LinkResult<Exchange>::fail(error);
    }

    if (timeout_next_) {
        timeout_next_ = false;
        Exchange exchange;
        exchange.sent = *encoded;
        exchange.status = Status::Timeout;
        return LinkResult<Exchange>::of(std::move(exchange));
    }

    if (options_.latency.count() > 0) {
        std::this_thread::sleep_for(options_.latency);
    }

    // A recorded answer wins over the model.
    const std::string header_line = encodeHeader(request.telegram.header);
    if (const auto it = replays_.find(header_line); it != replays_.end()) {
        Exchange exchange;
        exchange.sent = *encoded;
        exchange.received = it->second;
        auto parsed = parseLines(it->second);
        if (parsed) {
            exchange.reply = *parsed;
        } else {
            exchange.reply_error = parsed.error;
        }
        return LinkResult<Exchange>::of(std::move(exchange));
    }

    return request.telegram.header.access == Access::Read ? answerRead(request, *encoded)
                                                          : answerWrite(request, *encoded);
}

LinkResult<Exchange> MockTransport::answerWrite(const Request& request, const std::vector<std::string>& sent) {
    // Commands carry no payload but still mean something -- XX13 clears the
    // unique-data buffer -- so the whole tree is walked, not just the fields.
    forEachNode(request.telegram.header.nodes, [&](const Node& node) {
        if (node.token.arity() != 0) return;
        if (node.token.isBlock() || node.token.isBlockClose()) return;
        if (const auto it = handlers_.find(node.token); it != handlers_.end()) {
            it->second(node, Access::Write);
        }
        writes_.emplace_back(node.token, Value{});
    });

    for (auto& [token, value] : payloadPairs(request.telegram)) {
        writes_.emplace_back(token, value);
        if (const auto it = handlers_.find(token); it != handlers_.end()) {
            it->second(Node(token, value), Access::Write);
            continue;
        }
        values_[token] = std::move(value);
    }

    Exchange exchange;
    exchange.sent = sent;
    // Nothing is fabricated here: what a device sends back after a write is not
    // documented, so the mock sends back nothing and the caller reads back.
    return LinkResult<Exchange>::of(std::move(exchange));
}

LinkResult<Exchange> MockTransport::answerRead(const Request& request, const std::vector<std::string>& sent) {
    Record record;
    std::string missing;

    forEachNode(request.telegram.header.nodes, [&](const Node& node) {
        if (node.token.arity() == 0) return;

        if (const auto it = handlers_.find(node.token); it != handlers_.end()) {
            if (auto value = it->second(node, Access::Read)) {
                record.push_back(std::move(*value));
            } else if (missing.empty()) {
                missing = node.token.str() + " (handler declined)";
            }
            return;
        }

        if (const auto it = values_.find(node.token); it != values_.end()) {
            record.push_back(it->second);
            return;
        }

        if (missing.empty()) missing = node.token.str();
    });

    if (!missing.empty()) {
        return LinkResult<Exchange>::fail(
            {0, "mock device has no value for " + missing + "; seed it with set() or handle()"});
    }

    Exchange exchange;
    exchange.sent = sent;

    std::vector<std::string> lines;
    if (options_.echo_header) {
        lines.push_back(encodeHeader(request.telegram.header));
    }
    auto data = encodeRecord(request.telegram.header, record);
    if (!data) {
        return LinkResult<Exchange>::fail({0, "cannot encode reply: " + data.error.message});
    }
    lines.push_back(*data);

    exchange.received = lines;
    if (options_.echo_header) {
        auto parsed = parseLines(lines);
        if (parsed) {
            exchange.reply = *parsed;
        } else {
            exchange.reply_error = parsed.error;
        }
    } else {
        // Without a header there is nothing to parse against but the request's
        // own layout, which is what a caller would do anyway.
        Telegram reply;
        reply.header = request.telegram.header;
        reply.records.push_back(record);
        exchange.reply = std::move(reply);
    }

    return LinkResult<Exchange>::of(std::move(exchange));
}

}  // namespace gxnet::link
