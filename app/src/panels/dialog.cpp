// SPDX-License-Identifier: MIT
#include <wx/tokenzr.h>

#include <algorithm>

#include "panels/panels.hpp"

namespace gxdemo::panels {
namespace {

/// WZW_HDL: 0 / 65 535. wxSpinCtrl works in int and the field is a signed word
/// on the wire, so the panel offers the half that needs no reinterpretation.
constexpr int kMaxHandle = 32767;

/// WZW_SDD_ID: 0 / 65 536 by the reference; same reasoning as the handle.
constexpr int kMaxId = 32767;

/// WZT_HEADLINE and WZT_LABEL are both capped here by the reference.
constexpr std::size_t kMaxLabel = 30;

/// The dialog types this panel can build, in the order the choice offers them.
///
/// Not all nine: the rest take numeric, alphanumeric or calendar input
/// elements, whose fields (WZL_VALUE, WZW_DIGITS, GGT_ATX and the others) this
/// does not assemble. Offering them with the wrong element contents would be a
/// worse experiment than not offering them.
struct Kind {
    std::int16_t type;
    const char* label;
};

constexpr std::array<Kind, 3> kKinds{{
    {8, "confirmation (WZW_SDD_TYP 8)"},
    {7, "selection, scroll menu (WZW_SDD_TYP 7)"},
    {9, "display only (WZW_SDD_TYP 9)"},
}};

}  // namespace

DialogPanel::DialogPanel(wxWindow* parent, Session& session) : Panel(parent, session) {
    auto* root = new wxBoxSizer(wxVERTICAL);

    // Why this exists at all: the note on the Terminal tab cannot be dismissed
    // from the panel, so it is no use for anything the operator has to answer.
    // A standard dialog can be left with HOME, and says so in its result.
    root->Add(hint(this,
                   "Standard dialogs: WZV_SDD_START (WV60) with one WZV_SDD_DATA "
                   "(WV62) per element, answered by WZV_SDD_RESULT (WV63).\n"
                   "The operator can leave a dialog with HOME, and the result says "
                   "so."),
              0, wxALL, 6);

    // The device refuses these and the reason is not known, so every field the
    // reference marks optional, and the one whose token code was deduced, is a
    // control here rather than a constant in the builder.
    auto* varying = new wxStaticText(this, wxID_ANY,
                                     "The device answers an internal error and draws an empty window. Each "
                                     "control below is one thing to vary;\n"
                                     "vary one at a time, or a result settles nothing.");
    varying->SetForegroundColour(kWarn);
    root->Add(varying, 0, wxLEFT | wxRIGHT | wxBOTTOM, 6);

    // --- what kind --------------------------------------------------------

    auto* kind_row = new wxBoxSizer(wxHORIZONTAL);
    kind_row->Add(new wxStaticText(this, wxID_ANY, "Dialog"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

    kind_ = new wxChoice(this, wxID_ANY);
    for (const Kind& kind : kKinds) kind_->Append(kind.label);
    kind_->SetSelection(0);
    kind_row->Add(kind_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 20);

    kind_row->Add(new wxStaticText(this, wxID_ANY, "handle (WW60)"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    handle_ = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(80, -1), wxSP_ARROW_KEYS, 0,
                             kMaxHandle, 1);
    handle_->SetToolTip(
        "Chosen here and echoed back in the result. What pairs an answer with "
        "the question that asked it.");
    kind_row->Add(handle_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 20);

    kind_row->Add(new wxStaticText(this, wxID_ANY, "element type (WW63)"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    elem_type_ =
        new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(70, -1), wxSP_ARROW_KEYS, 0, 9, 0);
    elem_type_->SetToolTip(
        "0 takes the value the reference pairs with the dialog type. The two are not independent:\n"
        "its coding table prints the permitted combinations, and this is how to send one that is not.");
    kind_row->Add(elem_type_, 0, wxALIGN_CENTER_VERTICAL);

    root->Add(kind_row, 0, wxALL, 6);

    // --- which fields go out ----------------------------------------------

    auto* fields_box = new wxStaticBoxSizer(wxVERTICAL, this, "Fields");
    wxWindow* fbox = fields_box->GetStaticBox();

    elem_count_ = new wxCheckBox(fbox, wxID_ANY, "WZW_SDD_ELEM_COUNT (WW62, deduced)");
    elem_count_->SetValue(true);
    elem_count_->SetToolTip(
        "The reference names the field but its coding table leaves 0x62 "
        "unnamed. WW62 is the slot the field order\n"
        "points at, and nothing else nearby is a counting variable. Turn it "
        "off to see whether the device prefers it absent.");
    fields_box->Add(elem_count_, 0, wxALL, 4);

    with_active_ = new wxCheckBox(fbox, wxID_ANY, "WZW_SDD_ELEM_ACTIVE (WW69)");
    with_active_->SetToolTip("Optional in the reference. A position among the elements, one based, not an id.");
    fields_box->Add(with_active_, 0, wxALL, 4);

    with_headline_ = new wxCheckBox(fbox, wxID_ANY, "WZT_HEADLINE (WT60)");
    with_headline_->SetValue(true);
    with_headline_->SetToolTip("Not marked optional, which is what makes leaving it out worth trying.");
    fields_box->Add(with_headline_, 0, wxALL, 4);

    close_blocks_ = new wxCheckBox(fbox, wxID_ANY, "close the blocks with LGX_CLOSE");
    close_blocks_->SetValue(true);
    close_blocks_->SetToolTip(
        "Both forms are legal. The reference's own worked example never closes its block,\n"
        "and the device's complaint is about how the telegram is put together.");
    fields_box->Add(close_blocks_, 0, wxALL, 4);

    root->Add(fields_box, 0, wxEXPAND | wxALL, 6);

    auto* headline_row = new wxBoxSizer(wxHORIZONTAL);
    headline_row->Add(new wxStaticText(this, wxID_ANY, "Headline (WT60)"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    headline_ = new wxTextCtrl(this, wxID_ANY, "gxnet");
    headline_row->Add(headline_, 1, wxALIGN_CENTER_VERTICAL);
    root->Add(headline_row, 0, wxEXPAND | wxALL, 6);

    // --- the elements -----------------------------------------------------

    auto* entries_box = new wxStaticBoxSizer(wxVERTICAL, this, "Elements, one per line (WT62, max 30 characters each)");
    wxWindow* ebox = entries_box->GetStaticBox();

    entries_ = new wxTextCtrl(ebox, wxID_ANY, "Continue?", wxDefaultPosition, wxSize(-1, 80), wxTE_MULTILINE);
    entries_box->Add(entries_, 0, wxEXPAND | wxALL, 6);

    auto* ids_row = new wxBoxSizer(wxHORIZONTAL);
    ids_row->Add(new wxStaticText(ebox, wxID_ANY, "first id (WW64)"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    first_id_ =
        new wxSpinCtrl(ebox, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(80, -1), wxSP_ARROW_KEYS, 0, kMaxId, 1);
    first_id_->SetToolTip(
        "Ids are handed out from here, one per element. The result names the one "
        "the operator picked.");
    ids_row->Add(first_id_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 20);

    ids_row->Add(new wxStaticText(ebox, wxID_ANY, "attribute (WW65)"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    attrib_ = new wxChoice(ebox, wxID_ANY);
    attrib_->Append("normal (0)");
    attrib_->Append("flashing (1)");
    attrib_->Append("delete (-1)");
    attrib_->SetSelection(0);
    attrib_->SetToolTip("WZW_DISPLAY_ATTRIB shares the coding of WZW_REMOTE_DISPLAY_ATTR.");
    ids_row->Add(attrib_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 20);

    ids_row->Add(new wxStaticText(ebox, wxID_ANY, "cursor starts on"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    active_ =
        new wxSpinCtrl(ebox, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(70, -1), wxSP_ARROW_KEYS, 1, 999, 1);
    active_->SetToolTip("A position in the list, one based. Not an id.");
    ids_row->Add(active_, 0, wxALIGN_CENTER_VERTICAL);

    entries_box->Add(ids_row, 0, wxALL, 6);
    root->Add(entries_box, 0, wxEXPAND | wxALL, 6);

    // --- waiting ----------------------------------------------------------

    auto* wait_box = new wxStaticBoxSizer(wxVERTICAL, this, "Waiting for the answer");
    wxWindow* wbox = wait_box->GetStaticBox();

    auto* wait_row = new wxBoxSizer(wxHORIZONTAL);
    wait_row->Add(new wxStaticText(wbox, wxID_ANY, "wait up to"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    wait_s_ =
        new wxSpinCtrl(wbox, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(80, -1), wxSP_ARROW_KEYS, 5, 600, 20);
    wait_row->Add(wait_s_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    wait_row->Add(new wxStaticText(wbox, wxID_ANY, "s"), 0, wxALIGN_CENTER_VERTICAL);
    wait_box->Add(wait_row, 0, wxALL, 6);

    poll_ = new wxCheckBox(wbox, wxID_ANY, "Ask for the answer once a second (A?WV63 by handle, then A?WW68)");
    poll_->SetValue(true);
    poll_->SetToolTip(
        "For devices that deliver nothing unasked. WZV_SDD_RESULT carries neither\n"
        "? nor ! in the reference, so reading it is unspecified rather than\n"
        "forbidden, and a device without it answers in one telegram.");
    wait_box->Add(poll_, 0, wxALL, 6);

    auto* wait_note =
        new wxStaticText(wbox, wxID_ANY,
                         "The wait does not occupy the connection: the send returns on the acknowledgement, and "
                         "the answer is\n"
                         "watched for separately. Arriving unasked needs both Connection boxes, the open flag "
                         "and the listener,\n"
                         "and a device configured to send on its own.");
    wait_note->SetForegroundColour(kMuted);
    wait_box->Add(wait_note, 0, wxALL, 6);

    root->Add(wait_box, 0, wxEXPAND | wxALL, 6);

    // --- send and answer --------------------------------------------------

    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    open_ = new wxButton(this, wxID_ANY, "Open dialog");
    buttons->Add(open_, 0, wxRIGHT, 12);
    needs_connection_.push_back(open_);
    open_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { open(); });

    // Exercises the listener without a line. It queues a WZV_SDD_RESULT on the
    // mock's spontaneous channel -- the same path a real answer would take --
    // which is the only way to tell "our side is ready" apart from "the device
    // is silent". Disabled against a real device, where the answer is the thing
    // being measured and one we invented would be worthless.
    simulate_ = new wxButton(this, wxID_ANY, "Simulate an answer");
    simulate_->SetToolTip("Mock only. Posts a WZV_SDD_RESULT on the spontaneous channel, "
                          "so the wait, the log and this panel can be seen working end to end.");
    simulate_->Enable(false);
    buttons->Add(simulate_, 0, wxRIGHT, 12);
    simulate_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { simulate(); });

    // No auto-resize: this label is rewritten on every tick while a dialog is
    // open, and one that sizes itself to its text walks the button beside it.
    status_ = new wxStaticText(this, wxID_ANY, "nothing open", wxDefaultPosition, wxDefaultSize,
                               wxST_NO_AUTORESIZE | wxST_ELLIPSIZE_END);
    status_->SetForegroundColour(kMuted);
    buttons->Add(status_, 1, wxALIGN_CENTER_VERTICAL);
    root->Add(buttons, 0, wxEXPAND | wxALL, 6);

    answer_ = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                               wxST_NO_AUTORESIZE | wxST_ELLIPSIZE_END);
    root->Add(answer_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

    auto* wire_row = new wxBoxSizer(wxHORIZONTAL);
    wire_row->Add(hint(this, "What goes on the wire"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 20);

    // The controls above cover the variations that have a name. This covers the
    // ones that do not: a field in another order, a token this panel has no
    // idea about, a value outside its range.
    hand_edit_ = new wxCheckBox(this, wxID_ANY, "edit by hand and send exactly this");
    wire_row->Add(hand_edit_, 0, wxALIGN_CENTER_VERTICAL);
    root->Add(wire_row, 0, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, 8);

    preview_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(-1, 70),
                              wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
    preview_->SetFont(wxFont(wxFontInfo(9).Family(wxFONTFAMILY_TELETYPE)));
    root->Add(preview_, 0, wxEXPAND | wxALL, 6);

    // No forced close here. XCL_DIALOG_CLOSE (XL09) exists and takes a long,
    // and the reference says nothing about what that long is -- so a button
    // sending the handle would be a guess. The documented way out is the
    // operator's own HOME key, which is the point of a dialog over a note.
    root->Add(hint(this,
                   "The dialog is closed from the terminal, with HOME. "
                   "XCL_DIALOG_CLOSE (XL09) takes an undocumented parameter,\n"
                   "so there is no button for it here."),
              0, wxALL, 6);

    finishLayout(root);

    const auto on_change = [this](wxCommandEvent& event) {
        refreshPreview();
        event.Skip();
    };
    kind_->Bind(wxEVT_CHOICE, on_change);
    attrib_->Bind(wxEVT_CHOICE, on_change);
    headline_->Bind(wxEVT_TEXT, on_change);
    entries_->Bind(wxEVT_TEXT, on_change);
    handle_->Bind(wxEVT_TEXT, on_change);
    elem_type_->Bind(wxEVT_TEXT, on_change);
    elem_count_->Bind(wxEVT_CHECKBOX, on_change);
    with_active_->Bind(wxEVT_CHECKBOX, on_change);
    with_headline_->Bind(wxEVT_CHECKBOX, on_change);
    close_blocks_->Bind(wxEVT_CHECKBOX, on_change);
    first_id_->Bind(wxEVT_TEXT, on_change);
    active_->Bind(wxEVT_TEXT, on_change);

    hand_edit_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent& event) {
        // Leaving hand editing puts the built telegram back, so the box never
        // shows something other than what the button would send.
        preview_->SetEditable(hand_edit_->GetValue());
        if (!hand_edit_->GetValue()) refreshPreview();
        event.Skip();
    });

    refreshPreview();
    onConnectionChanged(false);
}

std::int16_t DialogPanel::dialogType() const {
    const int index = kind_->GetSelection();
    if (index < 0 || index >= static_cast<int>(kKinds.size())) return kKinds.front().type;
    return kKinds[static_cast<std::size_t>(index)].type;
}

link::DialogSpec DialogPanel::spec() const {
    link::DialogSpec out;
    out.type = dialogType();
    out.element_type = static_cast<std::int16_t>(elem_type_->GetValue());
    out.handle = static_cast<std::int16_t>(handle_->GetValue());
    out.headline = utf8(headline_->GetValue());
    out.active = static_cast<std::int16_t>(active_->GetValue());
    out.with_headline = with_headline_->GetValue();
    out.with_element_count = elem_count_->GetValue();
    out.with_active = with_active_->GetValue();
    out.close_blocks = close_blocks_->GetValue();

    constexpr std::array<std::int16_t, 3> kAttributes{0, 1, -1};
    const int attribute = attrib_->GetSelection();
    const std::int16_t attrib =
        attribute >= 0 && attribute < static_cast<int>(kAttributes.size()) ? kAttributes[attribute] : 0;

    const int first = first_id_->GetValue();
    wxStringTokenizer lines(entries_->GetValue(), "\r\n", wxTOKEN_STRTOK);
    while (lines.HasMoreTokens()) {
        const wxString line = lines.GetNextToken().Trim().Trim(false);
        if (line.empty()) continue;
        link::DialogItem item;
        item.label = utf8(line);
        item.id = static_cast<std::int16_t>(std::min<int>(first + static_cast<int>(out.elements.size()), kMaxId));
        item.attrib = attrib;
        out.elements.push_back(std::move(item));
    }
    return out;
}

void DialogPanel::refreshPreview() {
    if (hand_edit_->GetValue()) return;

    const link::DialogSpec built = spec();
    if (built.elements.empty()) {
        preview_->ChangeValue("no elements: a dialog needs at least one");
        return;
    }

    auto lines = encodeLines(link::dialog(built));
    if (!lines) {
        preview_->ChangeValue(wx(lines.error.message));
        return;
    }
    wxString text;
    for (const std::string& line : *lines) text += wx(line) + "\n";
    preview_->ChangeValue(text);

    // Not enforced, because the reference states a maximum and says nothing
    // about what a device does with more -- and finding that out is one of the
    // things this panel is for.
    //
    // Characters, not bytes. The reference counts characters, and a Cyrillic
    // headline is two bytes each: counting bytes would flag a fifteen-character
    // Russian title as too long and send everyone chasing the wrong field.
    const auto tooLong = [](const std::string& value) {
        const auto length = utf8Length(value);
        return length && *length > kMaxLabel;
    };
    bool over = built.with_headline && tooLong(built.headline);
    for (const link::DialogItem& item : built.elements) over = over || tooLong(item.label);
    if (over) {
        preview_->AppendText("\n(a text is longer than the 30 characters the reference allows)");
    }
}

void DialogPanel::open() {
    if (waiting_) return;

    Telegram telegram;
    if (hand_edit_->GetValue()) {
        std::vector<std::string> lines;
        wxStringTokenizer tokens(preview_->GetValue(), "\r\n", wxTOKEN_STRTOK);
        while (tokens.HasMoreTokens()) lines.push_back(utf8(tokens.GetNextToken()));
        if (lines.empty()) {
            show(status_, "nothing to send", kBad);
            return;
        }
        auto parsed = parseLines(lines);
        if (!parsed) {
            show(status_, wx(parsed.error.message), kBad);
            return;
        }
        telegram = *parsed;
    } else {
        const link::DialogSpec built = spec();
        if (built.elements.empty()) {
            show(status_, "no elements, nothing to show", kBad);
            return;
        }
        telegram = link::dialog(built);
    }

    waiting_ = true;
    waited_s_ = 0.0;
    since_poll_s_ = 0.0;
    ask_ = Ask::Block;
    open_->Enable(false);
    answer_->SetLabel(wxEmptyString);
    status_->SetLabel("opening...");
    status_->SetForegroundColour(kMuted);

    // Subscribed before the send, not after. The device could in principle
    // answer faster than the acknowledgement comes back, and a listener armed
    // afterwards would miss exactly the case worth catching.
    if (listener_ == 0) {
        listener_ = session_.listen([this](const link::Exchange& exchange) {
            if (!waiting_) return;
            if (reportResult(exchange)) {
                stopWaiting();
                Layout();
            }
        });
    }

    // The connection's own timeout, not the operator's. This send waits for the
    // device to acknowledge the telegram, which takes milliseconds; the person
    // in front of the terminal is waited for separately, by the tick below.
    session_.send(std::move(telegram), /*expect_reply=*/true, [this](link::LinkResult<link::Exchange> result) {
        if (!result) {
            stopWaiting();
            status_->SetLabel(wx(result.error.str()));
            status_->SetForegroundColour(kBad);
            Layout();
            return;
        }
        reportSend(*result);
        Layout();
    });
}

void DialogPanel::reportSend(const link::Exchange& exchange) {
    // A dialog result on the request handle would be a surprise -- it is the
    // thing this panel spent a session establishing does not happen -- but if
    // the device ever does answer that way, it is the answer and it counts.
    if (reportResult(exchange)) {
        stopWaiting();
        return;
    }

    if (exchange.reply) {
        const auto code = link::returnCodeOf(*exchange.reply);
        if (code && *code != 0) {
            stopWaiting();
            status_->SetLabel("refused");
            status_->SetForegroundColour(kBad);
            answer_->SetLabel(wx(describeTelegram(*exchange.reply)));
            answer_->SetForegroundColour(kBad);
            return;
        }
    }

    if (exchange.reply_error) {
        status_->SetLabel("acknowledgement did not parse");
        status_->SetForegroundColour(kWarn);
        answer_->SetLabel(wx(exchange.reply_error->message));
        answer_->SetForegroundColour(kWarn);
        return;
    }

    status_->SetLabel("open on the terminal");
    status_->SetForegroundColour(kGood);
    answer_->SetLabel("Waiting for the answer on the spontaneous channel. "
                      "The line stays usable meanwhile.");
    answer_->SetForegroundColour(kMuted);
}

bool DialogPanel::reportResult(const link::Exchange& exchange) {
    if (!exchange.reply) return false;
    const auto result = link::parseDialogResult(*exchange.reply);
    if (!result) return false;

    answer_->UnsetToolTip();
    status_->SetLabel("answered");
    status_->SetForegroundColour(kGood);

    wxString text =
        wxString::Format("handle %d, WZW_EXIT %d: %s", static_cast<int>(result->handle),
                         static_cast<int>(result->exit), result->confirmed() ? "confirmed" : "cancelled with HOME");
    if (result->id) {
        text += wxString::Format("   |   WZW_SDD_ID %d", static_cast<int>(*result->id));
    }
    if (result->label) text += "   |   " + wx(*result->label);
    answer_->SetLabel(text);
    answer_->SetForegroundColour(result->confirmed() ? kGood : kWarn);
    answer_->SetToolTip(text);
    return true;
}

void DialogPanel::stopWaiting() {
    waiting_ = false;
    open_->Enable(session_.connected());
}

void DialogPanel::pollAnswer() {
    // One at a time. The transport has a single thread and a poll that has not
    // come back yet is already asking the question this one would ask.
    if (poll_in_flight_ || ask_ == Ask::None) return;
    poll_in_flight_ = true;

    const Ask asked = ask_;
    Telegram telegram = asked == Ask::Block
                            ? link::dialogResultQuery(static_cast<std::int16_t>(handle_->GetValue()))
                            : link::dialogExitQuery();

    session_.send(std::move(telegram), /*expect_reply=*/true,
                  [this, asked](link::LinkResult<link::Exchange> result) {
                      poll_in_flight_ = false;
                      if (!waiting_) return;

                      // "fremdes Kommando" is the device saying it has no such
                      // subfunction, and it will keep saying it. Step down once
                      // rather than filling the log with the same refusal.
                      const bool unknown =
                          !result && result.error.message.find("fremdes Kommando") != std::string::npos;
                      if (unknown) {
                          ask_ = asked == Ask::Block ? Ask::Word : Ask::None;
                          if (ask_ == Ask::None) {
                              answer_->SetLabel(
                                  "Neither WV63 nor WW68 can be read on this device. "
                                  "The answer has nowhere to come from; see the notes, section 16.");
                              answer_->SetForegroundColour(kWarn);
                              Layout();
                          }
                          return;
                      }
                      if (!result) return;

                      if (reportResult(*result)) {
                          stopWaiting();
                          Layout();
                          return;
                      }

                      // WZW_EXIT on its own is not a whole result, but it is the
                      // half that says the operator acted at all.
                      if (asked == Ask::Word && result->reply) {
                          static constexpr Token kExit = knownToken("WZW_EXIT").token;  // WW68
                          if (const auto exit = link::valueOf(*result->reply, kExit)) {
                              if (const auto* word = std::get_if<std::int16_t>(&*exit)) {
                                  stopWaiting();
                                  status_->SetLabel("answered, through WZW_EXIT");
                                  status_->SetForegroundColour(kGood);
                                  answer_->SetLabel(wxString::Format("WZW_EXIT %d: %s", static_cast<int>(*word),
                                                                     *word == 0 ? "input OK" : "cancelled with HOME"));
                                  answer_->SetForegroundColour(*word == 0 ? kGood : kWarn);
                                  Layout();
                              }
                          }
                      }
                  });
}

void DialogPanel::simulate() {
    link::MockTransport* device = session_.mock();
    if (!device) return;

    // The shape a real result would have, built from what this panel asked for
    // so the handle and the chosen id are the ones on screen rather than
    // constants that happen to look right.
    const link::DialogSpec built = spec();
    std::string line = "A!WV63|WW60|" + std::to_string(built.handle) + "|WW68|0";
    if (!built.elements.empty()) {
        line += "|WW64|" + std::to_string(built.elements.front().id);
        line += "|WT62|" + built.elements.front().label;
    }
    line += "|LX02";
    device->postSpontaneous({line});
}

void DialogPanel::onTick(double seconds) {
    if (!waiting_) return;
    waited_s_ += seconds;

    if (waited_s_ >= wait_s_->GetValue()) {
        stopWaiting();
        status_->SetLabel(wxString::Format("no answer within %d s", wait_s_->GetValue()));
        status_->SetForegroundColour(kWarn);
        answer_->SetLabel(
            session_.settings().spontaneous
                ? "The dialog may still be on the terminal: nothing arrived on the "
                  "spontaneous channel either. Check GGW_SENDKANAL_A_ENABLE (A?GWBF)."
                : "Nothing was listening: the connection did not subscribe to spontaneous "
                  "messages. Tick that on the Connection tab and reconnect.");
        answer_->SetForegroundColour(kMuted);
        Layout();
        return;
    }

    if (poll_->GetValue()) {
        since_poll_s_ += seconds;
        if (since_poll_s_ >= 1.0) {
            since_poll_s_ = 0.0;
            pollAnswer();
        }
    }

    // Once a second, not twenty times: the tick runs at 50 ms and rewriting a
    // label it cannot see the difference in is pure repainting.
    const wxString text = wxString::Format("waiting for the operator, %.0f of %d s", waited_s_, wait_s_->GetValue());
    if (text != status_->GetLabel()) {
        status_->SetLabel(text);
        status_->SetForegroundColour(kMuted);
    }
}

DialogPanel::~DialogPanel() {
    if (listener_ != 0) session_.unlisten(listener_);
}

void DialogPanel::onConnectionChanged(bool connected) {
    for (wxWindow* control : needs_connection_) control->Enable(connected);
    simulate_->Enable(connected && session_.mock() != nullptr);
    if (!connected && waiting_) {
        // The dialog is on the terminal and this program can no longer say
        // anything about it. The operator's HOME key still works.
        stopWaiting();
        status_->SetLabel(
            "disconnected while a dialog was open. It is still on the terminal; "
            "the operator can leave it with HOME");
        status_->SetForegroundColour(kWarn);
    }
}

}  // namespace gxdemo::panels
