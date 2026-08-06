// SPDX-License-Identifier: MIT
#include <wx/textctrl.h>

#include "panels/panels.hpp"

namespace gxdemo::panels {
namespace {

const wxColour kSent(40, 90, 170);
const wxColour kReceived(20, 120, 60);

}  // namespace

LogView::LogView(wxWindow* parent, Session& session) : wxPanel(parent, wxID_ANY), session_(session) {
    auto* root = new wxBoxSizer(wxVERTICAL);

    auto* bar = new wxBoxSizer(wxHORIZONTAL);
    bar->Add(new wxStaticText(this, wxID_ANY, "Exchange log"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);

    follow_ = new wxCheckBox(this, wxID_ANY, "follow");
    follow_->SetValue(true);
    bar->Add(follow_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

    details_ = new wxCheckBox(this, wxID_ANY, "details");
    details_->SetValue(true);
    bar->Add(details_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

    auto* clear = new wxButton(this, wxID_ANY, "Clear", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
    bar->Add(clear, 0, wxALIGN_CENTER_VERTICAL);

    root->Add(bar, 0, wxEXPAND | wxALL, 4);

    // wxTE_RICH2 is what allows per-line colour; without it every line would
    // come out in the default style and direction would be harder to scan.
    text_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                           wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2 | wxTE_DONTWRAP);
    text_->SetFont(wxFont(wxFontInfo(9).Family(wxFONTFAMILY_TELETYPE)));
    root->Add(text_, 1, wxEXPAND | wxALL, 4);

    SetSizer(root);

    clear->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        session_.clearLog();
        text_->Clear();
        shown_ = 0;
    });
}

void LogView::refresh() {
    Session::LogSlice slice = session_.logSince(shown_);
    if (slice.entries.empty()) {
        shown_ = slice.cursor;
        return;
    }
    shown_ = slice.cursor;

    const bool details = details_->GetValue();

    // Freezing keeps the control from repainting once per appended line, which
    // is visible as flicker when a burst of records arrives at once.
    text_->Freeze();

    for (const Session::LogEntry& entry : slice.entries) {
        wxColour colour = kMuted;
        wxString marker = " * ";
        switch (entry.kind) {
            case Session::LogEntry::Kind::Sent:
                colour = kSent;
                marker = "-> ";
                break;
            case Session::LogEntry::Kind::Received:
                colour = kReceived;
                marker = "<- ";
                break;
            case Session::LogEntry::Kind::Error:
                colour = kBad;
                marker = " ! ";
                break;
            case Session::LogEntry::Kind::Note: break;
        }

        text_->SetDefaultStyle(wxTextAttr(colour));

        wxString line = clockText(entry.at) + "  " + marker + wx(entry.text);
        if (entry.elapsed.count() > 0) {
            line += wxString::Format("   (%lld ms)", static_cast<long long>(entry.elapsed.count()));
        }
        text_->AppendText(line + "\n");

        if (details && !entry.detail.empty()) {
            text_->SetDefaultStyle(wxTextAttr(kMuted));
            text_->AppendText("                " + wx(entry.detail) + "\n");
        }
    }

    text_->Thaw();

    if (follow_->GetValue()) scrollToEnd();
}

void LogView::scrollToEnd() {
    // Scroll to the *start* of the last line, not to its end.
    //
    // ShowPosition brings a character into view horizontally as well, and with
    // wxTE_DONTWRAP a log line is easily wider than the control -- a raw
    // telegram, or a server error with a file path in it. Asking for the very
    // last character therefore scrolls the view off to the right, where every
    // other line has already ended: the log looks empty and has to be scrolled
    // back by hand. That is the jump; the vertical position was never wrong.
    const long last = text_->GetLastPosition();
    long column = 0;
    long line = 0;
    if (text_->PositionToXY(last, &column, &line)) {
        text_->ShowPosition(text_->XYToPosition(0, line));
    } else {
        text_->ShowPosition(last);
    }
}

}  // namespace gxdemo::panels
