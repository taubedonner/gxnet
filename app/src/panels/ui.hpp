// SPDX-License-Identifier: MIT
#pragma once

#include <wx/wx.h>

#include "format.hpp"

/// The few pieces of presentation every panel needs, in one place.
///
/// These were copied into each panel as it was written; six copies of the same
/// four colours is six chances for one of them to drift.
namespace gxdemo::panels {

/// Secondary text: explanations, units, values not yet read.
inline const wxColour kMuted(110, 110, 115);
/// Confirmed by the device.
inline const wxColour kGood(20, 130, 60);
/// Went through, but not the way it was meant to.
inline const wxColour kWarn(190, 110, 20);
/// Did not happen.
inline const wxColour kBad(190, 50, 50);

/// An explanatory line under a control.
inline wxStaticText* hint(wxWindow* parent, const wxString& text) {
    auto* label = new wxStaticText(parent, wxID_ANY, text);
    label->SetForegroundColour(kMuted);
    return label;
}

/// Puts the reference's account of a subfunction on a control as a tooltip.
///
/// Does nothing when the table of meanings was not generated, which is the
/// normal state of a checkout without the vendor reference beside it.
inline void explainToken(wxWindow* control, Token token) {
    const wxString text = tokenMeaningText(token);
    if (!text.empty()) control->SetToolTip(text);
}

/// A label carrying whatever the device last said.
///
/// It must not resize itself. A wxStaticText sizes to its text the moment
/// SetLabel is called, before the sizer runs, and an error message is several
/// times the width of "not read yet" -- so a plain label first covers the
/// controls beside it and then, at the next Layout(), drags them across the
/// row. Ellipsised instead, with the whole text on the tooltip.
///
/// Add it with wxEXPAND, or it keeps the width of its initial text.
inline wxStaticText* valueLabel(wxWindow* parent, const wxString& initial = "not read yet") {
    auto* label = new wxStaticText(parent, wxID_ANY, initial, wxDefaultPosition, wxDefaultSize,
                                   wxST_NO_AUTORESIZE | wxST_ELLIPSIZE_END);
    label->SetForegroundColour(kMuted);
    return label;
}

/// Sets the text of a valueLabel, keeping the full text reachable.
inline void show(wxStaticText* label, const wxString& text, const wxColour& colour) {
    label->SetLabel(text);
    label->SetForegroundColour(colour);
    if (text.empty()) {
        label->UnsetToolTip();
    } else {
        label->SetToolTip(text);
    }
}

}  // namespace gxdemo::panels
