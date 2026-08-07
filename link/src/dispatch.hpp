// SPDX-License-Identifier: MIT
#ifndef GXNET_LINK_DISPATCH_HPP
#define GXNET_LINK_DISPATCH_HPP

#ifdef _WIN32

// Ahead of every Windows header, including the ones others pull in: NOMINMAX
// stops min and max being defined as macros, which turns a later std::min into
// std::(...) and an error a long way from the cause. The build also defines it
// on the command line, because a header cannot win an include-order race with
// whatever included it first.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <oaidl.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "gxnet/link/transport.hpp"

/// Minimal late-binding COM, sized to exactly what BCS needs.
///
/// Late binding rather than `#import`, for two reasons. The type library is not
/// guaranteed to be present on a build machine, and the installation exposes
/// three interface versions of which only `.1` is complete -- binding by name
/// at run time keeps a version mismatch a clear error instead of a link
/// failure. What the vendor tooling offers instead (`_com_dispatch_method`,
/// ATL's CComDispatchDriver) is MSVC-specific and reports failure by throwing,
/// which does not fit a Result-returning API.
///
/// Everything here is UTF-8 at the boundary: BSTR is UTF-16, and the conversion
/// uses CP_UTF8 in both directions. Never CP_ACP -- it silently mangles
/// anything outside the active code page, which on a Russian installation means
/// every label text.
namespace gxnet::link::win {

std::string toUtf8(const wchar_t* text, int length = -1);
std::wstring toUtf16(std::string_view text);

/// Human-readable form of an HRESULT, including the vendor's own facility
/// codes when the system has a message for them.
std::string describeHresult(HRESULT hr);

/// Argument list for one late-bound call.
///
/// Out parameters are bound to caller storage: `outString(handle)` records
/// where the returned BSTR should end up, and the value is written back after
/// the call returns. This is the part `Get-Member` output hides -- it prints
/// `Send (string, string, string, int, int)` with no hint that the third and
/// fifth are outputs, so they are named explicitly here.
class Args {
public:
    Args& in(std::string_view text);
    Args& inLong(std::int32_t value);
    Args& inShort(std::int16_t value);

    Args& outString(std::string& destination);
    Args& outLong(std::int32_t& destination);
    Args& outShort(std::int16_t& destination);

private:
    friend class Dispatch;

    struct Slot {
        VARTYPE type = VT_EMPTY;
        bool byref = false;

        BSTR bstr = nullptr;
        LONG i4 = 0;
        SHORT i2 = 0;

        std::string* out_string = nullptr;
        std::int32_t* out_long = nullptr;
        std::int16_t* out_short = nullptr;
    };

    std::vector<Slot> slots_;
};

/// An IDispatch pointer with by-name invocation.
class Dispatch {
public:
    Dispatch() = default;
    ~Dispatch();

    Dispatch(Dispatch&& other) noexcept;
    Dispatch& operator=(Dispatch&& other) noexcept;
    Dispatch(const Dispatch&) = delete;
    Dispatch& operator=(const Dispatch&) = delete;

    /// Creates the object registered under `prog_id`, e.g.
    /// "BCS.BCSComunnication.1" -- note the vendor's spelling, one m and two n.
    LinkError create(std::string_view prog_id);

    void reset();
    [[nodiscard]] bool valid() const { return dispatch_ != nullptr; }

    /// Calls a method by name. `result` receives the return value when the
    /// method has one; pass nullptr when it does not.
    LinkError call(std::string_view method, Args& args, std::int32_t* result = nullptr);

    /// True when the object exposes `method`. Used to tell the interface
    /// versions apart: only `.1` has SendOne and the receive-queue methods.
    [[nodiscard]] bool has(std::string_view method);

private:
    LinkError dispatchId(std::string_view method, DISPID& out);

    IDispatch* dispatch_ = nullptr;
    std::map<std::string, DISPID, std::less<>> ids_;
};

/// COM apartment for the calling thread, initialised on construction and
/// uninitialised on destruction.
///
/// Apartment-threaded, and the transport is only ever driven from the one
/// thread that owns this -- which is what `Worker` guarantees.
class Apartment {
public:
    Apartment() = default;
    ~Apartment();

    Apartment(const Apartment&) = delete;
    Apartment& operator=(const Apartment&) = delete;

    LinkError enter();
    void leave();

private:
    bool entered_ = false;
};

}  // namespace gxnet::link::win

#endif  // _WIN32
#endif  // GXNET_LINK_DISPATCH_HPP
