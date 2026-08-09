// SPDX-License-Identifier: MIT
#ifndef GXNET_LINK_TRANSPORT_HPP
#define GXNET_LINK_TRANSPORT_HPP

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "gxnet/codec.hpp"
#include "gxnet/escape.hpp"
#include "gxnet/telegram.hpp"

/// Transport layer: what carries a telegram to a device and back.
///
/// The library proper contains no transport, deliberately. This layer adds one,
/// and it does so through the vendor's own server rather than by speaking to
/// the device directly -- the connection layer below the telegrams is
/// undocumented (post-connect info message, back-synchronisation, bus
/// addressing, licence check) and `_connect.BRAIN` already implements it.
///
/// What travels across this interface is a Telegram, not bytes. That is not a
/// simplification: the vendor manual states that commands and data are
/// transmitted "as ASCII text in BxNet message format", and every BCS method
/// takes and returns BSTR. The compact binary form lives between the server and
/// the device, where we never see it, and `gxnet::binary` exists to read logs of
/// it -- not to speak it.
namespace gxnet::link {

/// Parameters of BCS `Open`.
///
/// The defaults are the ones that are safe where another client is already
/// talking to the device: shared access, no spontaneous messages. Changing
/// either takes the device away from the other clients.
struct Endpoint {
    /// Free-form; the server uses it to attribute errors.
    std::string user = "gxnet";
    /// The system name as configured in `_connectConfig`.
    std::string device;
    /// `nTelegramType`: true asks for spontaneous messages, which only one
    /// client may do at a time.
    bool spontaneous = false;
    /// `nAccess`: true claims the device exclusively.
    bool exclusive = false;
    /// `bLightLicenceEnable`.
    bool light_licence = false;
};

/// Transfer status, as reported by BCS in `lStatus`.
enum class Status {
    Ok = 0,
    Timeout = 1,
    /// Further records are waiting; receive again. This is how multi-record
    /// answers such as a buffer read arrive.
    MoreData = 2,
};

const char* statusName(Status status);

/// A transport failure, as distinct from a malformed telegram.
///
/// `code` carries whatever the layer below reported -- an HRESULT, a BCS return
/// value -- so it can be looked up in the vendor's error catalogue instead of
/// being flattened into prose.
struct LinkError {
    int code = 0;
    std::string message;

    explicit operator bool() const { return code != 0 || !message.empty(); }
    std::string str() const;
};

template<typename T>
struct LinkResult {
    std::optional<T> value;
    LinkError error;

    bool ok() const { return value.has_value(); }
    explicit operator bool() const { return ok(); }
    const T& operator*() const { return *value; }
    const T* operator->() const { return &*value; }

    static LinkResult fail(LinkError error) { return {std::nullopt, std::move(error)}; }
    static LinkResult of(T value) { return {std::move(value), {}}; }
};

/// One request to the device.
struct Request {
    Telegram telegram;

    /// How long the server waits for the device before giving up. A read on a
    /// busy line can take noticeably longer than the send itself.
    std::chrono::milliseconds timeout{3000};

    /// Whether an answer is expected. Reads always answer; for writes the shape
    /// of the acknowledgement is not documented, so the caller decides whether
    /// to wait for one. Read back with a separate read telegram instead of
    /// trusting what a write returns.
    bool expect_reply = true;

    /// Interleaved single-line form (`SendOne`) versus separate header and data
    /// lines (`Send`).
    ///
    /// `Send` by default, settled by the server's own log rather than
    /// preference. `SendOne` has been seen to fail with
    ///
    ///     2712 (BCS_GX) Telegrammaufbau ist fehlerhaft
    ///     source: CConvDataToBxNetBase::SeperteHeaderData
    ///
    /// -- "separate header data": the server takes the single string apart into
    /// a header and a data part, and our interleaved encoding gives it nothing
    /// to split on. `Send` hands over the two parts already separated, so that
    /// step does not arise. What `SendOne` actually wants between them is not
    /// documented; `BcsTransport::Options::send_one_separator` is the guess,
    /// and it is a guess.
    bool one_line = false;
};

/// What came back.
struct Exchange {
    /// Exactly what was handed to the transport, for the log.
    std::vector<std::string> sent;
    /// Exactly what came back, before parsing.
    std::vector<std::string> received;
    /// The parsed answer, when there was one and it parsed.
    std::optional<Telegram> reply;
    /// Parse failure of an answer that did arrive. An unparseable answer is
    /// worth surfacing rather than discarding: it is evidence about the device.
    std::optional<CodecError> reply_error;

    Status status = Status::Ok;

    /// `lStatus` as `Send` itself reported it, before any receiving.
    ///
    /// Kept apart from `status` because the two answer different questions and
    /// the vendor's samples turn on this one: they receive only while `Send`
    /// said 2, and this code receives whenever the caller asked for a reply.
    /// Which is right cannot be settled without seeing the value, so the value
    /// is carried rather than folded away.
    Status send_status = Status::Ok;

    std::chrono::milliseconds elapsed{0};

    /// The send timed out and was cancelled with `Reset`.
    ///
    /// Worth reporting rather than folding into the timeout: it says the
    /// request was taken off the server's transmission list, so a late answer
    /// from the device will arrive as a spontaneous record belonging to
    /// nothing.
    bool reset_after_timeout = false;
};

/// One argument of a direct call on the object underneath the transport.
///
/// The direction is the caller's to choose, and that is the whole point. The
/// automation object exposes a family of FTP methods that appear in no manual,
/// and `Get-Member` prints only a positional type list:
///
///     int ListFilesOnServerFTP (string, string)
///
/// -- with no hint that one of those two strings is where the answer comes
/// back. Trying it both ways is the only way to find out, so the shape of the
/// call is not fixed in code.
struct CallArg {
    enum class Kind { InText, InLong, InShort, OutText, OutLong, OutShort };

    Kind kind = Kind::InText;
    std::string text;
    std::int32_t number = 0;

    [[nodiscard]] bool isOut() const {
        return kind == Kind::OutText || kind == Kind::OutLong || kind == Kind::OutShort;
    }
};

[[nodiscard]] CallArg inText(std::string value);
[[nodiscard]] CallArg inLong(std::int32_t value);
[[nodiscard]] CallArg inShort(std::int16_t value);
[[nodiscard]] CallArg outText();
[[nodiscard]] CallArg outLong();
[[nodiscard]] CallArg outShort();

/// What a direct call produced.
struct CallResult {
    /// The method's own return value. Zero is success for every BCS method
    /// that returns one.
    std::int32_t result = 0;
    /// One entry per argument, in the order given: what an out parameter came
    /// back with, and the unchanged input for the rest. Keeping all of them
    /// makes it visible when a parameter marked in was written to anyway.
    std::vector<std::string> arguments;
    /// What the object's own `Error` reported afterwards, when it reported
    /// anything at all.
    std::string server_error;
};

/// A connection to one device.
///
/// Implementations are not required to be thread safe; drive one from a single
/// thread, or put a queue in front of it (see `link::Worker`).
class Transport {
public:
    virtual ~Transport() = default;

    /// Called on the thread that will drive this transport, before anything
    /// else, and its counterpart when that thread finishes.
    ///
    /// The transport states its own thread requirements instead of the caller
    /// guessing them: `BcsTransport` initialises COM here, the mock needs
    /// nothing. `Worker` calls both at the right moments.
    virtual LinkError onThreadStart() { return {}; }
    virtual void onThreadStop() {}

    virtual LinkError open(const Endpoint& endpoint) = 0;
    virtual void close() = 0;
    [[nodiscard]] virtual bool isOpen() const = 0;

    virtual LinkResult<Exchange> execute(const Request& request) = 0;

    /// The queue the server files spontaneous records under when the client has
    /// not made one of its own. Not a description of what happened to them: it
    /// is the literal handle to pass to a receive method. See
    /// `docs/bcs-notes.md`, section 1.
    static constexpr const char* kDefaultQueue = "DUSTBIN";

    /// Collects one record the device sent of its own accord, if one is waiting.
    ///
    /// The distinction that matters: `execute` receives against the handle
    /// `Send` returned, so it can only ever collect the answer to its own
    /// request. A dialog result, a softkey press, a package record -- anything
    /// the device decides to send when nothing was asked -- belongs to no send
    /// handle and is filed under a receive queue instead. Nobody collects those
    /// unless something calls this.
    ///
    /// A timeout is not a failure here: it is the normal answer, meaning nothing
    /// was waiting. Callers should expect to see it almost every time. The
    /// returned Exchange has `received` empty in that case.
    ///
    /// Keep the timeout short. The transport runs on one thread and this
    /// occupies it for the whole wait, so a long poll delays every request
    /// behind it.
    virtual LinkResult<Exchange> receiveSpontaneous(const std::string& queue, std::chrono::milliseconds timeout);

    /// Calls a method on the object underneath, by name.
    ///
    /// Not a telegram: this is for the parts of the automation surface that are
    /// not the protocol at all -- the FTP family, `DeviceTest`, `GetCategory`.
    /// They are undocumented, so what this offers is the ability to try one,
    /// not a tidy wrapper around a known signature.
    ///
    /// The default is a refusal, which is the honest answer for a transport
    /// with no automation object behind it.
    virtual LinkResult<CallResult> call(const std::string& method, const std::vector<CallArg>& args);

    /// How the device wants text, when the transport can tell. The BCS
    /// automation object exposes `IsUnicodeDevice`, which answers this without
    /// a telegram; returns nullopt when unknown.
    [[nodiscard]] virtual std::optional<TextMode> textMode() const { return std::nullopt; }

    /// Human-readable identification, for the connection indicator.
    [[nodiscard]] virtual std::string description() const = 0;

    /// Escape options implied by `textMode()`, defaulting to the conservative
    /// choice when the mode is unknown.
    [[nodiscard]] EscapeOptions escapeOptions() const {
        const auto mode = textMode();
        return EscapeOptions::forMode(mode.value_or(TextMode::UnicodeDevice));
    }
};

/// Convenience: send a read telegram for one token and return the value the
/// device reported for it. The common shape of "ask the device one thing".
LinkResult<Value> readOne(Transport& transport, Token token,
                          std::chrono::milliseconds timeout = std::chrono::milliseconds{3000});

/// Convenience: write one value, then read it back and confirm it took.
///
/// A successful send is not evidence that anything happened -- the server
/// accepting a telegram says nothing about the device acting on it. Every write
/// that matters should go through here.
LinkResult<Value> writeAndVerify(Transport& transport, Token token, Value value,
                                 std::chrono::milliseconds timeout = std::chrono::milliseconds{3000});

}  // namespace gxnet::link

#endif  // GXNET_LINK_TRANSPORT_HPP
