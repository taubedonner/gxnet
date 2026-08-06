// SPDX-License-Identifier: MIT
#include "gxnet/validate.hpp"

#include "gxnet/registry.hpp"

namespace gxnet {
namespace {

void add(std::vector<Diagnostic>& out, Severity sev, std::string code, std::string message, std::string token = {}) {
    Diagnostic d;
    d.severity = sev;
    d.code = std::move(code);
    d.message = std::move(message);
    d.token = std::move(token);
    out.push_back(std::move(d));
}

void checkToken(const Token& token, const ValidateOptions& opts, std::vector<Diagnostic>& out) {
    const std::string text = token.str();

    if (!token.type()) {
        add(out, Severity::Error, "token.malformed", "token has an unknown data type letter", text);
        return;
    }
    if (!token.group()) {
        // A token from another device family, and that is documented rather
        // than guessed: the R group belongs to IxNet, the sibling language for
        // industrial scale terminals, where RX01 is a weight request and RX04
        // an adding item registration. See docs/markdown/IxNet.md.
        add(out, Severity::Warning, "token.unknown_group",
            std::string("group letter '") + token.group_letter +
                "' is not part of the bundled Gx table; it may belong to "
                "another device family",
            text);
        return;
    }

    auto info = Registry::builtin().find(token);
    if (!info) {
        if (opts.warn_unknown_tokens) {
            add(out, Severity::Warning, "token.unknown",
                "token is absent from the bundled reference table; check for a "
                "typo, or for a subfunction newer than the table",
                text);
        }
        return;
    }

    if (info->reserved && opts.warn_reserved_tokens) {
        add(out, Severity::Warning, "token.reserved",
            std::string(info->name) +
                " is listed as reserved: the reference gives no parameter "
                "description, so its behaviour must be confirmed on the device",
            text);
    }

    if (opts.device_version.valid() && info->since.valid() && info->since > opts.device_version) {
        add(out, Severity::Error, "version.too_new",
            std::string(info->name) + " requires software " + info->since.str() + " but the device runs " +
                opts.device_version.str(),
            text);
    }
}

void checkStructure(const std::vector<Node>& nodes, bool inside_block, std::vector<Diagnostic>& out) {
    for (const Node& n : nodes) {
        if (n.token.isBlockClose()) {
            add(out, Severity::Error, "block.stray_close", "LX02 appears in the tree; block terminators are implicit",
                n.token.str());
        }
        if (n.token.isBlock()) {
            if (n.children.empty()) {
                add(out, Severity::Warning, "block.empty", "block command has no members", n.token.str());
            }
            checkStructure(n.children, true, out);
        } else if (!n.children.empty()) {
            add(out, Severity::Error, "block.children_on_scalar", "only block commands may have children",
                n.token.str());
        }
    }
    (void)inside_block;
}

void checkValueRange(const Token& token, const Value& value, Access access, std::vector<Diagnostic>& out) {
    auto type = token.type();
    if (!type) return;

    // A read request supplies no values; the device fills them in.
    if (isEmpty(value) && access == Access::Read && payloadArity(*type) > 0) {
        return;
    }

    switch (*type) {
        case DataType::Word:
            if (!std::holds_alternative<std::int16_t>(value)) {
                add(out, Severity::Error, "value.type_mismatch", "expected a 16-bit word value", token.str());
            }
            break;
        case DataType::Long:
            if (!std::holds_alternative<std::int32_t>(value)) {
                add(out, Severity::Error, "value.type_mismatch", "expected a 32-bit long value", token.str());
            }
            break;
        case DataType::Dimension:
            if (!std::holds_alternative<Dimension>(value)) {
                add(out, Severity::Error, "value.type_mismatch", "expected a dimensional value", token.str());
            } else if (std::get<Dimension>(value).unit.empty()) {
                add(out, Severity::Warning, "value.unit_missing", "dimensional value has an empty unit", token.str());
            }
            break;
        case DataType::Text:
            if (!std::holds_alternative<std::string>(value)) {
                add(out, Severity::Error, "value.type_mismatch", "expected a text value", token.str());
            }
            break;
        case DataType::Command:
        case DataType::Block:
            if (!isEmpty(value)) {
                add(out, Severity::Error, "value.unexpected", "command and block tokens carry no payload", token.str());
            }
            break;
    }
}

}  // namespace

std::string Diagnostic::str() const {
    std::string out = severity == Severity::Error ? "error" : "warning";
    out += " [" + code + "]";
    if (!token.empty()) out += " " + token;
    out += ": " + message;
    return out;
}

std::vector<Diagnostic> validate(const Header& header, const ValidateOptions& opts) {
    std::vector<Diagnostic> out;
    checkStructure(header.nodes, false, out);
    forEachNode(header.nodes, [&](const Node& n) { checkToken(n.token, opts, out); });
    return out;
}

std::vector<Diagnostic> validate(const Telegram& telegram, const ValidateOptions& opts) {
    std::vector<Diagnostic> out = validate(telegram.header, opts);

    std::vector<Token> tokens = telegram.header.payloadTokens();
    for (std::size_t r = 0; r < telegram.records.size(); ++r) {
        const Record& record = telegram.records[r];
        if (record.size() != tokens.size()) {
            add(out, Severity::Error, "record.arity",
                "record " + std::to_string(r) + " has " + std::to_string(record.size()) +
                    " fields but the header declares " + std::to_string(tokens.size()));
            continue;
        }
        for (std::size_t i = 0; i < record.size(); ++i) {
            checkValueRange(tokens[i], record[i], telegram.header.access, out);
        }
    }

    if (opts.warn_payload_on_read && telegram.header.access == Access::Read) {
        bool has_payload = false;
        for (const Record& record : telegram.records) {
            for (const Value& v : record) {
                if (!isEmpty(v)) {
                    has_payload = true;
                    break;
                }
            }
        }
        if (has_payload) {
            add(out, Severity::Warning, "access.payload_on_read",
                "a read telegram carries payload values; most read requests "
                "send only tokens");
        }
    }

    return out;
}

bool hasNoErrors(const std::vector<Diagnostic>& diags) {
    for (const Diagnostic& d : diags) {
        if (d.severity == Severity::Error) return false;
    }
    return true;
}

}  // namespace gxnet
