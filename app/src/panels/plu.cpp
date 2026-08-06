// SPDX-License-Identifier: MIT
#include "panels/panels.hpp"

namespace gxdemo::panels {
namespace {

constexpr Token kPluNumber = knownToken("GGL_PLUNR").token;  // GL19

/// GGL_PLUNR: 0 / 999 999 999.
constexpr int kMaxPlu = 999999999;

}  // namespace

PluPanel::PluPanel(wxWindow* parent, Session& session) : Panel(parent, session) {
    current_.token = kPluNumber;

    auto* root = new wxBoxSizer(wxVERTICAL);

    // The distinction that gives this panel its reason to exist. Writing GL19
    // and performing a PLU change are different operations, and the first one
    // looks like it worked.
    root->Add(hint(this,
                   "The full PLU change: XCV_DBTAB_DATASET (XV00), what the PLU key "
                   "on the terminal does. It saves the\n"
                   "article total, imports the PLU data and everything belonging to "
                   "it.\n\n"
                   "Writing GGL_PLUNR on its own is a different thing: it sets a "
                   "number in the current working record\n"
                   "and imports nothing."),
              0, wxALL, 6);

    // --- current ----------------------------------------------------------

    auto* current_box = new wxStaticBoxSizer(wxVERTICAL, this, "Current PLU: GGL_PLUNR (GL19)");
    wxWindow* cbox = current_box->GetStaticBox();

    auto* current_row = new wxBoxSizer(wxHORIZONTAL);
    auto* read = new wxButton(cbox, wxID_ANY, "Read", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    current_row->Add(read, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);
    needs_connection_.push_back(read);
    read->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { readCurrent(); });

    current_value_ = new wxStaticText(cbox, wxID_ANY, "not read yet", wxDefaultPosition, wxDefaultSize,
                                      wxST_NO_AUTORESIZE | wxST_ELLIPSIZE_END);
    current_value_->SetForegroundColour(kMuted);
    current_row->Add(current_value_, 1, wxALIGN_CENTER_VERTICAL);

    current_box->Add(current_row, 0, wxEXPAND | wxALL, 6);
    root->Add(current_box, 0, wxEXPAND | wxALL, 6);

    // --- the change -------------------------------------------------------

    auto* change_box = new wxStaticBoxSizer(wxVERTICAL, this, "Change to: XCV_DBTAB_DATASET (XV00)");
    wxWindow* bbox = change_box->GetStaticBox();

    auto* number_row = new wxBoxSizer(wxHORIZONTAL);
    number_row->Add(new wxStaticText(bbox, wxID_ANY, "PLU number"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

    plu_ = new wxSpinCtrl(bbox, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(140, -1), wxSP_ARROW_KEYS, 0,
                          kMaxPlu, 0);
    number_row->Add(plu_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 20);

    // GGL_KDNR is the second database key: a value set is held per (PLU,
    // customer) pair, and leaving it out addresses the PLU alone.
    with_customer_ = new wxCheckBox(bbox, wxID_ANY, "with customer (GL1A)");
    number_row->Add(with_customer_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

    customer_ = new wxSpinCtrl(bbox, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(140, -1), wxSP_ARROW_KEYS, 0,
                               kMaxPlu, 0);
    customer_->Enable(false);
    number_row->Add(customer_, 0, wxALIGN_CENTER_VERTICAL);

    change_box->Add(number_row, 0, wxALL, 6);

    preview_ = new wxStaticText(bbox, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                wxST_NO_AUTORESIZE | wxST_ELLIPSIZE_END);
    preview_->SetForegroundColour(kMuted);
    preview_->SetFont(wxFont(wxFontInfo(9).Family(wxFONTFAMILY_TELETYPE)));
    change_box->Add(preview_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

    change_ = new wxButton(bbox, wxID_ANY, "Change PLU...");
    change_box->Add(change_, 0, wxALL, 6);
    needs_connection_.push_back(change_);
    change_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { change(); });

    result_ = new wxStaticText(bbox, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                               wxST_NO_AUTORESIZE | wxST_ELLIPSIZE_END);
    result_->SetForegroundColour(kMuted);
    change_box->Add(result_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

    root->Add(change_box, 0, wxEXPAND | wxALL, 6);

    // --- what to know before pressing it ----------------------------------

    auto* care_box = new wxStaticBoxSizer(wxVERTICAL, this, "On a running line");
    wxWindow* kbox = care_box->GetStaticBox();

    // Not implemented here on purpose: XCW_PCK_SYNC changes how the line
    // releases packages, and turning that on without turning it off again is
    // worse than doing the change by hand. Saying so beats a button that looks
    // harmless.
    auto* care = new wxStaticText(kbox, wxID_ANY,
                                  "A PLU change implicitly releases a package held at the scale, when "
                                  "the softkey \"Message at trigger 2\"\n"
                                  "is active. XCW_PCK_SYNC (XW12) with parameter 1 prevents that, 0 or 2 "
                                  "releases again. Not sent from\n"
                                  "here: a lock left set by a crashed program stops the line.\n\n"
                                  "Read the acknowledgement rather than assume. 2650 means the database "
                                  "has no such record, so nothing\n"
                                  "changed and the machine is still on the old article.");
    care->SetForegroundColour(kWarn);
    care_box->Add(care, 0, wxALL, 6);

    root->Add(care_box, 0, wxEXPAND | wxALL, 6);

    finishLayout(root);

    plu_->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent& event) {
        refreshPreviewLine();
        event.Skip();
    });
    plu_->Bind(wxEVT_TEXT, [this](wxCommandEvent& event) {
        refreshPreviewLine();
        event.Skip();
    });
    customer_->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent& event) {
        refreshPreviewLine();
        event.Skip();
    });
    with_customer_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent& event) {
        customer_->Enable(with_customer_->GetValue());
        refreshPreviewLine();
        event.Skip();
    });

    refreshPreviewLine();
    onConnectionChanged(false);
}

Telegram PluPanel::buildChange() const {
    std::optional<std::int32_t> customer;
    if (with_customer_->GetValue()) {
        customer = static_cast<std::int32_t>(customer_->GetValue());
    }
    return link::pluChange(static_cast<std::int32_t>(plu_->GetValue()), customer);
}

void PluPanel::refreshPreviewLine() {
    auto lines = encodeLines(buildChange());
    if (!lines || lines->empty()) {
        preview_->SetLabel(wx(lines.error.message));
        return;
    }
    wxString text = wx(lines->front());
    for (std::size_t i = 1; i < lines->size(); ++i) {
        text += "   +   " + wx((*lines)[i]);
    }
    preview_->SetLabel(text);
}

void PluPanel::readCurrent() {
    current_value_->SetLabel("reading...");
    current_value_->SetForegroundColour(kMuted);

    session_.read(kPluNumber, [this](link::LinkResult<Value> result) {
        if (!result) {
            current_value_->SetLabel(wx(result.error.str()));
            current_value_->SetForegroundColour(kBad);
            current_value_->SetToolTip(wx(result.error.str()));
            if (requested_) {
                result_->SetLabel(
                    "could not read the PLU back, so nothing is "
                    "confirmed");
                result_->SetForegroundColour(kBad);
                requested_.reset();
            }
            return;
        }
        current_.value = *result;
        current_value_->SetLabel(describeValue(current_.value));
        current_value_->SetForegroundColour(wxNullColour);
        current_value_->UnsetToolTip();

        const auto* raw = std::get_if<std::int32_t>(&current_.value);

        // The verdict on a change, and the only one worth having: what the
        // machine says it is on now.
        if (requested_ && raw != nullptr) {
            if (*raw == *requested_) {
                result_->SetLabel(wxString::Format("confirmed: the machine is on PLU %d", *raw));
                result_->SetForegroundColour(kGood);
            } else {
                result_->SetLabel(
                    wxString::Format("did not take: asked for %d, the machine is on %d", *requested_, *raw));
                result_->SetForegroundColour(kBad);
            }
            requested_.reset();
        }

        if (raw != nullptr) {
            // Seed the editor, so a change that is one digit away does not have
            // to be retyped, and so the number about to be sent is visibly not
            // the one already loaded.
            if (*raw >= 0 && *raw <= kMaxPlu) plu_->SetValue(*raw);
            refreshPreviewLine();
        }
    });
}

void PluPanel::change() {
    const int plu = plu_->GetValue();

    wxString warning = wxString::Format(
        "Change the machine to PLU %d?\n\n"
        "This is the full change, not a write of the PLU number: the article "
        "total is saved and the whole data record is imported.",
        plu);
    if (const auto* raw = std::get_if<std::int32_t>(&current_.value)) {
        if (*raw == plu) {
            warning += wxString::Format("\n\nThe machine already reports PLU %d.", *raw);
        }
    } else {
        warning +=
            "\n\nThe current PLU has not been read, so there is nothing "
            "to compare against.";
    }

    if (wxMessageBox(warning, "Change PLU", wxYES_NO | wxICON_WARNING, this) != wxYES) {
        return;
    }

    requested_ = static_cast<std::int32_t>(plu);
    result_->SetLabel("changing...");
    result_->SetForegroundColour(kMuted);

    // No reply is waited for, and that is not laziness.
    //
    // The device does acknowledge: the commlog shows LGW_QUIT_OK carrying
    // 0xA600, the class code of XV00, 164 ms after the send. But the server
    // consumes it. A positive acknowledgement simply makes Send return zero and
    // leaves the receive queue empty, so waiting for one costs the full timeout
    // and then reports "timeout" over a change that worked.
    //
    // A negative one is not lost: the server turns LGV_QUIT into a failed Send
    // carrying the device's own words, which arrives below as an error.
    //
    // What settles it either way is the read-back.
    session_.send(buildChange(), /*expect_reply=*/false, [this](link::LinkResult<link::Exchange> result) {
        if (!result) {
            result_->SetLabel(wx(result.error.str()));
            result_->SetForegroundColour(kBad);
            result_->SetToolTip(wx(result.error.str()));
            // Still worth reading back: the machine's state is
            // the answer, not the error text.
            readCurrent();
            return;
        }
        result_->SetLabel("sent; reading back");
        result_->SetForegroundColour(kMuted);
        readCurrent();
    });
}

void PluPanel::onConnectionChanged(bool connected) {
    for (wxWindow* control : needs_connection_) control->Enable(connected);
}

}  // namespace gxdemo::panels
