// SPDX-License-Identifier: MIT
#ifndef GXNET_TELEGRAM_HPP
#define GXNET_TELEGRAM_HPP

#include <string>
#include <vector>

#include "gxnet/token.hpp"
#include "gxnet/value.hpp"

namespace gxnet {

/// Telegram prefix family.
///
/// `A` addresses a weighing labeler, `I` an industrial device, `G` the Gx
/// device itself. `Legacy` is the old GxTools form with a bare `!` or `?` and
/// no family letter; it also switches the dimension sub-separator to '|'.
enum class Family { Automatic, Industrial, Gx, Legacy };

/// Access direction: `!` writes to the device, `?` reads from it.
enum class Access { Write, Read };

/// One entry of a telegram. Block tokens own their children; the terminating
/// LX02 is implicit in the tree and materialised only during encoding.
struct Node {
    Token token;
    Value value;                 ///< empty for Command and Block tokens
    std::vector<Node> children;  ///< non-empty only for Block tokens

    /// Whether a block is terminated by an explicit LX02.
    ///
    /// The reference shows both forms: a block may be closed by LX02, or it may
    /// simply run to the end of the header line. Preserving which form was used
    /// keeps parse/encode round-trips byte-exact.
    bool explicit_close = true;

    Node() = default;
    explicit Node(Token t) : token(t) {}
    Node(Token t, Value v) : token(t), value(std::move(v)) {}
};

/// The token layout of a telegram, i.e. everything on the header line.
struct Header {
    Family family = Family::Automatic;
    Access access = Access::Write;
    std::vector<Node> nodes;

    /// Sub-separator used inside dimensional values for this family.
    char dimensionSeparator() const { return family == Family::Legacy ? '|' : ';'; }

    /// Tokens that consume a field in the data line, in wire order.
    std::vector<Token> payloadTokens() const;

    /// Number of fields one data record must contain.
    std::size_t payloadArity() const;
};

/// A single data record: one field per payload token, in header order.
using Record = std::vector<Value>;

/// A header together with the records that follow it. Device exports and file
/// interfaces emit one header followed by many records; an interactive command
/// is simply the degenerate case of exactly one record.
struct Telegram {
    Header header;
    std::vector<Record> records;

    /// Convenience accessor for the single record case.
    bool singleRecord() const { return records.size() == 1; }
};

/// Walks the node tree depth first, invoking `fn` for every node.
template<typename Fn>
void forEachNode(const std::vector<Node>& nodes, Fn&& fn) {
    for (const Node& n : nodes) {
        fn(n);
        if (!n.children.empty()) forEachNode(n.children, fn);
    }
}

/// Fluent construction of a single-record telegram.
///
///     Telegram t = Builder(Family::Automatic, Access::Write)
///                      .block("PV04")
///                          .word("PW02", 7)
///                          .long_("GL19", 1)
///                          .dimension("PD00", {"KG", -3, 1064})
///                      .end()
///                      .build();
///
/// Tokens are given as text and validated on the spot; a malformed token or a
/// type mismatch sets the error flag, which `build()` reports.
class Builder {
public:
    Builder(Family family, Access access);

    Builder& command(std::string_view token);
    Builder& word(std::string_view token, std::int16_t value);
    Builder& long_(std::string_view token, std::int32_t value);
    Builder& text(std::string_view token, std::string value);
    Builder& dimension(std::string_view token, Dimension value);

    /// Adds a payload-carrying token with no value attached.
    ///
    /// This is the shape of a read request: `A?GW7D` names the subfunction and
    /// the device supplies the value. Accepted only for Access::Read; on a
    /// write telegram the encoder reports a missing value.
    Builder& query(std::string_view token);

    /// Opens a block command. Must be matched by `end()`.
    Builder& block(std::string_view token);
    /// Closes the innermost open block.
    Builder& end();

    /// Adds a pre-built node to the current scope.
    Builder& add(Node node);

    bool ok() const { return error_.empty(); }
    const std::string& error() const { return error_; }

    /// Returns the assembled telegram. Check `ok()` first; on error the result
    /// is whatever was accumulated before the failure.
    Telegram build() const;

private:
    Node* current();
    void push(Node node);
    void fail(std::string message);

    Family family_;
    Access access_;
    std::vector<Node> roots_;
    std::vector<std::vector<std::size_t>> open_;  // path to each open block
    std::string error_;
};

}  // namespace gxnet

#endif  // GXNET_TELEGRAM_HPP
