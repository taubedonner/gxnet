// SPDX-License-Identifier: MIT
#include "panels/panels.hpp"

namespace gxdemo::panels {
namespace {

constexpr Token kIntake = knownToken("GGW_UNIQUE_DATEN").token;          // GW7D
constexpr Token kClear = knownToken("XCX_DELETE_UNIQUE_DATA").token;     // XX13
constexpr Token kReady = knownToken("SRW_UNIQUE_PCK_DATA_READY").token;  // SW9B

}  // namespace

UniquePanel::UniquePanel(wxWindow* parent, Session& session) : Panel(parent, session) {
    intake_.token = kIntake;
    ready_.token = kReady;

    auto* root = new wxBoxSizer(wxVERTICAL);

    root->Add(hint(this,
                   "Unique-code data: the mandatory-marking codes the line "
                   "applies to labels."),
              0, wxALL, 6);

    auto* refresh_button = new wxButton(this, wxID_ANY, "Read state");
    root->Add(refresh_button, 0, wxLEFT | wxBOTTOM, 6);
    needs_connection_.push_back(refresh_button);
    refresh_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { refresh(); });

    // --- intake -----------------------------------------------------------

    auto* intake_box = new wxStaticBoxSizer(wxVERTICAL, this, "Intake: GGW_UNIQUE_DATEN (GW7D)");
    wxWindow* ibox = intake_box->GetStaticBox();
    explainToken(ibox, kIntake);

    intake_value_ = new wxStaticText(ibox, wxID_ANY, "not read yet");
    intake_value_->SetForegroundColour(kMuted);
    intake_box->Add(intake_value_, 0, wxALL, 6);

    intake_note_ = new wxStaticText(ibox, wxID_ANY, "");
    intake_note_->SetForegroundColour(kMuted);
    intake_box->Add(intake_note_, 0, wxLEFT | wxBOTTOM, 6);

    auto* intake_buttons = new wxBoxSizer(wxHORIZONTAL);
    auto* disable = new wxButton(ibox, wxID_ANY, "Disable (0)");
    auto* enable = new wxButton(ibox, wxID_ANY, "Enable (1)");
    intake_buttons->Add(disable, 0, wxRIGHT, 8);
    intake_buttons->Add(enable, 0);
    intake_box->Add(intake_buttons, 0, wxALL, 6);
    needs_connection_.push_back(disable);
    needs_connection_.push_back(enable);

    disable->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { setIntake(0); });
    enable->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { setIntake(1); });

    root->Add(intake_box, 0, wxEXPAND | wxALL, 6);

    // --- buffer -----------------------------------------------------------

    auto* clear_box = new wxStaticBoxSizer(wxVERTICAL, this, "Buffer: XCX_DELETE_UNIQUE_DATA (XX13)");
    wxWindow* cbox = clear_box->GetStaticBox();
    explainToken(cbox, kClear);

    clear_box->Add(hint(cbox,
                        "Clears the buffered codes. The reference allows it only while intake "
                        "is disabled, and with intake enabled\n"
                        "the machine swallows a dropped file immediately, on top of the old buffer."),
                   0, wxALL, 6);

    auto* clear_button = new wxButton(cbox, wxID_ANY, "Clear buffer...");
    clear_box->Add(clear_button, 0, wxALL, 6);
    needs_connection_.push_back(clear_button);
    clear_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { clearBuffer(); });

    root->Add(clear_box, 0, wxEXPAND | wxALL, 6);

    // --- readiness --------------------------------------------------------

    auto* ready_box = new wxStaticBoxSizer(wxVERTICAL, this, "Readiness: SRW_UNIQUE_PCK_DATA_READY (SW9B)");
    wxWindow* rbox = ready_box->GetStaticBox();
    explainToken(rbox, kReady);

    // A flag, not a stock level: the reference names both values and neither
    // counts anything. So the raw value is still shown, because a value outside
    // 0 and 1 would mean the reading is wrong.
    ready_box->Add(hint(rbox,
                        "Needs firmware 16.40. A readiness flag: 0 the unique data cannot be "
                        "used, 1 it can.\nIt does not report how many codes are left."),
                   0, wxALL, 6);

    ready_value_ = new wxStaticText(rbox, wxID_ANY, "not read yet");
    ready_value_->SetForegroundColour(kMuted);
    ready_box->Add(ready_value_, 0, wxLEFT | wxBOTTOM, 6);

    ready_note_ = new wxStaticText(rbox, wxID_ANY, "");
    ready_note_->SetForegroundColour(kMuted);
    ready_box->Add(ready_note_, 0, wxLEFT | wxBOTTOM, 6);

    auto* probe = new wxButton(rbox, wxID_ANY, "Probe");
    ready_box->Add(probe, 0, wxALL, 6);
    needs_connection_.push_back(probe);
    probe->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { probeReady(); });

    root->Add(ready_box, 0, wxEXPAND | wxALL, 6);

    action_ = new wxStaticText(this, wxID_ANY, "");
    action_->SetForegroundColour(kMuted);
    root->Add(action_, 0, wxALL, 6);

    finishLayout(root);
    onConnectionChanged(false);
}

void UniquePanel::refresh() {
    intake_value_->SetLabel("reading...");
    intake_value_->SetForegroundColour(kMuted);

    session_.read(kIntake, [this](link::LinkResult<Value> result) {
        if (!result) {
            intake_value_->SetLabel(wx(result.error.str()));
            intake_value_->SetForegroundColour(kBad);
            intake_note_->SetLabel("");
            Layout();
            return;
        }
        intake_.value = *result;

        const auto* state = std::get_if<std::int16_t>(&intake_.value);
        if (state == nullptr) {
            intake_value_->SetLabel(describeValue(intake_.value));
            intake_value_->SetForegroundColour(kWarn);
        } else if (*state == 0) {
            intake_value_->SetLabel("0: intake disabled");
            intake_value_->SetForegroundColour(kWarn);
            intake_note_->SetLabel("A file dropped on FTP now waits instead of being swallowed.");
        } else if (*state == 1) {
            intake_value_->SetLabel("1: intake enabled");
            intake_value_->SetForegroundColour(kGood);
            intake_note_->SetLabel("");
        } else {
            // The reference gives this one a range of 0 to 1. Anything else is
            // worth showing rather than guessing at.
            intake_value_->SetLabel(wxString::Format("%d: outside the documented range", *state));
            intake_value_->SetForegroundColour(kWarn);
            intake_note_->SetLabel("");
        }
        Layout();
    });

    probeReady();
}

void UniquePanel::setIntake(std::int16_t state) {
    action_->SetLabel(state == 0 ? "disabling intake..." : "enabling intake...");

    session_.write(kIntake, Value(state), [this, state](link::LinkResult<Value> result) {
        if (result) {
            intake_.value = *result;
            action_->SetLabel(state == 0 ? "intake disabled and confirmed" : "intake enabled and confirmed");
            action_->SetForegroundColour(kGood);
            refresh();
        } else {
            action_->SetLabel(wx(result.error.str()));
            action_->SetForegroundColour(kBad);
        }
        Layout();
    });
}

void UniquePanel::clearBuffer() {
    const auto* state = std::get_if<std::int16_t>(&intake_.value);
    wxString warning = "Clear the unique-code buffer?\n\nEvery code currently loaded is lost.";
    if (state == nullptr) {
        warning += "\n\nIntake state is unknown: it has not been read.";
    } else if (*state != 0) {
        warning +=
            "\n\nIntake is still ENABLED. The reference allows the clear only while "
            "intake is disabled, and the machine may swallow a dropped file on top "
            "of the cleared buffer.";
    }

    if (wxMessageBox(warning, "Clear buffer", wxYES_NO | wxICON_WARNING, this) != wxYES) {
        return;
    }

    action_->SetLabel("clearing buffer...");
    action_->SetForegroundColour(kMuted);

    Builder builder(Family::Automatic, Access::Write);
    builder.command(kClear.str());

    session_.send(builder.build(), /*expect_reply=*/false, [this](link::LinkResult<link::Exchange> result) {
        if (result) {
            action_->SetLabel("buffer cleared");
            action_->SetForegroundColour(kGood);
            // The command answers nothing, so the state
            // afterwards is established by reading it back.
            refresh();
        } else {
            action_->SetLabel("clear failed: " + wx(result.error.str()));
            action_->SetForegroundColour(kBad);
        }
        Layout();
    });
}

void UniquePanel::probeReady() {
    // Asked before sending, because this one is polled after every write and on
    // a device below 16.40 every single call came back as an exception. Six of
    // them in one session, each printing a message that told the reader to
    // compare the release against SRT_GX_VERSION -- which is the comparison
    // being made right here.
    if (const Session::Supported supported = session_.supports(kReady); !supported.ok) {
        ready_value_->SetLabel(wx(supported.reason));
        ready_value_->SetForegroundColour(kWarn);
        ready_note_->SetLabel("Not asked. The readiness flag simply does not exist on this firmware.");
        ready_note_->SetForegroundColour(kMuted);
        Layout();
        return;
    }

    ready_value_->SetLabel("reading...");
    ready_value_->SetForegroundColour(kMuted);

    session_.read(kReady, [this](link::LinkResult<Value> result) {
        if (!result) {
            ready_value_->SetLabel(wx(result.error.str()));
            ready_value_->SetForegroundColour(kBad);
            ready_note_->SetLabel("");
            Layout();
            return;
        }
        ready_.value = *result;
        ready_value_->SetLabel("raw value: " + describeValue(ready_.value));
        ready_value_->SetForegroundColour(wxNullColour);

        if (const auto* value = std::get_if<std::int16_t>(&ready_.value)) {
            wxString note = *value != 0 ? "unique data ready" : "unique data not ready";
            if (*value > 1) {
                note += "; outside the documented 0 / 1, so this reading is not trustworthy";
                ready_note_->SetForegroundColour(kWarn);
            } else {
                ready_note_->SetForegroundColour(kMuted);
            }
            ready_note_->SetLabel(note);
        }
        Layout();
    });
}

void UniquePanel::onConnectionChanged(bool connected) {
    for (wxWindow* control : needs_connection_) control->Enable(connected);
}

}  // namespace gxdemo::panels
