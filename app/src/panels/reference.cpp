// SPDX-License-Identifier: MIT
#include "panels/panels.hpp"

namespace gxdemo::panels {
namespace {

/// Upper case, so a query matches the way the reference spells things.
wxString folded(const wxString& text) { return text.Upper(); }

/// The detail pane is where the whole return-code list is worth reading, so
/// nothing is left out of it.
constexpr std::size_t kNoValueLimit = static_cast<std::size_t>(-1);

}  // namespace

ReferencePanel::ReferencePanel(wxWindow* parent, Session& session) : Panel(parent, session) {
    auto* root = new wxBoxSizer(wxVERTICAL);

    root->Add(hint(this,
                   "Every subfunction the bundled table knows, with the release that "
                   "introduced it. This tab needs no connection."),
              0, wxALL, 6);

    auto* query_row = new wxBoxSizer(wxHORIZONTAL);
    query_row->Add(new wxStaticText(this, wxID_ANY, "Find"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

    filter_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(240, -1));
    filter_->SetHint("GW7D, UNIQUE, WZV_SDD");
    query_row->Add(filter_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);

    in_meanings_ = new wxCheckBox(this, wxID_ANY, "search the descriptions too");
    query_row->Add(in_meanings_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);

    count_ = new wxStaticText(this, wxID_ANY, "");
    count_->SetForegroundColour(kMuted);
    query_row->Add(count_, 1, wxALIGN_CENTER_VERTICAL);

    root->Add(query_row, 0, wxEXPAND | wxALL, 6);

    auto* columns = new wxBoxSizer(wxHORIZONTAL);

    list_ = new wxListBox(this, wxID_ANY, wxDefaultPosition, wxSize(320, 260));
    list_->SetFont(wxFont(wxFontInfo(9).Family(wxFONTFAMILY_TELETYPE)));
    columns->Add(list_, 1, wxEXPAND | wxRIGHT, 8);

    detail_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(-1, 260),
                             wxTE_MULTILINE | wxTE_READONLY | wxTE_BESTWRAP);
    columns->Add(detail_, 1, wxEXPAND);

    root->Add(columns, 1, wxEXPAND | wxALL, 6);

#ifndef GXNET_HAS_TOKEN_DOCS
    // Without the generated table there are still names and versions, which is
    // most of what the tab is for. Say so rather than leaving the pane blank.
    root->Add(hint(this,
                   "Only names and versions are compiled in. The table of meanings is "
                   "generated from the vendor reference by tools/gen_docs.py."),
              0, wxALL, 6);
    in_meanings_->Disable();
#endif

    finishLayout(root);

    filter_->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { refill(); });
    in_meanings_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { refill(); });
    list_->Bind(wxEVT_LISTBOX, [this](wxCommandEvent&) { showSelected(); });

    refill();
}

void ReferencePanel::refill() {
    const wxString query = folded(filter_->GetValue().Trim(true).Trim(false));
    const bool deep = in_meanings_->IsChecked();

    shown_.clear();
    wxArrayString rows;

    for (const TokenInfo& entry : Registry::builtin().entries()) {
        const wxString token = wx(entry.token.str());
        const wxString name = wx(entry.name);

        bool matches = query.empty() || folded(token).Contains(query) || folded(name).Contains(query);
        if (!matches && deep) {
            if (const auto meaning = tokenMeaning(entry.token)) {
                matches = folded(wx(*meaning)).Contains(query);
            }
        }
        if (!matches) continue;

        shown_.push_back(entry.token);
        rows.Add(token + "  " + name);
    }

    list_->Set(rows);
    count_->SetLabel(
        wxString::Format("%zu of %zu", shown_.size(), static_cast<std::size_t>(Registry::builtin().size())));
    detail_->Clear();
    Layout();
}

void ReferencePanel::showSelected() {
    const int index = list_->GetSelection();
    if (index == wxNOT_FOUND || static_cast<std::size_t>(index) >= shown_.size()) return;

    const Token token = shown_[static_cast<std::size_t>(index)];
    wxString text = describeToken(token) + "\n";

    const wxString meaning = tokenMeaningText(token, kNoValueLimit);
    text += meaning.empty() ? "\nThe reference gives no more than the name." : "\n" + meaning;

    detail_->SetValue(text);
}

}  // namespace gxdemo::panels
