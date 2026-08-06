// SPDX-License-Identifier: MIT
#include "gxnet/link/bcs.hpp"
#include "panels/panels.hpp"

namespace gxdemo::panels {
namespace {}  // namespace

ConnectionPanel::ConnectionPanel(wxWindow* parent, Session& session) : Panel(parent, session) {
    auto* root = new wxBoxSizer(wxVERTICAL);

    // --- transport --------------------------------------------------------

    auto* transport_box = new wxStaticBoxSizer(wxVERTICAL, this, "Transport");

    transport_ = new wxChoice(transport_box->GetStaticBox(), wxID_ANY);
    transport_->Append("In-memory device");
    transport_->Append("_connect.BRAIN (BCS)");
    transport_->SetSelection(session_.settings().kind == Session::Kind::Bcs ? 1 : 0);
    transport_box->Add(transport_, 0, wxALL, 4);

    if (!link::BcsTransport::available()) {
        transport_box->Add(hint(transport_box->GetStaticBox(), "This build has no BCS transport: it is Windows only."),
                           0, wxLEFT | wxBOTTOM, 6);
    }
    transport_box->Add(hint(transport_box->GetStaticBox(),
                            "The in-memory device remembers what is written to it and reports\n"
                            "it back. Enough to exercise every sequence, including failures."),
                       0, wxLEFT | wxBOTTOM, 6);

    root->Add(transport_box, 0, wxEXPAND | wxALL, 6);

    // --- device -----------------------------------------------------------

    auto* device_box = new wxStaticBoxSizer(wxVERTICAL, this, "Device");
    wxWindow* box = device_box->GetStaticBox();

    auto* grid = new wxFlexGridSizer(2, 6, 6);
    grid->AddGrowableCol(1, 1);

    grid->Add(new wxStaticText(box, wxID_ANY, "System name"), 0, wxALIGN_CENTER_VERTICAL);
    device_ = new wxTextCtrl(box, wxID_ANY, wx(session_.settings().device));
    device_->SetToolTip("The name as configured in _connectConfig.");
    grid->Add(device_, 1, wxEXPAND);

    grid->Add(new wxStaticText(box, wxID_ANY, "User"), 0, wxALIGN_CENTER_VERTICAL);
    user_ = new wxTextCtrl(box, wxID_ANY, wx(session_.settings().user));
    user_->SetToolTip("Free-form; the server uses it to attribute errors.");
    grid->Add(user_, 1, wxEXPAND);

    grid->Add(new wxStaticText(box, wxID_ANY, "ProgID"), 0, wxALIGN_CENTER_VERTICAL);
    prog_id_ = new wxTextCtrl(box, wxID_ANY, wx(session_.settings().prog_id));
    prog_id_->SetToolTip(
        "BCS.BCSComunnication.1 is the vendor's spelling: one m, two n.\n"
        "The manual's BCS.BCSCommunication.1 does not exist.\n"
        "Versions .2 and .3 are stripped: no SendOne, no ReceiveOne.");
    grid->Add(prog_id_, 1, wxEXPAND);

    grid->Add(new wxStaticText(box, wxID_ANY, "Timeout, ms"), 0, wxALIGN_CENTER_VERTICAL);
    timeout_ = new wxSpinCtrl(box, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(120, -1), wxSP_ARROW_KEYS, 100,
                              120000, session_.settings().timeout_ms);
    grid->Add(timeout_, 0);

    device_box->Add(grid, 0, wxEXPAND | wxALL, 6);
    root->Add(device_box, 0, wxEXPAND | wxALL, 6);

    // --- open flags -------------------------------------------------------

    auto* flags_box = new wxStaticBoxSizer(wxVERTICAL, this, "Open flags");
    wxWindow* flags = flags_box->GetStaticBox();

    spontaneous_ = new wxCheckBox(flags, wxID_ANY, "Spontaneous messages (nTelegramType = 1)");
    spontaneous_->SetToolTip(
        "Only one client can receive spontaneous messages at a time.\n"
        "Here package data arrives through the memory-card buffer instead,\n"
        "so this is not needed.");
    flags_box->Add(spontaneous_, 0, wxALL, 4);

    exclusive_ = new wxCheckBox(flags, wxID_ANY, "Exclusive access (nAccess = 1)");
    exclusive_->SetToolTip(
        "Locks every other client out of the device.\n"
        "Shared access is what is safe here.");
    flags_box->Add(exclusive_, 0, wxALL, 4);

    probe_text_mode_ = new wxCheckBox(flags, wxID_ANY, "Ask the server for the device's Unicode setting");
    probe_text_mode_->SetValue(session_.settings().probe_text_mode);
    probe_text_mode_->SetToolTip(
        "Calls the undocumented IsUnicodeDevice after opening, so escaping is "
        "right from the first telegram.\n"
        "Not required: SRW_UNICODE_DEVICE (SW85) answers the same question "
        "with an ordinary telegram.");
    flags_box->Add(probe_text_mode_, 0, wxALL, 4);

    use_send_one_ = new wxCheckBox(flags, wxID_ANY, "Send header and data as one string (SendOne)");
    use_send_one_->SetValue(session_.settings().use_send_one);
    use_send_one_->SetToolTip(
        "Off: Send(header, data), the two parts passed separately. This is "
        "what works here.\n"
        "On: SendOne, one string the server splits itself. It is rejected on "
        "this installation with\n"
        "\"Telegrammaufbau ist fehlerhaft\", and what it wants between the "
        "parts is undocumented.");
    flags_box->Add(use_send_one_, 0, wxALL, 4);

    listen_ = new wxCheckBox(flags, wxID_ANY, "Collect spontaneous records (poll DUSTBIN)");
    listen_->SetValue(session_.settings().listen);
    listen_->SetToolTip(
        "Receives against the DUSTBIN queue whenever the line is idle, which is\n"
        "the only way a record the device sent unasked ever gets read: an\n"
        "ordinary receive is bound to the handle its own Send returned.\n"
        "Needs the flag above; without it the server has nothing to file.");
    flags_box->Add(listen_, 0, wxALL, 4);

    warning_ = new wxStaticText(flags, wxID_ANY, "");
    warning_->SetForegroundColour(kWarn);
    flags_box->Add(warning_, 0, wxALL, 4);

    root->Add(flags_box, 0, wxEXPAND | wxALL, 6);

    // --- connection -------------------------------------------------------

    auto* state_box = new wxStaticBoxSizer(wxVERTICAL, this, "Connection");
    wxWindow* state = state_box->GetStaticBox();

    connect_ = new wxButton(state, wxID_ANY, "Connect");
    state_box->Add(connect_, 0, wxALL, 6);

    status_ = new wxStaticText(state, wxID_ANY, "not connected");
    state_box->Add(status_, 0, wxLEFT | wxBOTTOM, 8);

    spontaneous_count_ = new wxStaticText(state, wxID_ANY, "");
    spontaneous_count_->SetToolTip(
        "Records the device sent of its own accord, and polls made looking for\n"
        "them. Polls rising while records stay at zero is the useful reading:\n"
        "the listener works and the device is silent.");
    state_box->Add(spontaneous_count_, 0, wxLEFT | wxBOTTOM, 8);

    text_mode_ = new wxStaticText(state, wxID_ANY, "");
    text_mode_->SetToolTip(
        "Reported by IsUnicodeDevice, so escaping is right from the first\n"
        "telegram. Sending the wrong form is a diagnosable error on the\n"
        "device side (17194 / 17195).");
    state_box->Add(text_mode_, 0, wxLEFT | wxBOTTOM, 8);

    root->Add(state_box, 0, wxEXPAND | wxALL, 6);

    finishLayout(root);

    connect_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (session_.connected()) {
            session_.disconnect();
        } else {
            pullSettings();
            session_.connect();
        }
        refreshStatus();
    });

    const auto note_flags = [this](wxCommandEvent&) {
        if (spontaneous_->GetValue() || exclusive_->GetValue()) {
            warning_->SetLabel("These flags disturb other clients on a live line.");
        } else {
            warning_->SetLabel("");
        }
        Layout();
    };
    spontaneous_->Bind(wxEVT_CHECKBOX, note_flags);
    exclusive_->Bind(wxEVT_CHECKBOX, note_flags);

    // Takes effect immediately rather than at the next open: it is the one
    // setting here that is about what this program does, not about what the
    // server was asked for.
    listen_->Bind(wxEVT_CHECKBOX,
                  [this](wxCommandEvent&) { session_.settings().listen = listen_->GetValue(); });

    refreshStatus();
}

void ConnectionPanel::pullSettings() {
    Session::Settings& settings = session_.settings();
    settings.kind = transport_->GetSelection() == 1 ? Session::Kind::Bcs : Session::Kind::Mock;
    settings.device = utf8(device_->GetValue());
    settings.user = utf8(user_->GetValue());
    settings.prog_id = utf8(prog_id_->GetValue());
    settings.timeout_ms = timeout_->GetValue();
    settings.spontaneous = spontaneous_->GetValue();
    settings.exclusive = exclusive_->GetValue();
    settings.probe_text_mode = probe_text_mode_->GetValue();
    settings.use_send_one = use_send_one_->GetValue();
    settings.listen = listen_->GetValue();
}

void ConnectionPanel::refreshStatus() {
    const bool connected = session_.connected();

    connect_->SetLabel(connected ? "Disconnect" : "Connect");

    const std::string& status = session_.status();
    shown_status_ = wx(status);
    status_->SetLabel(shown_status_);
    status_->SetForegroundColour(status.find("not connected:") != std::string::npos ? kBad
                                 : connected                                        ? kGood
                                                                                    : kMuted);

    if (const auto mode = session_.textMode()) {
        text_mode_->SetLabel(*mode == TextMode::UnicodeDevice ? "Text mode: unicode device"
                                                              : "Text mode: codepage device");
        text_mode_->SetForegroundColour(kGood);
    } else if (connected) {
        text_mode_->SetLabel("Text mode: unknown, assuming unicode device");
        text_mode_->SetForegroundColour(kMuted);
    } else {
        text_mode_->SetLabel("");
    }

    if (connected) {
        spontaneous_count_->SetLabel(wxString::Format("Spontaneous: %d record(s) in %d poll(s)",
                                                      static_cast<int>(session_.spontaneousCount()),
                                                      static_cast<int>(session_.pollCount())));
        spontaneous_count_->SetForegroundColour(session_.spontaneousCount() > 0 ? kGood : kMuted);
    } else {
        spontaneous_count_->SetLabel("");
    }
    // The session turns the listener off by itself when a poll fails, so the
    // box follows the setting rather than owning it.
    listen_->SetValue(session_.settings().listen);

    for (wxWindow* control :
         {static_cast<wxWindow*>(transport_), static_cast<wxWindow*>(device_), static_cast<wxWindow*>(user_),
          static_cast<wxWindow*>(prog_id_), static_cast<wxWindow*>(timeout_), static_cast<wxWindow*>(spontaneous_),
          static_cast<wxWindow*>(exclusive_), static_cast<wxWindow*>(probe_text_mode_),
          static_cast<wxWindow*>(use_send_one_)}) {
        control->Enable(!connected);
    }
    // Not in that list: the listener can be turned on and off while connected,
    // which is the point of keeping it separate from the open flag.
    listen_->Enable(true);

    Layout();
}

void ConnectionPanel::onConnectionChanged(bool) { refreshStatus(); }

void ConnectionPanel::onTick(double) {
    // An open that fails leaves connected() false, so the frame never reports a
    // change and the panel would keep showing "connecting..." over an error
    // that only reached the status bar. Poll, but only redraw on a change.
    if (shown_status_ != wx(session_.status())) {
        refreshStatus();
        return;
    }

    // The poll counter moves several times a second; redraw only when it does,
    // and only the label that changed.
    if (shown_records_ != session_.spontaneousCount() || shown_polls_ != session_.pollCount()) {
        shown_records_ = session_.spontaneousCount();
        shown_polls_ = session_.pollCount();
        refreshStatus();
    }
}

}  // namespace gxdemo::panels
