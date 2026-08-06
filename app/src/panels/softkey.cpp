// SPDX-License-Identifier: MIT
#include "panels/panels.hpp"

namespace gxdemo::panels {
namespace {

/// WZT_REMOTE_SOFTKEY_TEXT is 20 characters -- and the reference adds that how
/// many actually appear "depends on the width of the characters and of the
/// softkey, and is therefore application dependent". So 20 is a ceiling, not a
/// promise, and a short caption is the safe one.
constexpr std::size_t kLabelLimit = 20;

/// WZW_REMOTE_SOFTKEY_NR: 1 to 16, and 1 to 12 on a GD.
constexpr int kMinKey = 1;
constexpr int kMaxKey = 16;

}  // namespace

SoftkeyPanel::SoftkeyPanel(wxWindow* parent, Session& session) : Panel(parent, session) {
    auto* root = new wxBoxSizer(wxVERTICAL);

    auto* note = new wxStaticText(
        this, wxID_ANY,
        "Programmable keys in the terminal's TERMINAL level: WZV_REMOTE_TO_SOFTKEY (WV04).\n"
        "The operator presses one and the device sends WZV_SOFTKEY_TO_REMOTE (WV05) back.");
    note->SetForegroundColour(kMuted);
    root->Add(note, 0, wxALL, 6);

    // Two things that decide whether anything below is visible at all, and one
    // that decides whether the operator can undo it.
    auto* level = new wxStaticText(
        this, wxID_ANY,
        "These keys exist only in the TERMINAL level, which is authorisation level 9: 'T' before firmware\n"
        "10.00, '9' after. Read the current level with A?WW0C (ASCII in the low byte: '9' = 57, 'T' = 84).\n"
        "A key also claims its position: whatever level 9 had at that number stops working until it is\n"
        "deleted. The operator's way out is <Configuration>/<Communication configuration>/<Del. remote\n"
        "softkeys>, authorisation level 4.");
    level->SetForegroundColour(kWarn);
    root->Add(level, 0, wxLEFT | wxRIGHT | wxBOTTOM, 6);

    // --- the key ----------------------------------------------------------

    auto* key_box = new wxStaticBoxSizer(wxVERTICAL, this, "Key");
    wxWindow* kbox = key_box->GetStaticBox();

    auto* number_row = new wxBoxSizer(wxHORIZONTAL);
    number_row->Add(new wxStaticText(kbox, wxID_ANY, "Number (WW06)"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

    number_ = new wxSpinCtrl(kbox, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(70, -1), wxSP_ARROW_KEYS, kMinKey,
                             kMaxKey, 1);
    number_row->Add(number_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);

    all_keys_ = new wxCheckBox(kbox, wxID_ANY, "all keys (leave WW06 out)");
    all_keys_->SetToolTip(
        "Not a no-op: the reference says that without a number the properties "
        "below apply to every remote softkey at once.");
    number_row->Add(all_keys_, 0, wxALIGN_CENTER_VERTICAL);

    key_box->Add(number_row, 0, wxALL, 6);

    auto* type_row = new wxBoxSizer(wxHORIZONTAL);
    type_row->Add(new wxStaticText(kbox, wxID_ANY, "Type (WW07)"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

    type_ = new wxChoice(kbox, wxID_ANY);
    type_->Append("push button (0)");
    type_->Append("alphanumeric (1)");
    type_->Append("numeric (2)");
    type_->Append("switch (3)");
    type_->Append("date (4)");
    type_->Append("time (5)");
    type_->Append("do not send WW07");
    type_->SetSelection(0);
    type_row->Add(type_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);

    type_row->Add(new wxStaticText(kbox, wxID_ANY, "Digits (WW09)"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    digits_ = new wxSpinCtrl(kbox, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(70, -1), wxSP_ARROW_KEYS, 0, 30,
                             0);
    digits_->SetToolTip("1 to 9 for a numeric key, 0 to 30 for an alphanumeric one. Ignored by a push button.");
    type_row->Add(digits_, 0, wxALIGN_CENTER_VERTICAL);

    key_box->Add(type_row, 0, wxALL, 6);

    auto* label_row = new wxBoxSizer(wxHORIZONTAL);
    label_row->Add(new wxStaticText(kbox, wxID_ANY, "Caption (WT00)"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    label_ = new wxTextCtrl(kbox, wxID_ANY, "gxnet", wxDefaultPosition, wxSize(220, -1));
    label_row->Add(label_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    counter_ = new wxStaticText(kbox, wxID_ANY, "");
    counter_->SetForegroundColour(kMuted);
    label_row->Add(counter_, 0, wxALIGN_CENTER_VERTICAL);
    key_box->Add(label_row, 0, wxALL, 6);

    root->Add(key_box, 0, wxEXPAND | wxALL, 6);

    // --- attribute --------------------------------------------------------

    auto* attr_row = new wxBoxSizer(wxHORIZONTAL);
    attr_row->Add(new wxStaticText(this, wxID_ANY, "Attribute (WW08)"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

    attribute_ = new wxChoice(this, wxID_ANY);
    attribute_->Append("active (1)");
    attribute_->Append("passive (0)");
    attribute_->SetSelection(0);
    attr_row->Add(attribute_, 0, wxALIGN_CENTER_VERTICAL);
    root->Add(attr_row, 0, wxALL, 6);

    // Attribute 3 exists and is deliberately absent from the choice above.
    auto* attr_note = new wxStaticText(this, wxID_ANY,
                                       "The reference also defines attribute 3: active, and every softkey is "
                                       "locked once the telegram goes through.\n"
                                       "Not offered here, because the lock comes off only with XCW_UNLOCK_EING, and one "
                                       "left set by a program that died is a stopped line.");
    attr_note->SetForegroundColour(kWarn);
    root->Add(attr_note, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);

    // --- send -------------------------------------------------------------

    auto* buttons = new wxBoxSizer(wxHORIZONTAL);

    auto* program = new wxButton(this, wxID_ANY, "Program key");
    buttons->Add(program, 0, wxRIGHT, 8);
    needs_connection_.push_back(program);
    program->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { program_(); });

    auto* clear_one = new wxButton(this, wxID_ANY, "Delete this key (-1)");
    buttons->Add(clear_one, 0, wxRIGHT, 8);
    needs_connection_.push_back(clear_one);
    clear_one->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { clear(/*everything=*/false); });

    auto* clear_all = new wxButton(this, wxID_ANY, "Delete all keys");
    clear_all->SetToolTip("Attribute -1 with no key number, which the reference applies to every remote softkey.");
    buttons->Add(clear_all, 0, wxRIGHT, 16);
    needs_connection_.push_back(clear_all);
    clear_all->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { clear(/*everything=*/true); });

    status_ = new wxStaticText(this, wxID_ANY, "nothing sent");
    status_->SetForegroundColour(kMuted);
    buttons->Add(status_, 0, wxALIGN_CENTER_VERTICAL);

    root->Add(buttons, 0, wxALL, 6);

    // --- listening --------------------------------------------------------

    auto* listen_box = new wxStaticBoxSizer(wxVERTICAL, this, "Waiting for a press");
    wxWindow* lbox = listen_box->GetStaticBox();

    auto* listen_row = new wxBoxSizer(wxHORIZONTAL);
    listen_ = new wxButton(lbox, wxID_ANY, "Wait for a press");
    listen_row->Add(listen_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    needs_connection_.push_back(listen_);
    listen_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { listen(); });

    listen_row->Add(new wxStaticText(lbox, wxID_ANY, "for up to"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    wait_s_ = new wxSpinCtrl(lbox, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(80, -1), wxSP_ARROW_KEYS, 5, 600,
                             60);
    listen_row->Add(wait_s_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    listen_row->Add(new wxStaticText(lbox, wxID_ANY, "s"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 16);

    countdown_ = new wxStaticText(lbox, wxID_ANY, "");
    countdown_->SetForegroundColour(kMuted);
    listen_row->Add(countdown_, 0, wxALIGN_CENTER_VERTICAL);

    listen_box->Add(listen_row, 0, wxALL, 6);

    auto* ask_row = new wxBoxSizer(wxHORIZONTAL);
    poll_ = new wxCheckBox(lbox, wxID_ANY, "Ask for the press once a second (A?WV05 by key number)");
    poll_->SetValue(true);
    poll_->SetToolTip(
        "WZV_SOFTKEY_TO_REMOTE carries neither ? nor ! in the reference, so\n"
        "reading it is unspecified rather than forbidden. On a device that\n"
        "answers questions and starts nothing, asking is the only move left.");
    ask_row->Add(poll_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 16);

    info_ = new wxButton(lbox, wxID_ANY, "Read key info (WVA6)");
    info_->SetToolTip(
        "WZV_GXNET_META_SOFTKEY_INFO, 12.00 SP5: per softkey an attribute\n"
        "(0 locked, 1 active) and its label. Answers \"did my key appear\"\n"
        "without walking to the terminal.");
    ask_row->Add(info_, 0, wxALIGN_CENTER_VERTICAL);
    needs_connection_.push_back(info_);
    info_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { readInfo(); });

    listen_box->Add(ask_row, 0, wxLEFT | wxRIGHT | wxBOTTOM, 6);

    auto* listen_note = new wxStaticText(
        lbox, wxID_ANY,
        "The wait no longer occupies the line: the send returns on the acknowledgement, and a press is\n"
        "watched for on the spontaneous channel. That needs both boxes on the Connection tab, the open "
        "flag\nand the listener, and a device willing to send unasked "
        "(GGW_SENDKANAL_A_ENABLE).");
    listen_note->SetForegroundColour(kWarn);
    listen_box->Add(listen_note, 0, wxLEFT | wxBOTTOM, 8);

    answer_ = new wxStaticText(lbox, wxID_ANY, "");
    listen_box->Add(answer_, 0, wxLEFT | wxBOTTOM, 8);

    root->Add(listen_box, 0, wxEXPAND | wxALL, 6);

    // --- preview ----------------------------------------------------------

    auto* preview_label = new wxStaticText(this, wxID_ANY, "What goes on the wire");
    preview_label->SetForegroundColour(kMuted);
    root->Add(preview_label, 0, wxLEFT | wxTOP, 8);

    preview_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(-1, 50),
                              wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
    preview_->SetFont(wxFont(wxFontInfo(9).Family(wxFONTFAMILY_TELETYPE)));
    root->Add(preview_, 0, wxEXPAND | wxALL, 6);

    finishLayout(root);

    const auto rebuild = [this](wxCommandEvent& event) {
        refreshPreview();
        event.Skip();
    };
    label_->Bind(wxEVT_TEXT, rebuild);
    type_->Bind(wxEVT_CHOICE, rebuild);
    attribute_->Bind(wxEVT_CHOICE, rebuild);
    all_keys_->Bind(wxEVT_CHECKBOX, rebuild);
    number_->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent& event) {
        refreshPreview();
        event.Skip();
    });
    digits_->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent& event) {
        refreshPreview();
        event.Skip();
    });

    refreshPreview();
    onConnectionChanged(false);
}

link::SoftkeySpec SoftkeyPanel::spec() const {
    link::SoftkeySpec spec;
    spec.number = all_keys_->GetValue() ? std::nullopt
                                        : std::optional<std::int16_t>(static_cast<std::int16_t>(number_->GetValue()));
    spec.attribute = attribute_->GetSelection() == 1 ? 0 : 1;

    const int chosen = type_->GetSelection();
    if (chosen < 0 || chosen > 5) {
        spec.type = std::nullopt;
    } else {
        spec.type = static_cast<link::SoftkeyType>(static_cast<std::int16_t>(chosen));
    }
    spec.digits = static_cast<std::int16_t>(digits_->GetValue());
    spec.label = utf8(label_->GetValue());
    return spec;
}

void SoftkeyPanel::refreshPreview() {
    const std::string label = utf8(label_->GetValue());
    const auto length = utf8Length(label);
    if (!length) {
        counter_->SetLabel("not valid UTF-8");
        counter_->SetForegroundColour(kBad);
    } else {
        // Characters, not bytes: a caption in Cyrillic is twice the bytes and
        // the same number of characters, and it is characters the device caps.
        const bool over = *length > kLabelLimit;
        counter_->SetLabel(wxString::Format("%zu/%zu chars", *length, kLabelLimit));
        counter_->SetForegroundColour(over ? kBad : kMuted);
    }

    digits_->Enable(type_->GetSelection() >= 0 && type_->GetSelection() <= 5);
    number_->Enable(!all_keys_->GetValue());

    auto encoded = encodeOneLine(link::remoteSoftkey(spec()));
    preview_->ChangeValue(encoded ? wx(*encoded) : wx(encoded.error.message));
}

void SoftkeyPanel::program_() {
    const auto length = utf8Length(utf8(label_->GetValue()));
    if (!length || *length > kLabelLimit) {
        status_->SetLabel("caption is invalid or too long, not sent");
        status_->SetForegroundColour(kBad);
        return;
    }

    // A write. The device acknowledges it by consuming the send, so waiting for
    // a reply would spend the whole timeout on an operation that worked.
    session_.send(link::remoteSoftkey(spec()), /*expect_reply=*/false, [this](link::LinkResult<link::Exchange> result) {
        if (result) {
            status_->SetLabel("programmed");
            status_->SetForegroundColour(kGood);
        } else {
            status_->SetLabel("failed: " + wx(result.error.str()));
            status_->SetForegroundColour(kBad);
        }
        Layout();
    });
}

void SoftkeyPanel::clear(bool everything) {
    const auto number = everything ? std::nullopt
                                   : std::optional<std::int16_t>(static_cast<std::int16_t>(number_->GetValue()));
    session_.send(link::clearSoftkey(number), /*expect_reply=*/false,
                  [this, everything](link::LinkResult<link::Exchange> result) {
                      if (result) {
                          status_->SetLabel(everything ? "all keys deleted" : "key deleted");
                          status_->SetForegroundColour(kGood);
                      } else {
                          status_->SetLabel("failed: " + wx(result.error.str()));
                          status_->SetForegroundColour(kBad);
                      }
                      Layout();
                  });
}

void SoftkeyPanel::listen() {
    if (waiting_) return;

    // There is no "read a softkey press" telegram; a press is sent by the
    // device on its own. So what this does is send the key again -- harmless,
    // it is the same programming telegram -- and then watch the spontaneous
    // channel. If the device answers presses on the request handle after all,
    // the send's own reply is checked for one too.
    waiting_ = true;
    waited_s_ = 0.0;
    since_poll_s_ = 0.0;
    ask_ = Ask::ByNumber;
    listen_->Enable(false);
    answer_->SetLabel("");
    status_->SetLabel("waiting for a press...");
    status_->SetForegroundColour(kMuted);

    if (listener_ == 0) {
        listener_ = session_.listen([this](const link::Exchange& exchange) {
            if (!waiting_) return;
            if (reportPress(exchange)) {
                stopWaiting();
                Layout();
            }
        });
    }

    session_.send(link::remoteSoftkey(spec()), /*expect_reply=*/true,
                  [this](link::LinkResult<link::Exchange> result) {
                      if (!result) {
                          stopWaiting();
                          status_->SetLabel("failed: " + wx(result.error.str()));
                          status_->SetForegroundColour(kBad);
                          Layout();
                          return;
                      }
                      report(*result);
                      Layout();
                  });
}

void SoftkeyPanel::report(const link::Exchange& exchange) {
    if (reportPress(exchange)) {
        stopWaiting();
        return;
    }

    if (!exchange.reply) {
        status_->SetLabel("programmed; watching the spontaneous channel");
        status_->SetForegroundColour(kMuted);
        return;
    }

    // A refusal is the interesting one, so decode what it carried rather than
    // saying "other".
    if (const auto code = link::returnCodeOf(*exchange.reply)) {
        const auto text = link::returnCodeText(*code);
        stopWaiting();
        status_->SetLabel(wxString::Format("refused: LGW_RETURN %d", *code) +
                          (text.empty() ? wxString() : " (" + wx(std::string(text)) + ")"));
        status_->SetForegroundColour(kBad);
        return;
    }

    status_->SetLabel("programmed; watching the spontaneous channel");
    status_->SetForegroundColour(kMuted);
}

bool SoftkeyPanel::reportPress(const link::Exchange& exchange) {
    if (!exchange.reply) return false;
    const auto input = link::parseSoftkeyInput(*exchange.reply);
    if (!input) return false;

    status_->SetLabel("a key was pressed");
    status_->SetForegroundColour(kGood);

    wxString text = wxString::Format("key %d, type %d", input->number, input->type);
    if (input->value) text += wxString::Format(", value %d", *input->value);
    if (input->text) text += ", text " + wx(*input->text);
    answer_->SetLabel(text);
    answer_->SetForegroundColour(kGood);
    return true;
}

void SoftkeyPanel::stopWaiting() {
    waiting_ = false;
    listen_->Enable(session_.connected());
    countdown_->SetLabel("");
}

void SoftkeyPanel::pollPress() {
    if (poll_in_flight_ || ask_ == Ask::None) return;
    poll_in_flight_ = true;

    const Ask asked = ask_;
    std::optional<std::int16_t> number;
    if (asked == Ask::ByNumber) number = static_cast<std::int16_t>(number_->GetValue());

    session_.send(link::softkeyPressQuery(number), /*expect_reply=*/true,
                  [this, asked](link::LinkResult<link::Exchange> result) {
                      poll_in_flight_ = false;
                      if (!waiting_) return;

                      const bool unknown =
                          !result && result.error.message.find("fremdes Kommando") != std::string::npos;
                      if (unknown) {
                          ask_ = asked == Ask::ByNumber ? Ask::Any : Ask::None;
                          if (ask_ == Ask::None) {
                              answer_->SetLabel("WZV_SOFTKEY_TO_REMOTE cannot be read on this device, "
                                                "with or without a key number.");
                              answer_->SetForegroundColour(kWarn);
                              Layout();
                          }
                          return;
                      }
                      if (!result) return;

                      if (reportPress(*result)) {
                          stopWaiting();
                          Layout();
                      }
                  });
}

void SoftkeyPanel::readInfo() {
    session_.send(link::softkeyInfoQuery(), /*expect_reply=*/true,
                  [this](link::LinkResult<link::Exchange> result) {
                      if (!result) {
                          status_->SetLabel("key info failed: " + wx(result.error.str()));
                          status_->SetForegroundColour(kBad);
                          Layout();
                          return;
                      }
                      if (!result->reply) {
                          status_->SetLabel("key info: nothing came back");
                          status_->SetForegroundColour(kWarn);
                          Layout();
                          return;
                      }

                      // The attribute is what answers the question: 0 means the
                      // key is there and locked, 1 that it is there and usable.
                      // Nothing at all means it was never created.
                      static constexpr Token kAttr = knownToken("WZW_REMOTE_SOFTKEY_ATTR").token;  // WW08
                      static constexpr Token kText = knownToken("WZT_LABEL").token;                // WT62
                      const auto attr = link::valueOf(*result->reply, kAttr);
                      const auto label = link::valueOf(*result->reply, kText);

                      if (!attr && !label) {
                          status_->SetLabel("key info: the device listed no softkeys");
                          status_->SetForegroundColour(kWarn);
                      } else {
                          wxString text = "key info:";
                          if (attr) {
                              const auto* word = std::get_if<std::int16_t>(&*attr);
                              text += wxString::Format(" attribute %d (%s)", word ? static_cast<int>(*word) : -1,
                                                       word && *word != 0 ? "active" : "locked");
                          }
                          if (label) text += "   |   " + wx(encodeValue(*label));
                          status_->SetLabel(text);
                          status_->SetForegroundColour(kGood);
                      }
                      Layout();
                  });
}

void SoftkeyPanel::onTick(double seconds) {
    if (!waiting_) return;
    waited_s_ += seconds;

    if (poll_->GetValue()) {
        since_poll_s_ += seconds;
        if (since_poll_s_ >= 1.0) {
            since_poll_s_ = 0.0;
            pollPress();
        }
    }

    const int left = wait_s_->GetValue() - static_cast<int>(waited_s_);
    if (left <= 0) {
        stopWaiting();
        status_->SetLabel("no press within the wait");
        status_->SetForegroundColour(kMuted);
        answer_->SetLabel(session_.settings().spontaneous
                              ? "Nothing arrived on the spontaneous channel. Check GGW_SENDKANAL_A_ENABLE (A?GWBF): "
                                "if it is 0 the device never sends unasked on this channel."
                              : "The connection did not subscribe to spontaneous messages, so a press had nowhere "
                                "to arrive. Tick that on the Connection tab and reconnect.");
        answer_->SetForegroundColour(kMuted);
        Layout();
        return;
    }
    countdown_->SetLabel(wxString::Format("%d s left", left));
}

SoftkeyPanel::~SoftkeyPanel() {
    if (listener_ != 0) session_.unlisten(listener_);
}

void SoftkeyPanel::onConnectionChanged(bool connected) {
    for (wxWindow* window : needs_connection_) window->Enable(connected);
    if (!connected) {
        waiting_ = false;
        countdown_->SetLabel("");
    }
}

}  // namespace gxdemo::panels
