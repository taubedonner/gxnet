// SPDX-License-Identifier: MIT
#include "panels/panels.hpp"

namespace gxdemo::panels {
namespace {

constexpr Token kPackageData = knownToken("PSV_DATA").token;            // PV04
constexpr Token kPlu = knownToken("GGL_PLUNR").token;                   // GL19
constexpr Token kNumerator = knownToken("GGL_EINZEL_NUMERATOR").token;  // GL16
constexpr Token kCode2 = knownToken("GGT_CODE2").token;                 // GT52
constexpr Token kNetWeight = knownToken("PSD_GEW_NETTO_EINZEL").token;  // PD00
constexpr Token kPrice = knownToken("PSD_PRS_VKPREIS").token;           // PD10
constexpr Token kErrorFlags = knownToken("PSL_PCK_ERR_FLAGS").token;    // PL13

wxString cell(const std::optional<Value>& value) { return value ? wx(encodeValue(*value)) : wxString("-"); }

}  // namespace

BufferPanel::BufferPanel(wxWindow* parent, Session& session) : Panel(parent, session) {
    auto* root = new wxBoxSizer(wxVERTICAL);

    auto* note = new wxStaticText(this, wxID_ANY,
                                  "The memory-card package buffer, polled with MDW_GET_BUFF (MW06).\n"
                                  "A poll, not a subscription: nothing is pushed.");
    note->SetForegroundColour(kMuted);
    root->Add(note, 0, wxALL, 6);

    // The reference's own words for MDW_GET_BUFF: "Transfer of package data and
    // implicit deletion of this transferred data to the memory card". A read
    // here is a withdrawal, not an observation.
    auto* danger = new wxStaticText(this, wxID_ANY,
                                    "Reading the buffer deletes what it returns. Where another client polls "
                                    "the same buffer, every\n"
                                    "record collected here is one it will never see, and the codes in it "
                                    "cannot be recovered.\n"
                                    "Use this on a stopped line, or against the in-memory device.");
    danger->SetForegroundColour(kBad);
    root->Add(danger, 0, wxLEFT | wxRIGHT | wxBOTTOM, 6);

    auto* controls = new wxBoxSizer(wxHORIZONTAL);

    auto* poll_once = new wxButton(this, wxID_ANY, "Poll once");
    controls->Add(poll_once, 0, wxRIGHT, 12);
    needs_connection_.push_back(poll_once);
    poll_once->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { poll(); });

    auto_poll_ = new wxCheckBox(this, wxID_ANY, "Poll automatically every");
    controls->Add(auto_poll_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);

    interval_ =
        new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(70, -1), wxSP_ARROW_KEYS, 1, 120, 5);
    controls->Add(interval_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    controls->Add(new wxStaticText(this, wxID_ANY, "s"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 16);

    controls->Add(new wxStaticText(this, wxID_ANY, "buffer size, bytes"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    buffer_size_ = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(90, -1), wxSP_ARROW_KEYS, 0,
                                  32767, 3000);
    buffer_size_->SetToolTip(
        "The word payload of MDW_GET_BUFF: the buffer size requested, documented as 0 to 2000. "
        "The default is 3000 because that is what the vendor's own software sends, outside the documented range.");
    controls->Add(buffer_size_, 0, wxALIGN_CENTER_VERTICAL);

    root->Add(controls, 0, wxALL, 6);

    status_ = new wxStaticText(this, wxID_ANY, "nothing received yet");
    status_->SetForegroundColour(kMuted);
    root->Add(status_, 0, wxLEFT | wxBOTTOM, 8);

    packages_ = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
    packages_->AppendColumn("PLU (GL19)", wxLIST_FORMAT_LEFT, 90);
    packages_->AppendColumn("Counter (GL16)", wxLIST_FORMAT_LEFT, 100);
    packages_->AppendColumn("Net weight (PD00)", wxLIST_FORMAT_LEFT, 140);
    packages_->AppendColumn("Price (PD10)", wxLIST_FORMAT_LEFT, 120);
    packages_->AppendColumn("Errors (PL13)", wxLIST_FORMAT_LEFT, 90);
    // The GS1 element string that was printed: (01) GTIN, (21) serial, GS,
    // (93) crypto tail. Checking it against the codes uploaded for the current
    // PLU is the cheapest guard against a stale buffer.
    packages_->AppendColumn("Applied code (GT52)", wxLIST_FORMAT_LEFT, 320);
    packages_->AppendColumn("Errors, decoded", wxLIST_FORMAT_LEFT, 320);
    root->Add(packages_, 1, wxEXPAND | wxALL, 6);

    auto* raw_label = new wxStaticText(this, wxID_ANY, "Raw reply");
    raw_label->SetForegroundColour(kMuted);
    root->Add(raw_label, 0, wxLEFT, 8);

    raw_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(-1, 90),
                          wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
    raw_->SetFont(wxFont(wxFontInfo(9).Family(wxFONTFAMILY_TELETYPE)));
    root->Add(raw_, 0, wxEXPAND | wxALL, 6);

    finishLayout(root);
    onConnectionChanged(false);
}

void BufferPanel::poll() {
    if (pending_) return;

    pending_ = true;
    status_->SetLabel("polling...");
    status_->SetForegroundColour(kMuted);

    session_.send(link::bufferPoll(static_cast<std::int16_t>(buffer_size_->GetValue())),
                  /*expect_reply=*/true, [this](link::LinkResult<link::Exchange> result) {
                      pending_ = false;

                      if (!result) {
                          status_->SetLabel(wx(result.error.str()));
                          status_->SetForegroundColour(kBad);
                          return;
                      }
                      if (result->status == link::Status::Timeout) {
                          status_->SetLabel("no answer within the transport timeout");
                          status_->SetForegroundColour(kBad);
                          return;
                      }

                      raw_->Clear();
                      for (const std::string& line : result->received) {
                          raw_->AppendText(wx(line) + "\n");
                      }

                      if (result->reply_error) {
                          status_->SetLabel("reply did not parse: " + wx(result->reply_error->message));
                          status_->SetForegroundColour(kBad);
                          return;
                      }
                      if (result->reply) showPackages(*result->reply);
                  });
}

void BufferPanel::showPackages(const Telegram& telegram) {
    packages_->DeleteAllItems();

    // Through link::valueOf, not node.value. A buffer answer arrives as one
    // interleaved line, which `parseOneLine` turns into a node tree carrying the
    // token layout plus one record holding the values; the nodes themselves are
    // empty.
    long row = 0;
    forEachNode(telegram.header.nodes, [&](const Node& node) {
        if (node.token != kPackageData) return;

        const auto field = [&](Token token) { return link::valueOf(telegram, node, token); };
        packages_->InsertItem(row, cell(field(kPlu)));
        packages_->SetItem(row, 1, cell(field(kNumerator)));
        packages_->SetItem(row, 2, cell(field(kNetWeight)));
        packages_->SetItem(row, 3, cell(field(kPrice)));
        packages_->SetItem(row, 4, cell(field(kErrorFlags)));
        packages_->SetItem(row, 5, cell(field(kCode2)));

        // A bit field. Decoded into its own column rather than a tooltip,
        // because bit 24 -- "no unique data available" -- is the reason a
        // package leaves the line without a marking code.
        if (const auto flags = field(kErrorFlags)) {
            packages_->SetItem(row, 6, wx(link::annotateValue(kErrorFlags, *flags)));
        }
        ++row;
    });

    if (row == 0) {
        // Not necessarily wrong: an empty buffer answers with something else.
        status_->SetLabel("no PSV_DATA blocks in the reply: see the raw text");
        status_->SetForegroundColour(kMuted);
    } else {
        status_->SetLabel(wxString::Format("%ld package(s)", row));
        status_->SetForegroundColour(wxNullColour);
    }
}

void BufferPanel::onTick(double seconds) {
    if (!auto_poll_->GetValue() || !session_.connected()) {
        since_last_s_ = 0.0;
        return;
    }
    since_last_s_ += seconds;
    if (since_last_s_ >= interval_->GetValue()) {
        since_last_s_ = 0.0;
        poll();
    }
}

void BufferPanel::onConnectionChanged(bool connected) {
    for (wxWindow* control : needs_connection_) control->Enable(connected);
}

}  // namespace gxdemo::panels
