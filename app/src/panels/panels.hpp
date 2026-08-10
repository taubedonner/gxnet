// SPDX-License-Identifier: MIT
#pragma once

#include <wx/wx.h>

#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/listctrl.h>
#include <wx/spinctrl.h>
#include <wx/treectrl.h>

#include <array>
#include <chrono>
#include <optional>
#include <vector>

#include "format.hpp"
#include "gxnet/link/operations.hpp"
#include "panels/ui.hpp"
#include "session.hpp"

namespace gxdemo::panels {

/// One value the panel keeps in view: what the device last reported, or why the
/// last attempt failed.
///
/// Everything here is asynchronous -- a request is posted and the answer lands
/// some milliseconds later on the UI thread -- so each readable thing carries
/// its own small state rather than the panel blocking on it.
struct Field {
    Token token;
    Value value{};
    std::string error;
    bool pending = false;

    [[nodiscard]] bool has() const { return !isEmpty(value); }
};

/// Base for every tab: holds the session and knows when the connection state
/// changed, so controls can be enabled and disabled in one place.
///
/// Scrolled, because the log sits below the tabs and takes a fixed share of the
/// window. Without this the bottom of a tall panel is simply unreachable --
/// the buttons are there, just under the log.
class Panel : public wxScrolledWindow {
public:
    Panel(wxWindow* parent, Session& session)
        : wxScrolledWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL | wxHSCROLL),
          session_(session) {}

    /// Called by the frame whenever the connection state may have changed.
    virtual void onConnectionChanged(bool connected) { (void)connected; }

    /// Called on every timer tick, for panels that poll.
    virtual void onTick(double seconds) { (void)seconds; }

protected:
    /// Installs the sizer and makes the panel scroll to fit it. Every panel
    /// ends its constructor with this instead of a bare SetSizer.
    void finishLayout(wxSizer* sizer) {
        SetSizer(sizer);
        // Ten pixels a click; the virtual size follows the sizer, so the
        // scrollbar appears exactly when the content stops fitting.
        SetScrollRate(10, 10);
        FitInside();
    }

    Session& session_;
};

class ConnectionPanel final : public Panel {
public:
    ConnectionPanel(wxWindow* parent, Session& session);
    void onConnectionChanged(bool connected) override;
    void onTick(double seconds) override;

private:
    void pullSettings();
    void refreshStatus();

    /// Last text put on the status label. A failed open never changes
    /// connected(), so the panel cannot rely on onConnectionChanged to show it;
    /// it polls instead, and this is what keeps the label from being rewritten
    /// -- and flickering -- twenty times a second.
    wxString shown_status_;

    wxChoice* transport_ = nullptr;
    wxTextCtrl* device_ = nullptr;
    wxTextCtrl* user_ = nullptr;
    wxTextCtrl* prog_id_ = nullptr;
    wxSpinCtrl* timeout_ = nullptr;
    wxCheckBox* spontaneous_ = nullptr;
    wxCheckBox* exclusive_ = nullptr;
    wxCheckBox* probe_text_mode_ = nullptr;
    wxCheckBox* use_send_one_ = nullptr;
    wxCheckBox* listen_ = nullptr;
    wxButton* connect_ = nullptr;
    wxStaticText* status_ = nullptr;
    wxStaticText* spontaneous_count_ = nullptr;
    /// Last counters shown, so the tick only redraws when they move.
    std::size_t shown_records_ = 0;
    std::size_t shown_polls_ = 0;
    wxStaticText* text_mode_ = nullptr;
    wxStaticText* warning_ = nullptr;
};

class DevicePanel final : public Panel {
public:
    DevicePanel(wxWindow* parent, Session& session);
    void onConnectionChanged(bool connected) override;

private:
    void readAll();
    void readField(Field& field, wxStaticText* target);
    void writeText(std::size_t index);
    void writeDate(std::size_t index);

    Field version_;
    std::array<Field, 2> dates_;
    std::array<Field, 3> texts_;

    wxStaticText* version_value_ = nullptr;
    wxStaticText* version_note_ = nullptr;
    std::array<wxStaticText*, 2> date_values_{};
    std::array<wxSpinCtrl*, 2> date_day_{};
    std::array<wxSpinCtrl*, 2> date_month_{};
    std::array<wxSpinCtrl*, 2> date_year_{};
    std::array<wxStaticText*, 3> text_values_{};
    std::array<wxTextCtrl*, 3> text_edits_{};
    std::array<wxStaticText*, 3> text_counters_{};
    std::vector<wxWindow*> needs_connection_;
};

class UniquePanel final : public Panel {
public:
    UniquePanel(wxWindow* parent, Session& session);
    void onConnectionChanged(bool connected) override;

private:
    void refresh();
    void setIntake(std::int16_t state);
    void clearBuffer();
    void probeReady();

    Field intake_;
    Field ready_;

    wxStaticText* intake_value_ = nullptr;
    wxStaticText* intake_note_ = nullptr;
    wxStaticText* ready_value_ = nullptr;
    wxStaticText* ready_note_ = nullptr;
    wxStaticText* action_ = nullptr;
    std::vector<wxWindow*> needs_connection_;
};

/// The full PLU change -- what the PLU key on the terminal does.
class PluPanel final : public Panel {
public:
    PluPanel(wxWindow* parent, Session& session);
    void onConnectionChanged(bool connected) override;

private:
    [[nodiscard]] Telegram buildChange() const;
    void refreshPreviewLine();
    void readCurrent();
    void change();

    Field current_;
    /// The number the last change asked for, until the read-back settles it.
    std::optional<std::int32_t> requested_;

    wxStaticText* current_value_ = nullptr;
    wxSpinCtrl* plu_ = nullptr;
    wxCheckBox* with_customer_ = nullptr;
    wxSpinCtrl* customer_ = nullptr;
    wxStaticText* preview_ = nullptr;
    wxStaticText* result_ = nullptr;
    wxButton* change_ = nullptr;
    std::vector<wxWindow*> needs_connection_;
};

/// Standard dialogs on the operator's terminal: WZV_SDD_START and its answer.
class DialogPanel final : public Panel {
public:
    DialogPanel(wxWindow* parent, Session& session);
    ~DialogPanel() override;
    void onConnectionChanged(bool connected) override;
    void onTick(double seconds) override;

private:
    [[nodiscard]] std::int16_t dialogType() const;
    [[nodiscard]] link::DialogSpec spec() const;
    void refreshPreview();
    void open();
    /// Whatever the send itself came back with -- an acknowledgement, or a
    /// refusal. Not the operator's answer.
    void reportSend(const link::Exchange& exchange);
    /// True when the record carried the operator's answer, which is what stops
    /// the wait.
    bool reportResult(const link::Exchange& exchange);
    void stopWaiting();
    /// Mock only: queue an answer on the spontaneous channel.
    void simulate();
    /// One round of asking the device for the answer instead of waiting for it.
    void pollAnswer();

    wxChoice* kind_ = nullptr;
    wxSpinCtrl* handle_ = nullptr;
    wxSpinCtrl* elem_type_ = nullptr;
    wxCheckBox* elem_count_ = nullptr;
    wxCheckBox* with_active_ = nullptr;
    wxCheckBox* with_headline_ = nullptr;
    wxCheckBox* close_blocks_ = nullptr;
    wxTextCtrl* headline_ = nullptr;
    wxTextCtrl* entries_ = nullptr;
    wxSpinCtrl* first_id_ = nullptr;
    wxChoice* attrib_ = nullptr;
    wxSpinCtrl* active_ = nullptr;
    wxSpinCtrl* wait_s_ = nullptr;
    wxCheckBox* hand_edit_ = nullptr;
    wxTextCtrl* preview_ = nullptr;
    wxStaticText* status_ = nullptr;
    wxStaticText* answer_ = nullptr;
    wxButton* open_ = nullptr;
    wxButton* simulate_ = nullptr;

    /// A dialog is open on the terminal and this panel is waiting for the
    /// answer.
    ///
    /// The wait no longer occupies the transport: the send returns as soon as
    /// the device acknowledges it, and the answer -- if it ever comes -- arrives
    /// as a spontaneous record. So the line stays usable while a dialog is up,
    /// which it did not when the worker sat inside ReceiveOne for the whole
    /// timeout.
    bool waiting_ = false;
    double waited_s_ = 0.0;
    std::size_t listener_ = 0;

    /// How the answer is being asked for, and it degrades in one direction.
    ///
    /// `Block` asks `WZV_SDD_RESULT` by handle. If the device calls that a
    /// foreign command, there is no point asking again, so the next round drops
    /// to `Word` -- `WZW_EXIT` on its own -- and if that is refused too, to
    /// `None`, which stops asking and says so.
    enum class Ask { Block, Word, None };
    Ask ask_ = Ask::Block;
    wxCheckBox* poll_ = nullptr;
    double since_poll_s_ = 0.0;
    bool poll_in_flight_ = false;
    std::vector<wxWindow*> needs_connection_;
};

/// Programmable softkeys on the terminal: WZV_REMOTE_TO_SOFTKEY and its answer.
///
/// The alternative to the Dialogs tab, and on this device the one that is not
/// refused outright -- see the note the panel puts at the top of itself.
class SoftkeyPanel final : public Panel {
public:
    SoftkeyPanel(wxWindow* parent, Session& session);
    ~SoftkeyPanel() override;
    void onConnectionChanged(bool connected) override;
    void onTick(double seconds) override;

private:
    [[nodiscard]] link::SoftkeySpec spec() const;
    void refreshPreview();
    /// Trailing underscore: `program` is a common enough word that a member of
    /// that name shadows more than it should.
    void program_();
    void clear(bool everything);
    void listen();
    void report(const link::Exchange& exchange);
    /// True when the record carried a press.
    bool reportPress(const link::Exchange& exchange);
    void stopWaiting();
    /// One round of asking the device whether a key was pressed.
    void pollPress();
    /// WZV_GXNET_META_SOFTKEY_INFO: does the key exist, and is it active.
    void readInfo();

    wxSpinCtrl* number_ = nullptr;
    wxCheckBox* all_keys_ = nullptr;
    wxChoice* type_ = nullptr;
    wxSpinCtrl* digits_ = nullptr;
    wxTextCtrl* label_ = nullptr;
    wxStaticText* counter_ = nullptr;
    wxChoice* attribute_ = nullptr;
    wxButton* listen_ = nullptr;
    wxSpinCtrl* wait_s_ = nullptr;
    wxStaticText* countdown_ = nullptr;
    wxStaticText* answer_ = nullptr;
    wxTextCtrl* preview_ = nullptr;
    wxStaticText* status_ = nullptr;

    bool waiting_ = false;
    double waited_s_ = 0.0;
    std::size_t listener_ = 0;

    /// Same shape as the dialog's: ask by key number, drop to asking without
    /// one when the device calls that a foreign command, then stop.
    enum class Ask { ByNumber, Any, None };
    Ask ask_ = Ask::ByNumber;
    wxCheckBox* poll_ = nullptr;
    wxButton* info_ = nullptr;
    double since_poll_s_ = 0.0;
    bool poll_in_flight_ = false;
    std::vector<wxWindow*> needs_connection_;
};

class BufferPanel final : public Panel {
public:
    BufferPanel(wxWindow* parent, Session& session);
    void onConnectionChanged(bool connected) override;
    void onTick(double seconds) override;

private:
    void poll();
    void showPackages(const Telegram& telegram);

    wxListCtrl* packages_ = nullptr;
    wxCheckBox* auto_poll_ = nullptr;
    wxSpinCtrl* interval_ = nullptr;
    wxSpinCtrl* buffer_size_ = nullptr;
    wxStaticText* status_ = nullptr;
    wxTextCtrl* raw_ = nullptr;
    double since_last_s_ = 0.0;
    bool pending_ = false;
    std::vector<wxWindow*> needs_connection_;
};

class TerminalPanel final : public Panel {
public:
    TerminalPanel(wxWindow* parent, Session& session);
    void onConnectionChanged(bool connected) override;
    void onTick(double seconds) override;

private:
    void show();
    void clear();
    void refreshPreview();
    [[nodiscard]] Telegram build(std::int16_t attribute, bool with_text) const;

    wxTextCtrl* text_ = nullptr;
    wxStaticText* counter_ = nullptr;
    wxChoice* attribute_ = nullptr;
    wxCheckBox* auto_clear_ = nullptr;
    wxSpinCtrl* auto_clear_after_ = nullptr;
    wxStaticText* status_ = nullptr;
    wxTextCtrl* preview_ = nullptr;
    wxStaticText* countdown_ = nullptr;
    double shown_for_s_ = 0.0;
    bool on_screen_ = false;
    /// A clear is in flight. Without this the auto-clear fires on every tick
    /// until the answer arrives -- twenty telegrams a second, for a job that
    /// needs one.
    bool clearing_ = false;
    std::vector<wxWindow*> needs_connection_;
};

/// The FTP family on the automation object: undocumented, and the whole reason
/// this panel lets the shape of the call be edited rather than fixing it.
class FtpPanel final : public Panel {
public:
    static constexpr std::size_t kArgs = 4;

    FtpPanel(wxWindow* parent, Session& session);
    void onConnectionChanged(bool connected) override;

private:
    void applyPreset(int index);
    void call();

    wxChoice* method_ = nullptr;
    wxTextCtrl* method_name_ = nullptr;
    std::array<wxChoice*, kArgs> kind_{};
    std::array<wxTextCtrl*, kArgs> value_{};
    std::array<wxStaticText*, kArgs> note_{};
    wxButton* call_ = nullptr;
    wxStaticText* status_ = nullptr;
    wxTextCtrl* result_ = nullptr;
    std::vector<wxWindow*> needs_connection_;
};

class ConsolePanel final : public Panel {
public:
    ConsolePanel(wxWindow* parent, Session& session);
    void onConnectionChanged(bool connected) override;

private:
    void send();
    void updatePreview();
    void followAccess(Access access);
    void fillTree(const Telegram& telegram);

    /// Direction the reply checkbox was last set from.
    std::optional<Access> shown_access_;

    wxTextCtrl* input_ = nullptr;
    wxStaticText* preview_ = nullptr;
    wxCheckBox* expect_reply_ = nullptr;
    wxListBox* history_ = nullptr;
    wxTreeCtrl* tree_ = nullptr;
    wxTextCtrl* raw_ = nullptr;
    wxButton* send_ = nullptr;
};

/// The bundled subfunction table, searchable. The one tab that works with no
/// device: it answers "what is this token" without opening the reference.
class ReferencePanel final : public Panel {
public:
    ReferencePanel(wxWindow* parent, Session& session);

private:
    /// Applies the query to the whole table.
    void refill();
    /// Puts the selected entry into the detail pane.
    void showSelected();

    wxTextCtrl* filter_ = nullptr;
    wxCheckBox* in_meanings_ = nullptr;
    wxStaticText* count_ = nullptr;
    wxListBox* list_ = nullptr;
    wxTextCtrl* detail_ = nullptr;

    /// Tokens behind the visible rows, in the same order.
    std::vector<Token> shown_;
};

/// The exchange log, shown below the tabs rather than as one of them.
class LogView final : public wxPanel {
public:
    LogView(wxWindow* parent, Session& session);

    /// Appends whatever is new since the last call.
    void refresh();

private:
    /// Brings the last line into view without scrolling sideways.
    void scrollToEnd();

    Session& session_;
    wxTextCtrl* text_ = nullptr;
    wxCheckBox* follow_ = nullptr;
    wxCheckBox* details_ = nullptr;
    std::size_t shown_ = 0;
};

}  // namespace gxdemo::panels
