// SPDX-License-Identifier: MIT
#include "panels/panels.hpp"

namespace gxdemo::panels {
namespace {

/// Starting points, including the questions that are still open.
struct Suggestion {
    const char* line;
    const char* what;
};

constexpr std::array<Suggestion, 6> kSuggestions{{
    {"A?ST8D", "software version"},
    {"A?GW7D", "unique-data intake state"},
    {"A?SW9B", "unique buffer readiness, needs firmware 16.40"},
    {"A?GL06", "date 1"},
    {"A?GT61", "plain text 1"},
    {"A?MW06|3000", "poll the package buffer, requested buffer size in bytes"},
}};

/// The subfunction a tree item stands for, so the tooltip can look it up in the
/// reference. Values are shown in the label; what they mean does not fit there.
class TokenItem final : public wxTreeItemData {
public:
    explicit TokenItem(Token token) : token(token) {}

    Token token;
};

}  // namespace

ConsolePanel::ConsolePanel(wxWindow* parent, Session& session) : Panel(parent, session) {
    auto* root = new wxBoxSizer(wxVERTICAL);

    auto* note = new wxStaticText(this, wxID_ANY,
                                  "Type a telegram as it goes on the wire. Reads (A?) carry no data; "
                                  "writes (A!) carry values after the tokens.");
    note->SetForegroundColour(kMuted);
    root->Add(note, 0, wxALL, 6);

    auto* input_row = new wxBoxSizer(wxHORIZONTAL);

    input_ = new wxTextCtrl(this, wxID_ANY, "A?ST8D", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
    input_->SetFont(wxFont(wxFontInfo(10).Family(wxFONTFAMILY_TELETYPE)));
    input_row->Add(input_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

    send_ = new wxButton(this, wxID_ANY, "Send");
    input_row->Add(send_, 0, wxRIGHT, 8);

    expect_reply_ = new wxCheckBox(this, wxID_ANY, "wait for reply");
    expect_reply_->SetValue(true);
    input_row->Add(expect_reply_, 0, wxALIGN_CENTER_VERTICAL);

    root->Add(input_row, 0, wxEXPAND | wxALL, 6);

    preview_ = new wxStaticText(this, wxID_ANY, "");
    root->Add(preview_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

    // --- suggestions ------------------------------------------------------

    auto* suggestions_box = new wxStaticBoxSizer(wxHORIZONTAL, this, "Suggestions");
    wxWindow* sbox = suggestions_box->GetStaticBox();

    for (const Suggestion& suggestion : kSuggestions) {
        auto* button = new wxButton(sbox, wxID_ANY, suggestion.line, wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
        button->SetToolTip(suggestion.what);
        suggestions_box->Add(button, 0, wxALL, 3);

        const wxString line = suggestion.line;
        button->Bind(wxEVT_BUTTON, [this, line](wxCommandEvent&) {
            input_->ChangeValue(line);
            updatePreview();
        });
    }

    root->Add(suggestions_box, 0, wxEXPAND | wxALL, 6);

    // --- history and reply ------------------------------------------------

    auto* columns = new wxBoxSizer(wxHORIZONTAL);

    auto* history_box = new wxStaticBoxSizer(wxVERTICAL, this, "History");
    history_ = new wxListBox(history_box->GetStaticBox(), wxID_ANY);
    history_box->Add(history_, 1, wxEXPAND | wxALL, 4);
    columns->Add(history_box, 1, wxEXPAND | wxRIGHT, 6);

    auto* reply_box = new wxStaticBoxSizer(wxVERTICAL, this, "Reply");
    tree_ = new wxTreeCtrl(reply_box->GetStaticBox(), wxID_ANY, wxDefaultPosition, wxDefaultSize,
                           wxTR_DEFAULT_STYLE | wxTR_HIDE_ROOT | wxTR_FULL_ROW_HIGHLIGHT);
    reply_box->Add(tree_, 2, wxEXPAND | wxALL, 4);

    raw_ = new wxTextCtrl(reply_box->GetStaticBox(), wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(-1, 70),
                          wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
    raw_->SetFont(wxFont(wxFontInfo(9).Family(wxFONTFAMILY_TELETYPE)));
    reply_box->Add(raw_, 1, wxEXPAND | wxALL, 4);

    columns->Add(reply_box, 2, wxEXPAND);
    root->Add(columns, 1, wxEXPAND | wxALL, 6);

    finishLayout(root);

    send_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { send(); });
    input_->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { send(); });
    input_->Bind(wxEVT_TEXT, [this](wxCommandEvent& event) {
        updatePreview();
        event.Skip();
    });
    tree_->Bind(wxEVT_TREE_ITEM_GETTOOLTIP, [this](wxTreeEvent& event) {
        const auto* item = dynamic_cast<TokenItem*>(tree_->GetItemData(event.GetItem()));
        if (item == nullptr) return;
        event.SetToolTip(tokenMeaningText(item->token));
    });
    history_->Bind(wxEVT_LISTBOX, [this](wxCommandEvent& event) {
        if (event.GetSelection() != wxNOT_FOUND) {
            input_->ChangeValue(history_->GetString(event.GetSelection()));
            updatePreview();
        }
    });

    updatePreview();
    onConnectionChanged(false);
}

void ConsolePanel::updatePreview() {
    const std::string line = utf8(input_->GetValue());
    if (line.empty()) {
        preview_->SetLabel("");
        return;
    }

    // What the line parses as, before anything is sent. A malformed telegram is
    // worth catching here rather than on the device.
    auto parsed = parseOneLine(line);
    if (!parsed) {
        auto header = parseHeader(line);
        if (!header) {
            preview_->SetLabel(wx(header.error.message));
            preview_->SetForegroundColour(kBad);
            Layout();
            return;
        }
        Telegram telegram;
        telegram.header = *header;
        followAccess(header->access);
        preview_->SetLabel("header with no data: " + wx(describeTelegram(telegram)));
        preview_->SetForegroundColour(kGood);
        Layout();
        return;
    }

    followAccess(parsed->header.access);

    wxString text = "parses as: " + wx(describeTelegram(*parsed));
    wxColour colour = kGood;

    // Structural problems are errors; an unknown token is only a warning,
    // because the bundled table reflects one revision and a device may be newer.
    for (const Diagnostic& diagnostic : validate(*parsed, {})) {
        text += "\n" + wx(diagnostic.str());
        if (diagnostic.severity == Severity::Error)
            colour = kBad;
        else if (colour != kBad)
            colour = kWarn;
    }

    preview_->SetLabel(text);
    preview_->SetForegroundColour(colour);
    Layout();
}

void ConsolePanel::followAccess(Access access) {
    // The server takes a write's acknowledgement itself, so waiting for one
    // buys a full timeout and nothing else; a read always answers. Follow the
    // direction, but only when it changes, so a deliberate override survives
    // further typing.
    if (shown_access_ && *shown_access_ == access) return;
    shown_access_ = access;
    expect_reply_->SetValue(access == Access::Read);
}

void ConsolePanel::send() {
    const std::string line = utf8(input_->GetValue());
    if (line.empty() || !session_.connected()) return;

    history_->Insert(wx(line), 0);
    tree_->DeleteAllItems();
    raw_->Clear();

    session_.sendRaw(line, expect_reply_->GetValue(), [this](link::LinkResult<link::Exchange> result) {
        if (!result) {
            raw_->SetValue(wx(result.error.str()));
            return;
        }
        for (const std::string& text : result->received) {
            raw_->AppendText(wx(text) + "\n");
        }
        if (result->status == link::Status::Timeout) {
            raw_->AppendText("timeout\n");
        }
        if (result->reply_error) {
            raw_->AppendText("reply did not parse: " + wx(result->reply_error->message) + "\n");
        }
        if (result->reply) fillTree(*result->reply);
    });
}

void ConsolePanel::fillTree(const Telegram& telegram) {
    tree_->DeleteAllItems();
    const wxTreeItemId root = tree_->AddRoot("telegram");

    const Record* record = telegram.records.empty() ? nullptr : &telegram.records.front();
    std::size_t field = 0;

    // A recursive lambda: blocks nest, and the value of a leaf comes either
    // from the node itself (interleaved form) or from the record (header plus
    // data form), in header order.
    const auto add = [&](auto&& self, const std::vector<Node>& nodes, const wxTreeItemId& parent) -> void {
        for (const Node& node : nodes) {
            wxString label = describeToken(node.token);

            Value value = node.value;
            if (node.token.arity() > 0) {
                if (isEmpty(value) && record != nullptr && field < record->size()) {
                    value = (*record)[field];
                }
                ++field;
                label += "  =  " + describeValue(value);
                // What the number means, for the tokens where the number alone
                // says nothing: a channel bitmap, a subfunction code, an error
                // bit field. Empty for everything else, which is most of it.
                const std::string decoded = link::annotateValue(node.token, value);
                if (!decoded.empty()) label += "   (" + wx(decoded) + ")";
            }

            const wxTreeItemId item = tree_->AppendItem(parent, label, -1, -1, new TokenItem(node.token));
            if (!node.children.empty()) self(self, node.children, item);
        }
    };
    add(add, telegram.header.nodes, root);

    tree_->ExpandAll();
}

void ConsolePanel::onConnectionChanged(bool connected) { send_->Enable(connected); }

}  // namespace gxdemo::panels
