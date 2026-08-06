// SPDX-License-Identifier: MIT
#include "gxnet/telegram.hpp"

#include <string>

namespace gxnet {

std::vector<Token> Header::payloadTokens() const {
    std::vector<Token> out;
    forEachNode(nodes, [&out](const Node& n) {
        if (n.token.arity() > 0) out.push_back(n.token);
    });
    return out;
}

std::size_t Header::payloadArity() const {
    std::size_t count = 0;
    forEachNode(nodes, [&count](const Node& n) { count += static_cast<std::size_t>(n.token.arity()); });
    return count;
}

// --- Builder --------------------------------------------------------------

Builder::Builder(Family family, Access access) : family_(family), access_(access) {}

void Builder::fail(std::string message) {
    if (error_.empty()) error_ = std::move(message);
}

Node* Builder::current() {
    if (open_.empty()) return nullptr;
    const std::vector<std::size_t>& path = open_.back();
    std::vector<Node>* level = &roots_;
    Node* node = nullptr;
    for (std::size_t idx : path) {
        if (idx >= level->size()) return nullptr;
        node = &(*level)[idx];
        level = &node->children;
    }
    return node;
}

void Builder::push(Node node) {
    Node* parent = current();
    if (parent) {
        parent->children.push_back(std::move(node));
    } else {
        roots_.push_back(std::move(node));
    }
}

namespace {

std::optional<Token> parseTokenChecked(std::string_view text, DataType expected, std::string& error_out) {
    auto tok = Token::parse(text);
    if (!tok) {
        if (error_out.empty()) {
            error_out = "malformed token: " + std::string(text);
        }
        return std::nullopt;
    }
    auto type = tok->type();
    if (!type || *type != expected) {
        if (error_out.empty()) {
            error_out = "token " + std::string(text) + " is of type " + (type ? dataTypeName(*type) : "unknown") +
                        ", expected " + dataTypeName(expected);
        }
        return std::nullopt;
    }
    return tok;
}

}  // namespace

Builder& Builder::command(std::string_view token) {
    if (auto t = parseTokenChecked(token, DataType::Command, error_)) {
        push(Node(*t));
    }
    return *this;
}

Builder& Builder::word(std::string_view token, std::int16_t value) {
    if (auto t = parseTokenChecked(token, DataType::Word, error_)) {
        push(Node(*t, Value(value)));
    }
    return *this;
}

Builder& Builder::long_(std::string_view token, std::int32_t value) {
    if (auto t = parseTokenChecked(token, DataType::Long, error_)) {
        push(Node(*t, Value(value)));
    }
    return *this;
}

Builder& Builder::text(std::string_view token, std::string value) {
    if (auto t = parseTokenChecked(token, DataType::Text, error_)) {
        push(Node(*t, Value(std::move(value))));
    }
    return *this;
}

Builder& Builder::dimension(std::string_view token, Dimension value) {
    if (auto t = parseTokenChecked(token, DataType::Dimension, error_)) {
        push(Node(*t, Value(std::move(value))));
    }
    return *this;
}

Builder& Builder::query(std::string_view token) {
    auto tok = Token::parse(token);
    if (!tok) {
        fail("malformed token: " + std::string(token));
        return *this;
    }
    push(Node(*tok));
    return *this;
}

Builder& Builder::block(std::string_view token) {
    auto t = parseTokenChecked(token, DataType::Block, error_);
    if (!t) return *this;

    std::vector<std::size_t> path;
    if (!open_.empty()) path = open_.back();

    Node* parent = current();
    std::size_t index = parent ? parent->children.size() : roots_.size();
    push(Node(*t));
    path.push_back(index);
    open_.push_back(std::move(path));
    return *this;
}

Builder& Builder::end() {
    if (open_.empty()) {
        fail("end() without a matching block()");
        return *this;
    }
    open_.pop_back();
    return *this;
}

Builder& Builder::add(Node node) {
    push(std::move(node));
    return *this;
}

Telegram Builder::build() const {
    Telegram out;
    out.header.family = family_;
    out.header.access = access_;
    out.header.nodes = roots_;

    Record record;
    forEachNode(out.header.nodes, [&record](const Node& n) {
        if (n.token.arity() > 0) record.push_back(n.value);
    });
    out.records.push_back(std::move(record));
    return out;
}

}  // namespace gxnet
