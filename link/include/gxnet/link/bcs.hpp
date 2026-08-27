// SPDX-License-Identifier: MIT
#ifndef GXNET_LINK_BCS_HPP
#define GXNET_LINK_BCS_HPP

#include <memory>
#include <string>

#include "gxnet/link/transport.hpp"

namespace gxnet::link {

/// Talks to a device through the `_connect.BRAIN` server.
///
/// Available on Windows only. The header declares it everywhere so calling code
/// needs no #ifdef; `available()` says whether this build can actually connect,
/// and `create()` returns nullptr when it cannot.
///
/// Why through the server at all: the connection layer below the telegrams is
/// undocumented -- a post-connect info message, back-synchronisation after
/// sending, bus addressing, a licence check -- and BCS implements all of it.
///
/// Concurrency: opening with `nTelegramType = 0` and `nAccess = 0` is safe
/// alongside another client, because requests are routed by handle and
/// multiplexed by the server. Asking for spontaneous messages or exclusive
/// access takes the device away from the other clients.
class BcsTransport : public Transport {
public:
    struct Options {
        /// The vendor's spelling, and it is not a typo on our side: one m and
        /// two n. `BCS.BCSCommunication.1`, as printed in the manual, does not
        /// exist. Versions `.2` and `.3` are progressively stripped -- only
        /// `.1` has SendOne, ReceiveOne and the receive-queue methods.
        std::string prog_id = "BCS.BCSComunnication.1";

        /// Keep receiving while the server reports "more data", up to this many
        /// rounds. A file export arrives as one header and many records; the
        /// bound is there so a device stuck on status 2 cannot hang the thread.
        int max_receive_rounds = 256;

        /// What `SendOne` expects between the header and the data. Unverified:
        /// the manual never says, so CR/LF is a guess and nothing more. Prefer
        /// `Send`, which takes the two parts separately. See `Request::one_line`.
        std::string send_one_separator = "\r\n";

        /// Ask the server how the device wants text (`IsUnicodeDevice`) when
        /// the connection opens, and report it through textMode().
        ///
        /// On by default, because the setting decides how text is escaped and
        /// getting that wrong shows up as a mangled label rather than an error.
        /// The method is undocumented in both directions -- `Get-Member` prints
        /// `int IsUnicodeDevice (short)` and nothing says whether that short is
        /// in or out -- so this passes it by reference, which may well be wrong.
        ///
        /// SRW_UNICODE_DEVICE (SW85) answers the same question with an ordinary
        /// telegram, so this can be turned off without losing the information.
        /// See `docs/bcs-notes.md`, *An unreachable device does not look like a
        /// timeout*, for the failure it once looked responsible for and was not.
        bool probe_text_mode = true;
    };

    ~BcsTransport() override;

    /// True when this build includes the BCS transport, i.e. on Windows.
    [[nodiscard]] static bool available();

    /// Returns nullptr on platforms without BCS.
    ///
    /// Two overloads rather than a defaulted argument: GCC rejects `= {}` for a
    /// nested type here, and this transport has to build under both compilers.
    [[nodiscard]] static std::unique_ptr<BcsTransport> create(Options options);
    [[nodiscard]] static std::unique_ptr<BcsTransport> create();

    /// The last error the server reported, read back through the `Error`
    /// method. Empty when the server has nothing to add.
    ///
    /// Worth consulting after a failure: the return value of a BCS call says
    /// that something went wrong, this says what.
    [[nodiscard]] virtual std::string lastServerError() = 0;

protected:
    BcsTransport() = default;
};

}  // namespace gxnet::link

#endif  // GXNET_LINK_BCS_HPP
