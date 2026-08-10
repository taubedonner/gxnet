// SPDX-License-Identifier: MIT
#include "panels/panels.hpp"

namespace gxdemo::panels {
namespace {

// Resolved at compile time: a malformed token, or one absent from the vendor
// reference, is a build error rather than something found out on a live line.
constexpr Token kVersion = knownToken("SRT_GX_VERSION").token;  // ST8D
constexpr Token kDate1 = knownToken("GGL_DATUM1").token;        // GL06
constexpr Token kDate2 = knownToken("GGL_DATUM2").token;        // GL07
constexpr Token kText1 = knownToken("GGT_SIMPLE_TXT1").token;   // GT61
constexpr Token kText2 = knownToken("GGT_SIMPLE_TXT2").token;   // GT62
constexpr Token kText3 = knownToken("GGT_SIMPLE_TXT3").token;   // GT63

/// Plain texts hold 11 characters, 15 from 7.61 and 30 from 14.60. Characters,
/// not bytes: Cyrillic in UTF-8 runs two bytes to the character, and a byte
/// check would reject valid text.
///
/// The last step is printed only in the German edition, and a device past 14.60
/// takes 30 whatever the English one says. Where the release is unknown the
/// short limit stands: the device does not refuse an over-long text, it prints
/// a truncated one.
constexpr std::size_t kPlainTextShort = 15;
constexpr std::size_t kPlainTextLong = 30;
constexpr Version kPlainTextLongSince{14, 60};

/// Room for the longest counter the panel can produce, measured in the font
/// that will actually draw it rather than guessed in pixels -- the widest case
/// is a full Cyrillic text, two bytes to the character.
wxSize counterSize(wxWindow* parent) {
    const wxSize widest = parent->GetTextExtent("30/30 chars, 60 bytes");
    return wxSize(widest.GetWidth() + 8, -1);
}

/// What this device takes, once its release is known.
std::size_t plainTextLimit(const std::optional<Version>& device) {
    return device && *device >= kPlainTextLongSince ? kPlainTextLong : kPlainTextShort;
}

}  // namespace

DevicePanel::DevicePanel(wxWindow* parent, Session& session) : Panel(parent, session) {
    version_.token = kVersion;
    dates_[0].token = kDate1;
    dates_[1].token = kDate2;
    texts_[0].token = kText1;
    texts_[1].token = kText2;
    texts_[2].token = kText3;

    auto* root = new wxBoxSizer(wxVERTICAL);

    auto* read_all = new wxButton(this, wxID_ANY, "Read everything");
    root->Add(read_all, 0, wxALL, 6);
    needs_connection_.push_back(read_all);
    read_all->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { readAll(); });

    // --- version ----------------------------------------------------------

    auto* version_box = new wxStaticBoxSizer(wxVERTICAL, this, "Software version");
    wxWindow* vbox = version_box->GetStaticBox();

    auto* version_row = new wxBoxSizer(wxHORIZONTAL);
    auto* version_label = new wxStaticText(vbox, wxID_ANY, describeToken(kVersion));
    explainToken(version_label, kVersion);
    version_row->Add(version_label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);

    auto* read_version = new wxButton(vbox, wxID_ANY, "Read", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    version_row->Add(read_version, 0, wxALIGN_CENTER_VERTICAL);
    needs_connection_.push_back(read_version);
    version_box->Add(version_row, 0, wxALL, 6);

    // wxEXPAND, because these no longer size themselves to their text: without
    // it they would keep the width of "not read yet" and clip everything else.
    version_value_ = valueLabel(vbox);
    version_box->Add(version_value_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

    version_note_ = valueLabel(vbox);
    show(version_note_, "", kMuted);
    version_box->Add(version_note_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

    root->Add(version_box, 0, wxEXPAND | wxALL, 6);

    read_version->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { readField(version_, version_value_); });

    // --- dates ------------------------------------------------------------

    auto* dates_box = new wxStaticBoxSizer(wxVERTICAL, this, "Dates");
    wxWindow* dbox = dates_box->GetStaticBox();
    dates_box->Add(hint(dbox, "Stored as a long holding DDMMYY."), 0, wxLEFT | wxTOP, 6);

    for (std::size_t i = 0; i < dates_.size(); ++i) {
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        auto* label = new wxStaticText(dbox, wxID_ANY, describeToken(dates_[i].token));
        explainToken(label, dates_[i].token);
        row->Add(label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);

        auto* read = new wxButton(dbox, wxID_ANY, "Read", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
        row->Add(read, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);
        needs_connection_.push_back(read);

        date_values_[i] = valueLabel(dbox);
        row->Add(date_values_[i], 1, wxALIGN_CENTER_VERTICAL);

        dates_box->Add(row, 0, wxEXPAND | wxALL, 6);

        auto* edit_row = new wxBoxSizer(wxHORIZONTAL);
        edit_row->AddSpacer(20);

        date_day_[i] =
            new wxSpinCtrl(dbox, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(60, -1), wxSP_ARROW_KEYS, 1, 31, 1);
        date_month_[i] =
            new wxSpinCtrl(dbox, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(60, -1), wxSP_ARROW_KEYS, 1, 12, 1);
        date_year_[i] =
            new wxSpinCtrl(dbox, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(60, -1), wxSP_ARROW_KEYS, 0, 99, 0);

        edit_row->Add(date_day_[i], 0, wxRIGHT, 4);
        edit_row->Add(new wxStaticText(dbox, wxID_ANY, "."), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        edit_row->Add(date_month_[i], 0, wxRIGHT, 4);
        edit_row->Add(new wxStaticText(dbox, wxID_ANY, "."), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        edit_row->Add(date_year_[i], 0, wxRIGHT, 12);

        auto* write = new wxButton(dbox, wxID_ANY, "Write and verify", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
        edit_row->Add(write, 0);
        needs_connection_.push_back(write);

        dates_box->Add(edit_row, 0, wxLEFT | wxBOTTOM, 6);

        read->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent&) { readField(dates_[i], date_values_[i]); });
        write->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent&) { writeDate(i); });
    }

    root->Add(dates_box, 0, wxEXPAND | wxALL, 6);

    // --- plain texts ------------------------------------------------------

    auto* texts_box = new wxStaticBoxSizer(wxVERTICAL, this, "Plain texts");
    wxWindow* tbox = texts_box->GetStaticBox();

    texts_box->Add(hint(tbox, wxString::Format("GGT_SIMPLE_TXT1..3, %zu characters from 7.61 on and %zu from %s.\n"
                                               "A label layout may already be printing them, and a write "
                                               "replaces whatever is there.",
                                               kPlainTextShort, kPlainTextLong, kPlainTextLongSince.str().c_str())),
                   0, wxALL, 6);

    for (std::size_t i = 0; i < texts_.size(); ++i) {
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        auto* label = new wxStaticText(tbox, wxID_ANY, describeToken(texts_[i].token));
        explainToken(label, texts_[i].token);
        row->Add(label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);

        auto* read = new wxButton(tbox, wxID_ANY, "Read", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
        row->Add(read, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);
        needs_connection_.push_back(read);

        text_values_[i] = valueLabel(tbox);
        row->Add(text_values_[i], 1, wxALIGN_CENTER_VERTICAL);

        texts_box->Add(row, 0, wxEXPAND | wxALL, 6);

        auto* edit_row = new wxBoxSizer(wxHORIZONTAL);
        edit_row->AddSpacer(20);

        text_edits_[i] = new wxTextCtrl(tbox, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(240, -1));
        edit_row->Add(text_edits_[i], 0, wxRIGHT, 8);

        // Fixed width, and no auto-resize. A wxStaticText resizes itself the
        // moment SetLabel is called -- before the sizer runs -- so a counter
        // that grows on every keystroke first covers the button beside it, and
        // then, at the next Layout(), pushes it across the row. Reserving the
        // room the widest counter needs keeps the button where the user left it.
        text_counters_[i] =
            new wxStaticText(tbox, wxID_ANY, wxEmptyString, wxDefaultPosition, counterSize(tbox), wxST_NO_AUTORESIZE);
        text_counters_[i]->SetForegroundColour(kMuted);
        edit_row->Add(text_counters_[i], 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

        auto* write = new wxButton(tbox, wxID_ANY, "Write and verify", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
        edit_row->Add(write, 0);
        needs_connection_.push_back(write);

        texts_box->Add(edit_row, 0, wxLEFT | wxBOTTOM, 6);

        read->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent&) { readField(texts_[i], text_values_[i]); });
        write->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent&) { writeText(i); });

        // The counter has to run on every keystroke: the limit is in
        // characters, and the byte count of Cyrillic is roughly double.
        text_edits_[i]->Bind(wxEVT_TEXT, [this, i](wxCommandEvent& event) {
            const std::string value = utf8(text_edits_[i]->GetValue());
            const auto length = utf8Length(value);
            const std::size_t limit = plainTextLimit(session_.deviceVersion());
            const bool over = length && *length > limit;
            text_counters_[i]->SetLabel(
                wxString::Format("%zu/%zu chars, %zu bytes", length.value_or(0), limit, value.size()));
            text_counters_[i]->SetForegroundColour(over ? kBad : kMuted);
            event.Skip();
        });
    }

    root->Add(texts_box, 0, wxEXPAND | wxALL, 6);

    finishLayout(root);
    onConnectionChanged(false);
}

void DevicePanel::readField(Field& field, wxStaticText* target) {
    field.pending = true;
    field.error.clear();
    show(target, "reading...", kMuted);

    session_.read(field.token, [this, &field, target](link::LinkResult<Value> result) {
        field.pending = false;
        if (result) {
            field.value = *result;
            field.error.clear();
            show(target, describeValue(field.value), wxNullColour);

            if (&field == &version_) {
                if (const auto* text = std::get_if<std::string>(&field.value)) {
                    // Comparing the parsed version against the reference's
                    // "since" tags is what catches a command the firmware would
                    // silently ignore.
                    if (const auto parsed = Version::parse(*text)) {
                        constexpr Version kReadySince = knownToken("SRW_UNIQUE_PCK_DATA_READY").since;
                        if (*parsed < kReadySince) {
                            show(version_note_,
                                 wxString::Format("SW9B needs %s; this device would ignore it", wx(kReadySince.str())),
                                 kBad);
                        } else {
                            show(version_note_,
                                 "parsed as " + wx(parsed->str()) + " and new enough for every command used here",
                                 kMuted);
                        }
                    }
                }
            }

            // Seed the editor from what the device reports, so a write that is
            // meant to change one character does not have to be retyped.
            for (std::size_t i = 0; i < texts_.size(); ++i) {
                if (&field != &texts_[i]) continue;
                if (const auto* text = std::get_if<std::string>(&field.value)) {
                    text_edits_[i]->ChangeValue(wx(*text));
                }
            }
            for (std::size_t i = 0; i < dates_.size(); ++i) {
                if (&field != &dates_[i]) continue;
                if (const auto* raw = std::get_if<std::int32_t>(&field.value)) {
                    const CalendarDate date = fromDeviceDate(*raw);
                    date_day_[i]->SetValue(date.day);
                    date_month_[i]->SetValue(date.month);
                    date_year_[i]->SetValue(date.year);
                    show(target, formatDeviceDate(*raw) + wxString::Format("   (raw %d)", *raw), wxNullColour);
                }
            }
        } else {
            field.error = result.error.str();
            show(target, wx(field.error), kBad);
        }
        Layout();
    });
}

void DevicePanel::readAll() {
    readField(version_, version_value_);
    for (std::size_t i = 0; i < dates_.size(); ++i) {
        readField(dates_[i], date_values_[i]);
    }
    for (std::size_t i = 0; i < texts_.size(); ++i) {
        readField(texts_[i], text_values_[i]);
    }
}

void DevicePanel::writeDate(std::size_t index) {
    CalendarDate date;
    date.day = date_day_[index]->GetValue();
    date.month = date_month_[index]->GetValue();
    date.year = date_year_[index]->GetValue();

    wxStaticText* target = date_values_[index];
    show(target, "writing...", kMuted);

    session_.write(dates_[index].token, Value(toDeviceDate(date)),
                   [this, index, target](link::LinkResult<Value> result) {
                       if (result) {
                           dates_[index].value = *result;
                           if (const auto* raw = std::get_if<std::int32_t>(&*result)) {
                               show(target, formatDeviceDate(*raw) + "   confirmed", wxNullColour);
                           }
                       } else {
                           show(target, wx(result.error.str()), kBad);
                       }
                       Layout();
                   });
}

void DevicePanel::writeText(std::size_t index) {
    const std::string value = utf8(text_edits_[index]->GetValue());

    // Two checks the device would answer for us, but with a mangled label
    // rather than an error: a stray non-UTF-8 byte, and a text past the limit.
    const auto length = utf8Length(value);
    if (!length) {
        show(text_values_[index], "not valid UTF-8, refusing to send", kBad);
        return;
    }
    const std::size_t limit = plainTextLimit(session_.deviceVersion());
    if (*length > limit) {
        show(text_values_[index],
             wxString::Format("%zu characters, limit is %zu, refusing to send", *length, limit), kBad);
        return;
    }

    wxStaticText* target = text_values_[index];
    show(target, "writing...", kMuted);

    session_.write(texts_[index].token, Value(value), [this, index, target](link::LinkResult<Value> result) {
        if (result) {
            texts_[index].value = *result;
            show(target, describeValue(*result) + "   confirmed", wxNullColour);
        } else {
            show(target, wx(result.error.str()), kBad);
        }
        Layout();
    });
}

void DevicePanel::onConnectionChanged(bool connected) {
    for (wxWindow* control : needs_connection_) control->Enable(connected);
}

}  // namespace gxdemo::panels
