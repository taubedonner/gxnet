// SPDX-License-Identifier: MIT
#ifndef GXNET_VALIDATE_HPP
#define GXNET_VALIDATE_HPP

#include <string>
#include <vector>

#include "gxnet/telegram.hpp"
#include "gxnet/version.hpp"

namespace gxnet {

enum class Severity { Warning, Error };

/// One validation finding.
struct Diagnostic {
    Severity severity = Severity::Error;
    std::string code;     ///< stable identifier, e.g. "version.too_new"
    std::string message;  ///< human readable explanation
    std::string token;    ///< token the finding relates to, if any

    std::string str() const;
};

struct ValidateOptions {
    /// Software release running on the target device. When set, tokens
    /// introduced in a later release are reported. Leave default to skip the
    /// check.
    Version device_version{};

    /// Report tokens absent from the registry. Useful to catch typos; turn off
    /// if you deliberately use subfunctions newer than the bundled table.
    bool warn_unknown_tokens = true;

    /// Report tokens the reference lists without any description.
    bool warn_reserved_tokens = true;

    /// Report read telegrams (`?`) that carry payload values.
    bool warn_payload_on_read = true;
};

/// Checks structure, arity, ranges and version availability.
///
/// Structural problems are errors: an unbalanced block, a record whose field
/// count disagrees with the header, a Word outside 16-bit range. Registry
/// findings are warnings, because the bundled table reflects one revision of
/// the reference and a device may legitimately be newer.
std::vector<Diagnostic> validate(const Telegram& telegram, const ValidateOptions& opts = {});

std::vector<Diagnostic> validate(const Header& header, const ValidateOptions& opts = {});

/// True if no diagnostic has Error severity.
bool hasNoErrors(const std::vector<Diagnostic>& diags);

}  // namespace gxnet

#endif  // GXNET_VALIDATE_HPP
