// SPDX-License-Identifier: MIT
#include "gxnet/link/transport.hpp"

namespace gxnet::link {

const char* statusName(Status status) {
    switch (status) {
        case Status::Ok: return "ok";
        case Status::Timeout: return "timeout";
        case Status::MoreData: return "more data";
    }
    return "unknown";
}

CallArg inText(std::string value) {
    CallArg arg;
    arg.kind = CallArg::Kind::InText;
    arg.text = std::move(value);
    return arg;
}

CallArg inLong(std::int32_t value) {
    CallArg arg;
    arg.kind = CallArg::Kind::InLong;
    arg.number = value;
    return arg;
}

CallArg inShort(std::int16_t value) {
    CallArg arg;
    arg.kind = CallArg::Kind::InShort;
    arg.number = value;
    return arg;
}

CallArg outText() {
    CallArg arg;
    arg.kind = CallArg::Kind::OutText;
    return arg;
}

CallArg outLong() {
    CallArg arg;
    arg.kind = CallArg::Kind::OutLong;
    return arg;
}

CallArg outShort() {
    CallArg arg;
    arg.kind = CallArg::Kind::OutShort;
    return arg;
}

LinkResult<CallResult> Transport::call(const std::string& method, const std::vector<CallArg>& args) {
    (void)args;
    return LinkResult<CallResult>::fail({0, "this transport has no automation object to call " + method + " on"});
}

LinkResult<Exchange> Transport::receiveSpontaneous(const std::string& queue, std::chrono::milliseconds timeout) {
    (void)queue;
    (void)timeout;
    // Nothing waiting, rather than an error. A transport with no spontaneous
    // channel behaves like one whose channel is quiet, which is what every
    // caller already has to handle.
    Exchange exchange;
    exchange.status = Status::Timeout;
    return LinkResult<Exchange>::of(std::move(exchange));
}

std::string LinkError::str() const {
    if (!*this) return "ok";
    std::string out = message.empty() ? "transport error" : message;
    if (code != 0) out += " (code " + std::to_string(code) + ")";
    return out;
}

namespace {

/// Finds the value the device reported for `token` anywhere in a reply.
///
/// The answer to a single-token read is a single field, but the same routine
/// serves a block reply where the token sits several levels down, so it walks
/// the tree rather than indexing into it.
std::optional<Value> valueOf(const Telegram& telegram, Token token) {
    std::size_t index = 0;
    std::optional<std::size_t> position;
    forEachNode(telegram.header.nodes, [&](const Node& node) {
        if (node.token.arity() == 0) return;
        if (node.token == token && !position) position = index;
        ++index;
    });
    if (!position) return std::nullopt;

    // An interleaved reply carries values on the nodes themselves; a
    // header-plus-data reply carries them in the record.
    std::optional<Value> inline_value;
    forEachNode(telegram.header.nodes, [&](const Node& node) {
        if (node.token == token && !isEmpty(node.value) && !inline_value) {
            inline_value = node.value;
        }
    });
    if (inline_value) return inline_value;

    if (telegram.records.empty()) return std::nullopt;
    const Record& record = telegram.records.front();
    if (*position >= record.size()) return std::nullopt;
    return record[*position];
}

}  // namespace

LinkResult<Value> readOne(Transport& transport, Token token, std::chrono::milliseconds timeout) {
    Builder builder(Family::Automatic, Access::Read);
    builder.query(token.str());
    if (!builder.ok()) {
        return LinkResult<Value>::fail({0, builder.error()});
    }

    Request request;
    request.telegram = builder.build();
    request.timeout = timeout;
    request.expect_reply = true;
    // one_line stays at its default, which is Send rather than SendOne.

    auto exchange = transport.execute(request);
    if (!exchange) return LinkResult<Value>::fail(exchange.error);

    if (exchange->status == Status::Timeout) {
        return LinkResult<Value>::fail(
            {static_cast<int>(Status::Timeout),
             "no answer to " + token.str() + " within " + std::to_string(timeout.count()) + " ms"});
    }
    if (!exchange->reply) {
        std::string message = "no reply telegram for " + token.str();
        if (exchange->reply_error) {
            message += ": " + exchange->reply_error->message;
        }
        return LinkResult<Value>::fail({0, std::move(message)});
    }

    auto value = valueOf(*exchange->reply, token);
    if (!value) {
        return LinkResult<Value>::fail({0, "reply carries no value for " + token.str()});
    }
    return LinkResult<Value>::of(*value);
}

LinkResult<Value> writeAndVerify(Transport& transport, Token token, Value value, std::chrono::milliseconds timeout) {
    Builder builder(Family::Automatic, Access::Write);
    builder.add(Node(token, value));
    if (!builder.ok()) {
        return LinkResult<Value>::fail({0, builder.error()});
    }

    Request request;
    request.telegram = builder.build();
    request.timeout = timeout;
    // The acknowledgement shape for a write is not documented, so nothing is
    // read here; the read-back below is what establishes the outcome.
    request.expect_reply = false;

    auto sent = transport.execute(request);
    if (!sent) return LinkResult<Value>::fail(sent.error);

    auto read_back = readOne(transport, token, timeout);
    if (!read_back) return read_back;

    if (*read_back != value) {
        return LinkResult<Value>::fail({0, "write to " + token.str() + " did not take: wrote " + encodeValue(value) +
                                               ", device reports " + encodeValue(*read_back)});
    }
    return read_back;
}

}  // namespace gxnet::link
