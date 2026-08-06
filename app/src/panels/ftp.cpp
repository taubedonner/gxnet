// SPDX-License-Identifier: MIT
#include <cstdlib>

#include "panels/panels.hpp"

namespace gxdemo::panels {
namespace {

/// One of the FTP methods, with the shape the vendor's own material gives it.
///
/// Not guesswork any more. Two independent sources agree:
///
///   - `BCS.tlb`, the type library shipped with the C++ sample, carries the
///     parameter names: `szLocalFilepath`, `szServerFilepath`,
///     `szDirectoryInfo`.
///   - the `_connectService` WCF proxy in the C# sample, generated from the
///     service contract over the same BCS, declares the same family as
///     `DownloadFileFtp(connectName, serverFilePath) -> byte[]`,
///     `UploadFileFtp(connectName, serverFilePath, fileContent)`,
///     `ListFilesOnServerFtp(connectName, serverFilePath) -> List<string>`,
///     `DeleteFileFtp(connectName, serverFilePath)`.
///
/// So the listing does come back from the call, and in COM it can only come
/// back through a parameter. What the type library does not record is the
/// order, and `Get-Member` does not print direction -- hence the presets rather
/// than a fixed signature.
struct Preset {
    const char* method;
    const char* what;
    /// One per argument slot: the index into the direction choice below.
    std::array<int, FtpPanel::kArgs> kinds;
    std::array<const char*, FtpPanel::kArgs> notes;
    std::array<const char*, FtpPanel::kArgs> values;
};

/// Directions, in the order the choice control offers them.
enum Kind { kUnused = 0, kInText = 1, kInLong = 2, kInShort = 3, kOutText = 4, kOutLong = 5, kOutShort = 6 };

/// The incoming directory for unique data, named in the GLM-Emaxx manual.
///
/// "There can be only one file at a time in this directory. After the device
/// has read a file, it immediately deletes the file from the incoming
/// directory." Which is what makes the directory a state signal: a file still
/// sitting there means intake is off or the changeover did not finish.
constexpr const char* kUniqueDir = "/bizstorecard/bizerba/public/uniquePckData";

constexpr std::array<Preset, 5> kPresets{{
    // Measured 2026-08-06, sixteen calls: this shape returns 0 for every path
    // including "" and "/", leaves argument 0 empty, and hands argument 1 back
    // unchanged. A method that succeeds on an empty path is not reporting on
    // the path, so the return value cannot tell one directory from another --
    // and neither argument is being written to.
    //
    // Which leaves the reading the old note here argued for and then got
    // backwards. DownloadFileFTP names its destination first and that
    // destination is *local*; by the same analogy argument 0 is a local file to
    // write the listing into, an in parameter, not an out one. Hence the preset
    // below. If it is right, the earlier attempts wrote the listing to a file
    // named by whatever sat in slot 1.
    {"ListFilesOnServerFTP",
     "what is in the directory. Argument 0 as a LOCAL file to write the listing into, by analogy with "
     "DownloadFileFTP, whose first parameter is also the local side. The out-string shape was tried and "
     "returns 0 for every path without filling anything in",
     {kInText, kInText, kUnused, kUnused},
     {"szLocalFilepath, where the listing is written", "szServerFilepath", "", ""},
     {"C:\\gxnet\\ftp-listing.txt", kUniqueDir, "", ""}},
    {"UploadFileFTP",
     "puts a file on the device. With unique-data intake enabled the machine swallows it immediately",
     {kInText, kInText, kUnused, kUnused},
     {"szLocalFilepath", "szServerFilepath", "", ""},
     {"", kUniqueDir, "", ""}},
    {"DownloadFileFTP",
     "fetches a file back. The safe one to try first: it reads, and the type library names both parameters",
     {kInText, kInText, kUnused, kUnused},
     {"szLocalFilepath", "szServerFilepath", "", ""},
     {"", kUniqueDir, "", ""}},
    {"DeleteFileFTP",
     "removes a file from the device. A leftover file from an aborted changeover is what this is for",
     {kInText, kUnused, kUnused, kUnused},
     {"szServerFilepath", "", "", ""},
     {"", "", "", ""}},
    {"IsUnicodeDevice",
     "already known to work, and it has one out parameter. A control: if this comes back right, the way "
     "arguments are passed is not what is wrong with the others",
     {kOutShort, kUnused, kUnused, kUnused},
     {"bIsUnicodeDevice", "", "", ""},
     {"", "", "", ""}},
}};

}  // namespace

FtpPanel::FtpPanel(wxWindow* parent, Session& session) : Panel(parent, session) {
    auto* root = new wxBoxSizer(wxVERTICAL);

    root->Add(hint(this,
                   "Calls a method on the BCS object directly. Not a telegram: these are the parts of the "
                   "automation surface\n"
                   "that carry files rather than protocol, and none of them appear in the manual."),
              0, wxALL, 6);

    // Order matters and it is not obvious, so it is stated where the button is
    // rather than left in a document.
    auto* danger = new wxStaticText(this, wxID_ANY,
                                    "Upload with unique-data intake disabled (A!GW7D|0 on the Unique data tab). "
                                    "With intake enabled the\n"
                                    "machine reads and deletes a dropped file at once, on top of whatever is "
                                    "already in the buffer.");
    danger->SetForegroundColour(kBad);
    root->Add(danger, 0, wxLEFT | wxRIGHT | wxBOTTOM, 6);

    // --- which method -----------------------------------------------------

    auto* method_row = new wxBoxSizer(wxHORIZONTAL);
    method_row->Add(new wxStaticText(this, wxID_ANY, "Method"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

    method_ = new wxChoice(this, wxID_ANY);
    for (const Preset& preset : kPresets) method_->Append(preset.method);
    method_->Append("something else");
    method_->SetSelection(0);
    method_row->Add(method_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);

    method_name_ = new wxTextCtrl(this, wxID_ANY, kPresets[0].method);
    method_name_->SetToolTip("The name as the object exposes it. Anything on the object can be called from here.");
    method_row->Add(method_name_, 1, wxALIGN_CENTER_VERTICAL);

    root->Add(method_row, 0, wxEXPAND | wxALL, 6);

    auto* what = hint(this, kPresets[0].what);
    root->Add(what, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

    // --- the arguments ----------------------------------------------------

    auto* args_box = new wxStaticBoxSizer(wxVERTICAL, this, "Arguments, in order");
    wxWindow* abox = args_box->GetStaticBox();

    args_box->Add(hint(abox,
                       "The direction is the part the listing does not print. An out parameter is bound to "
                       "storage here and\n"
                       "reported below; flip one and call again to find out which way round a method wants it.\n"
                       "The unique-data directory on the device is /bizstorecard/bizerba/public/uniquePckData, "
                       "and it holds one file at a time."),
                  0, wxALL, 4);

    for (std::size_t i = 0; i < kArgs; ++i) {
        auto* row = new wxBoxSizer(wxHORIZONTAL);

        row->Add(new wxStaticText(abox, wxID_ANY, wxString::Format("%zu", i)), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);

        kind_[i] = new wxChoice(abox, wxID_ANY);
        kind_[i]->Append("not passed");
        kind_[i]->Append("in, string");
        kind_[i]->Append("in, long");
        kind_[i]->Append("in, short");
        kind_[i]->Append("out, string");
        kind_[i]->Append("out, long");
        kind_[i]->Append("out, short");
        kind_[i]->SetSelection(kUnused);
        row->Add(kind_[i], 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

        value_[i] = new wxTextCtrl(abox, wxID_ANY);
        row->Add(value_[i], 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

        note_[i] = valueLabel(abox, wxEmptyString);
        row->Add(note_[i], 1, wxALIGN_CENTER_VERTICAL);

        args_box->Add(row, 0, wxEXPAND | wxALL, 4);

        kind_[i]->Bind(wxEVT_CHOICE, [this, i](wxCommandEvent& event) {
            // An out parameter has nothing to send, so the field for it is not
            // an input any more.
            const int kind = kind_[i]->GetSelection();
            value_[i]->Enable(kind == kInText || kind == kInLong || kind == kInShort);
            event.Skip();
        });
    }

    root->Add(args_box, 0, wxEXPAND | wxALL, 6);

    // --- call and result --------------------------------------------------

    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    call_ = new wxButton(this, wxID_ANY, "Call");
    buttons->Add(call_, 0, wxRIGHT, 12);
    needs_connection_.push_back(call_);
    call_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { call(); });

    status_ = valueLabel(this, "nothing called yet");
    buttons->Add(status_, 1, wxALIGN_CENTER_VERTICAL);
    root->Add(buttons, 0, wxEXPAND | wxALL, 6);

    root->Add(hint(this, "What came back"), 0, wxLEFT | wxTOP, 8);

    result_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(-1, 140),
                             wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
    result_->SetFont(wxFont(wxFontInfo(9).Family(wxFONTFAMILY_TELETYPE)));
    root->Add(result_, 1, wxEXPAND | wxALL, 6);

    finishLayout(root);

    method_->Bind(wxEVT_CHOICE, [this, what](wxCommandEvent& event) {
        const int index = method_->GetSelection();
        applyPreset(index);
        what->SetLabel(index < static_cast<int>(kPresets.size()) ? kPresets[index].what : "");
        Layout();
        FitInside();
        event.Skip();
    });

    applyPreset(0);
    onConnectionChanged(false);
}

void FtpPanel::applyPreset(int index) {
    if (index < 0 || index >= static_cast<int>(kPresets.size())) return;
    const Preset& preset = kPresets[static_cast<std::size_t>(index)];

    method_name_->ChangeValue(preset.method);
    for (std::size_t i = 0; i < kArgs; ++i) {
        kind_[i]->SetSelection(preset.kinds[i]);
        value_[i]->ChangeValue(preset.values[i]);
        const int kind = preset.kinds[i];
        value_[i]->Enable(kind == kInText || kind == kInLong || kind == kInShort);
        show(note_[i], preset.notes[i], kMuted);
    }
}

void FtpPanel::call() {
    const std::string method = utf8(method_name_->GetValue());
    if (method.empty()) {
        show(status_, "no method name", kBad);
        return;
    }

    // A gap in the middle would silently shift everything after it into the
    // wrong position, which is exactly the kind of result that reads as "the
    // method does not work".
    for (std::size_t i = 1; i < kArgs; ++i) {
        if (kind_[i]->GetSelection() != kUnused && kind_[i - 1]->GetSelection() == kUnused) {
            show(status_, wxString::Format("argument %zu is not passed but %zu is: no gaps", i - 1, i), kBad);
            return;
        }
    }

    std::vector<link::CallArg> args;
    for (std::size_t i = 0; i < kArgs; ++i) {
        const std::string text = utf8(value_[i]->GetValue());
        switch (kind_[i]->GetSelection()) {
            case kUnused: break;
            case kInText: args.push_back(link::inText(text)); break;
            case kInLong: args.push_back(link::inLong(std::atoi(text.c_str()))); break;
            case kInShort: args.push_back(link::inShort(static_cast<std::int16_t>(std::atoi(text.c_str())))); break;
            case kOutText: args.push_back(link::outText()); break;
            case kOutLong: args.push_back(link::outLong()); break;
            case kOutShort: args.push_back(link::outShort()); break;
            default: break;
        }
    }

    call_->Enable(false);
    show(status_, "calling " + wx(method) + "...", kMuted);
    result_->Clear();

    session_.call(method, std::move(args), [this, method](link::LinkResult<link::CallResult> result) {
        call_->Enable(session_.connected());

        if (!result) {
            show(status_, "the call itself failed", kBad);
            result_->SetValue(wx(result.error.str()));
            return;
        }

        // Zero is success for every BCS method that returns one, and a non-zero
        // return with no exception is the shape a refused FTP operation is
        // likely to take -- so it is reported as an outcome, not as a failure.
        const bool ok = result->result == 0;
        show(status_, wxString::Format("%s returned %d", method.c_str(), static_cast<int>(result->result)),
             ok ? kGood : kWarn);

        wxString text = wxString::Format("return value: %d\n", static_cast<int>(result->result));
        for (std::size_t i = 0; i < result->arguments.size(); ++i) {
            text += wxString::Format("[%zu] %s\n", i, wx(result->arguments[i]));
        }
        if (!result->server_error.empty()) {
            text += "\nserver: " + wx(result->server_error) + "\n";
        }
        result_->SetValue(text);
    });
}

void FtpPanel::onConnectionChanged(bool connected) {
    for (wxWindow* control : needs_connection_) control->Enable(connected);
}

}  // namespace gxdemo::panels
