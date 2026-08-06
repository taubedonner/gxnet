// SPDX-License-Identifier: MIT
#ifdef _WIN32

#include "dispatch.hpp"

#include <objbase.h>
#include <oleauto.h>

#include <utility>

namespace gxnet::link::win {

std::string toUtf8(const wchar_t* text, int length) {
    if (text == nullptr) return {};
    const int needed = ::WideCharToMultiByte(CP_UTF8, 0, text, length, nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};

    std::string out(static_cast<std::size_t>(needed), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text, length, out.data(), needed, nullptr, nullptr);
    // With length == -1 the count includes the terminating null.
    if (length < 0 && !out.empty() && out.back() == '\0') out.pop_back();
    return out;
}

std::wstring toUtf16(std::string_view text) {
    if (text.empty()) return {};
    const int needed = ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (needed <= 0) return {};

    std::wstring out(static_cast<std::size_t>(needed), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), needed);
    return out;
}

std::string describeHresult(HRESULT hr) {
    LPWSTR buffer = nullptr;
    const DWORD length =
        ::FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                         nullptr, static_cast<DWORD>(hr), 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);

    std::string message;
    if (length > 0 && buffer != nullptr) {
        message = toUtf8(buffer, static_cast<int>(length));
        while (!message.empty() && (message.back() == '\n' || message.back() == '\r' || message.back() == ' ')) {
            message.pop_back();
        }
    }
    if (buffer != nullptr) ::LocalFree(buffer);

    char code[16];
    std::snprintf(code, sizeof(code), "0x%08lX", static_cast<unsigned long>(hr));
    return message.empty() ? std::string(code) : message + " (" + code + ")";
}

// --- Args ------------------------------------------------------------------

Args& Args::in(std::string_view text) {
    Slot slot;
    slot.type = VT_BSTR;
    const std::wstring wide = toUtf16(text);
    slot.bstr = ::SysAllocStringLen(wide.data(), static_cast<UINT>(wide.size()));
    slots_.push_back(slot);
    return *this;
}

Args& Args::inLong(std::int32_t value) {
    Slot slot;
    slot.type = VT_I4;
    slot.i4 = value;
    slots_.push_back(slot);
    return *this;
}

Args& Args::inShort(std::int16_t value) {
    Slot slot;
    slot.type = VT_I2;
    slot.i2 = value;
    slots_.push_back(slot);
    return *this;
}

Args& Args::outString(std::string& destination) {
    Slot slot;
    slot.type = VT_BSTR;
    slot.byref = true;
    slot.out_string = &destination;
    slots_.push_back(slot);
    return *this;
}

Args& Args::outLong(std::int32_t& destination) {
    Slot slot;
    slot.type = VT_I4;
    slot.byref = true;
    slot.out_long = &destination;
    slots_.push_back(slot);
    return *this;
}

Args& Args::outShort(std::int16_t& destination) {
    Slot slot;
    slot.type = VT_I2;
    slot.byref = true;
    slot.out_short = &destination;
    slots_.push_back(slot);
    return *this;
}

// --- Dispatch --------------------------------------------------------------

Dispatch::~Dispatch() { reset(); }

Dispatch::Dispatch(Dispatch&& other) noexcept : dispatch_(other.dispatch_), ids_(std::move(other.ids_)) {
    other.dispatch_ = nullptr;
}

Dispatch& Dispatch::operator=(Dispatch&& other) noexcept {
    if (this != &other) {
        reset();
        dispatch_ = other.dispatch_;
        ids_ = std::move(other.ids_);
        other.dispatch_ = nullptr;
    }
    return *this;
}

void Dispatch::reset() {
    if (dispatch_ != nullptr) {
        dispatch_->Release();
        dispatch_ = nullptr;
    }
    ids_.clear();
}

LinkError Dispatch::create(std::string_view prog_id) {
    reset();

    const std::wstring wide_prog_id = toUtf16(prog_id);
    CLSID clsid{};
    HRESULT hr = ::CLSIDFromProgID(wide_prog_id.c_str(), &clsid);
    if (FAILED(hr)) {
        return {static_cast<int>(hr),
                "no COM class registered as " + std::string(prog_id) + ": " + describeHresult(hr)};
    }

    IDispatch* dispatch = nullptr;
    hr = ::CoCreateInstance(clsid, nullptr, CLSCTX_ALL, IID_IDispatch, reinterpret_cast<void**>(&dispatch));
    if (FAILED(hr) || dispatch == nullptr) {
        return {static_cast<int>(hr), "cannot create " + std::string(prog_id) + ": " + describeHresult(hr)};
    }

    dispatch_ = dispatch;
    return {};
}

LinkError Dispatch::dispatchId(std::string_view method, DISPID& out) {
    if (const auto it = ids_.find(method); it != ids_.end()) {
        out = it->second;
        return {};
    }

    std::wstring wide = toUtf16(method);
    LPOLESTR name = wide.data();
    DISPID id = 0;
    const HRESULT hr = dispatch_->GetIDsOfNames(IID_NULL, &name, 1, LOCALE_USER_DEFAULT, &id);
    if (FAILED(hr)) {
        return {static_cast<int>(hr), "the object has no method named " + std::string(method) +
                                          " -- wrong interface version? " + describeHresult(hr)};
    }

    ids_.emplace(std::string(method), id);
    out = id;
    return {};
}

bool Dispatch::has(std::string_view method) {
    if (dispatch_ == nullptr) return false;
    DISPID id = 0;
    return !dispatchId(method, id);
}

LinkError Dispatch::call(std::string_view method, Args& args, std::int32_t* result) {
    if (dispatch_ == nullptr) {
        return {0, "no COM object; call create() first"};
    }

    DISPID id = 0;
    if (LinkError error = dispatchId(method, id)) return error;

    // Storage for the by-ref slots has to outlive the call, so it stays in
    // args_.slots_ and the VARIANTs point into it.
    const std::size_t count = args.slots_.size();
    std::vector<VARIANT> variants(count);

    for (std::size_t i = 0; i < count; ++i) {
        Args::Slot& slot = args.slots_[i];
        // DISPPARAMS takes arguments in reverse order: rgvarg[0] is the last
        // declared parameter. Getting this wrong produces type mismatches that
        // look like a broken signature.
        VARIANT& variant = variants[count - 1 - i];
        ::VariantInit(&variant);

        if (slot.byref) {
            variant.vt = static_cast<VARTYPE>(slot.type | VT_BYREF);
            switch (slot.type) {
                case VT_BSTR: variant.pbstrVal = &slot.bstr; break;
                case VT_I4: variant.plVal = &slot.i4; break;
                case VT_I2: variant.piVal = &slot.i2; break;
                default: variant.vt = VT_EMPTY; break;
            }
        } else {
            variant.vt = slot.type;
            switch (slot.type) {
                case VT_BSTR: variant.bstrVal = slot.bstr; break;
                case VT_I4: variant.lVal = slot.i4; break;
                case VT_I2: variant.iVal = slot.i2; break;
                default: variant.vt = VT_EMPTY; break;
            }
        }
    }

    DISPPARAMS params{};
    params.rgvarg = variants.empty() ? nullptr : variants.data();
    params.cArgs = static_cast<UINT>(count);
    params.rgdispidNamedArgs = nullptr;
    params.cNamedArgs = 0;

    VARIANT returned;
    ::VariantInit(&returned);
    EXCEPINFO exception{};
    UINT bad_argument = 0;

    const HRESULT hr = dispatch_->Invoke(id, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &params,
                                         result != nullptr ? &returned : nullptr, &exception, &bad_argument);

    if (FAILED(hr)) {
        std::string message = "calling " + std::string(method) + " failed: " + describeHresult(hr);
        if (hr == DISP_E_TYPEMISMATCH || hr == DISP_E_PARAMNOTOPTIONAL) {
            message += "; argument " + std::to_string(bad_argument);
        }
        if (hr == DISP_E_EXCEPTION && exception.bstrDescription != nullptr) {
            message += ": " + toUtf8(exception.bstrDescription);
        }
        if (exception.bstrSource != nullptr) ::SysFreeString(exception.bstrSource);
        if (exception.bstrDescription != nullptr) {
            ::SysFreeString(exception.bstrDescription);
        }
        if (exception.bstrHelpFile != nullptr) {
            ::SysFreeString(exception.bstrHelpFile);
        }
        ::VariantClear(&returned);
        return {static_cast<int>(hr), std::move(message)};
    }

    // Copy out parameters back to caller storage.
    for (Args::Slot& slot : args.slots_) {
        if (!slot.byref) continue;
        if (slot.out_string != nullptr) {
            *slot.out_string =
                slot.bstr != nullptr ? toUtf8(slot.bstr, static_cast<int>(::SysStringLen(slot.bstr))) : std::string{};
        } else if (slot.out_long != nullptr) {
            *slot.out_long = static_cast<std::int32_t>(slot.i4);
        } else if (slot.out_short != nullptr) {
            *slot.out_short = static_cast<std::int16_t>(slot.i2);
        }
    }

    if (result != nullptr) {
        VARIANT converted;
        ::VariantInit(&converted);
        if (SUCCEEDED(::VariantChangeType(&converted, &returned, 0, VT_I4))) {
            *result = static_cast<std::int32_t>(converted.lVal);
        } else {
            *result = 0;
        }
        ::VariantClear(&converted);
    }
    ::VariantClear(&returned);

    // Free the strings we allocated, and any the server handed back.
    for (Args::Slot& slot : args.slots_) {
        if (slot.bstr != nullptr) {
            ::SysFreeString(slot.bstr);
            slot.bstr = nullptr;
        }
    }

    return {};
}

// --- Apartment -------------------------------------------------------------

Apartment::~Apartment() { leave(); }

LinkError Apartment::enter() {
    if (entered_) return {};

    const HRESULT hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    // S_FALSE means this thread was already initialised compatibly, which is
    // fine; it still owes a matching CoUninitialize.
    if (hr == S_OK || hr == S_FALSE) {
        entered_ = true;
        return {};
    }
    if (hr == RPC_E_CHANGED_MODE) {
        // Somebody else already put this thread in the multi-threaded
        // apartment. Saying so beats failing obscurely inside the first call.
        return {static_cast<int>(hr),
                "thread is already in a multi-threaded apartment; the "
                "transport needs an apartment-threaded one"};
    }
    return {static_cast<int>(hr), "CoInitializeEx failed: " + describeHresult(hr)};
}

void Apartment::leave() {
    if (!entered_) return;
    ::CoUninitialize();
    entered_ = false;
}

}  // namespace gxnet::link::win

#endif  // _WIN32
