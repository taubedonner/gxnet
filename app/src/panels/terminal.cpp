// SPDX-License-Identifier: MIT
#include "panels/panels.hpp"

namespace gxdemo::panels {
namespace {

constexpr Token kDisplayBlock = knownToken("WZV_REMOTE_DISPLAY").token;      // WV06
constexpr Token kDisplayAttr = knownToken("WZW_REMOTE_DISPLAY_ATTR").token;  // WW0B
constexpr Token kDisplayText = knownToken("WZT_REMOTE_DISPLAY_TEXT").token;  // WT02

/// WZT_REMOTE_DISPLAY_TEXT is 180 characters. Characters, not bytes.
constexpr std::size_t kDisplayTextLimit = 180;

/// WZW_REMOTE_DISPLAY_ATTR: -1 deletes the text, 0 shows it, 1 flashes it.
constexpr std::int16_t kAttrDelete = -1;
constexpr std::int16_t kAttrNormal = 0;
constexpr std::int16_t kAttrFlashing = 1;

}  // namespace

TerminalPanel::TerminalPanel(wxWindow* parent, Session& session) : Panel(parent, session) {
    auto* root = new wxBoxSizer(wxVERTICAL);

    auto* note = new wxStaticText(this, wxID_ANY,
                                  "Puts a note on the operator's terminal: WZV_REMOTE_DISPLAY with an "
                                  "attribute and a text, closed by LX02.");
    note->SetForegroundColour(kMuted);
    root->Add(note, 0, wxALL, 6);

    // --- text -------------------------------------------------------------

    auto* text_box = new wxStaticBoxSizer(wxVERTICAL, this, "Text");
    wxWindow* tbox = text_box->GetStaticBox();

    text_ = new wxTextCtrl(tbox, wxID_ANY, "gxnet test", wxDefaultPosition, wxSize(-1, 70), wxTE_MULTILINE);
    text_box->Add(text_, 0, wxEXPAND | wxALL, 6);

    counter_ = new wxStaticText(tbox, wxID_ANY, "");
    counter_->SetForegroundColour(kMuted);
    text_box->Add(counter_, 0, wxLEFT | wxBOTTOM, 8);

    root->Add(text_box, 0, wxEXPAND | wxALL, 6);

    // --- attribute --------------------------------------------------------

    auto* attribute_row = new wxBoxSizer(wxHORIZONTAL);
    attribute_row->Add(new wxStaticText(this, wxID_ANY, "Attribute (WW0B)"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

    attribute_ = new wxChoice(this, wxID_ANY);
    attribute_->Append("normal (0)");
    attribute_->Append("flashing (1)");
    attribute_->SetSelection(0);
    attribute_row->Add(attribute_, 0, wxALIGN_CENTER_VERTICAL);

    root->Add(attribute_row, 0, wxALL, 6);

    // --- send -------------------------------------------------------------

    auto* buttons = new wxBoxSizer(wxHORIZONTAL);

    auto* show_button = new wxButton(this, wxID_ANY, "Show");
    buttons->Add(show_button, 0, wxRIGHT, 8);
    needs_connection_.push_back(show_button);
    show_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { show(); });

    auto* clear_button = new wxButton(this, wxID_ANY, "Clear (-1)");
    clear_button->SetToolTip(
        "Sends the attribute -1, which deletes the text.\n"
        "Always available: a note must never be something the operator cannot "
        "get rid of.");
    buttons->Add(clear_button, 0, wxRIGHT, 16);
    needs_connection_.push_back(clear_button);
    clear_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { clear(); });

    status_ = new wxStaticText(this, wxID_ANY, "not shown");
    status_->SetForegroundColour(kMuted);
    buttons->Add(status_, 0, wxALIGN_CENTER_VERTICAL);

    root->Add(buttons, 0, wxALL, 6);

    // --- safety -----------------------------------------------------------

    auto* safety_box = new wxStaticBoxSizer(wxVERTICAL, this, "Safety");
    wxWindow* sbox = safety_box->GetStaticBox();

    auto* auto_row = new wxBoxSizer(wxHORIZONTAL);
    auto_clear_ = new wxCheckBox(sbox, wxID_ANY, "Clear automatically after");
    auto_clear_->SetValue(true);
    auto_row->Add(auto_clear_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);

    auto_clear_after_ =
        new wxSpinCtrl(sbox, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(80, -1), wxSP_ARROW_KEYS, 5, 3600, 60);
    auto_row->Add(auto_clear_after_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    auto_row->Add(new wxStaticText(sbox, wxID_ANY, "s"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 16);

    countdown_ = new wxStaticText(sbox, wxID_ANY, "");
    countdown_->SetForegroundColour(kMuted);
    auto_row->Add(countdown_, 0, wxALIGN_CENTER_VERTICAL);

    safety_box->Add(auto_row, 0, wxALL, 6);

    // A crashed or forgotten controlling process must not leave a dead panel on
    // a running line.
    auto* safety_note = new wxStaticText(sbox, wxID_ANY,
                                         "With this off, the note stays until something clears it, including "
                                         "after this program exits.");
    safety_note->SetForegroundColour(kWarn);
    safety_box->Add(safety_note, 0, wxLEFT | wxBOTTOM, 8);

    root->Add(safety_box, 0, wxEXPAND | wxALL, 6);

    // --- preview ----------------------------------------------------------

    auto* preview_label = new wxStaticText(this, wxID_ANY, "What goes on the wire");
    preview_label->SetForegroundColour(kMuted);
    root->Add(preview_label, 0, wxLEFT | wxTOP, 8);

    preview_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(-1, 60),
                              wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
    preview_->SetFont(wxFont(wxFontInfo(9).Family(wxFONTFAMILY_TELETYPE)));
    root->Add(preview_, 0, wxEXPAND | wxALL, 6);

    auto* unknown = new wxStaticText(this, wxID_ANY,
                                     "A note cannot be answered or dismissed from the terminal. For "
                                     "anything the operator has to respond to, use the Dialogs tab.");
    unknown->SetForegroundColour(kMuted);
    root->Add(unknown, 0, wxALL, 6);

    finishLayout(root);

    text_->Bind(wxEVT_TEXT, [this](wxCommandEvent& event) {
        refreshPreview();
        event.Skip();
    });
    attribute_->Bind(wxEVT_CHOICE, [this](wxCommandEvent& event) {
        refreshPreview();
        event.Skip();
    });

    refreshPreview();
    onConnectionChanged(false);
}

Telegram TerminalPanel::build(std::int16_t attribute, bool with_text) const {
    Builder builder(Family::Automatic, Access::Write);
    builder.block(kDisplayBlock.str());
    builder.word(kDisplayAttr.str(), attribute);
    if (with_text) builder.text(kDisplayText.str(), utf8(text_->GetValue()));
    builder.end();
    return builder.build();
}

void TerminalPanel::refreshPreview() {
    const std::string value = utf8(text_->GetValue());
    const auto length = utf8Length(value);

    if (!length) {
        counter_->SetLabel("not valid UTF-8");
        counter_->SetForegroundColour(kBad);
    } else {
        const bool over = *length > kDisplayTextLimit;
        counter_->SetLabel(wxString::Format("%zu/%zu chars, %zu bytes", *length, kDisplayTextLimit, value.size()));
        counter_->SetForegroundColour(over ? kBad : kMuted);
    }

    const auto attribute = attribute_->GetSelection() == 1 ? kAttrFlashing : kAttrNormal;
    auto encoded = encodeOneLine(build(attribute, /*with_text=*/true));
    preview_->ChangeValue(encoded ? wx(*encoded) : wx(encoded.error.message));
}

void TerminalPanel::show() {
    const std::string value = utf8(text_->GetValue());
    const auto length = utf8Length(value);
    if (!length || *length > kDisplayTextLimit) {
        status_->SetLabel("text is invalid or too long, not sent");
        status_->SetForegroundColour(kBad);
        return;
    }

    const auto attribute = attribute_->GetSelection() == 1 ? kAttrFlashing : kAttrNormal;
    session_.send(build(attribute, /*with_text=*/true), /*expect_reply=*/false,
                  [this](link::LinkResult<link::Exchange> result) {
                      if (result) {
                          on_screen_ = true;
                          shown_for_s_ = 0.0;
                          status_->SetLabel("on screen");
                          status_->SetForegroundColour(kGood);
                      } else {
                          status_->SetLabel("failed: " + wx(result.error.str()));
                          status_->SetForegroundColour(kBad);
                      }
                      Layout();
                  });
}

void TerminalPanel::clear() {
    // One at a time. The answer arrives some frames later, and the auto-clear
    // below would otherwise re-fire on every tick in the meantime.
    if (clearing_) return;
    clearing_ = true;

    session_.send(build(kAttrDelete, /*with_text=*/false), /*expect_reply=*/false,
                  [this](link::LinkResult<link::Exchange> result) {
                      clearing_ = false;
                      if (result) {
                          on_screen_ = false;
                          countdown_->SetLabel("");
                          status_->SetLabel("cleared");
                          status_->SetForegroundColour(kMuted);
                      } else {
                          // Still on the operator's screen. Restart the timer so
                          // the retry comes after another full interval rather
                          // than immediately, every frame.
                          shown_for_s_ = 0.0;
                          status_->SetLabel("clear failed: " + wx(result.error.str()));
                          status_->SetForegroundColour(kBad);
                      }
                      Layout();
                  });
}

void TerminalPanel::onTick(double seconds) {
    if (!on_screen_ || !auto_clear_->GetValue() || !session_.connected()) {
        if (!on_screen_) countdown_->SetLabel("");
        return;
    }

    shown_for_s_ += seconds;
    const double left = auto_clear_after_->GetValue() - shown_for_s_;
    if (left <= 0.0) {
        clear();
        return;
    }
    countdown_->SetLabel(wxString::Format("clearing in %.0f s", left));
}

void TerminalPanel::onConnectionChanged(bool connected) {
    for (wxWindow* control : needs_connection_) control->Enable(connected);

    if (!connected) {
        // The note may well still be on the terminal, but this program can no
        // longer say anything about it -- and must not resume a countdown for
        // something it can no longer clear.
        clearing_ = false;
        shown_for_s_ = 0.0;
        if (on_screen_) {
            status_->SetLabel(
                "disconnected while a note was shown. It is still on the terminal; "
                "reconnect and clear it");
            status_->SetForegroundColour(kWarn);
        }
        on_screen_ = false;
        countdown_->SetLabel("");
        Layout();
    }
}

}  // namespace gxdemo::panels
