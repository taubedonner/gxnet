// SPDX-License-Identifier: MIT
#include "mainframe.hpp"

#include <wx/config.h>

#include "build_info.hpp"
#include "gxnet/link/bcs.hpp"

namespace gxdemo {
namespace {

enum {
    ID_Timer = wxID_HIGHEST + 1,
    ID_Connect,
};

constexpr int kTimerIntervalMs = 50;

}  // namespace

MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, "gxdemo - GxNet bench", wxDefaultPosition, wxSize(1000, 760)), timer_(this, ID_Timer) {
    loadSettings();

#ifdef __WXMSW__
    // Loads the icon compiled into the executable by assets/gxdemo.rc. The name
    // has to match the resource name there.
    SetIcon(wxICON(aaagxdemo));
#endif

    auto* menu = new wxMenu;
    menu->Append(ID_Connect, "&Connect / disconnect\tCtrl-K");
    menu->AppendSeparator();
    menu->Append(wxID_EXIT);

    auto* help = new wxMenu;
    help->Append(wxID_ABOUT);

    auto* bar = new wxMenuBar;
    bar->Append(menu, "&Session");
    bar->Append(help, "&Help");
    SetMenuBar(bar);

    CreateStatusBar(3);
    SetStatusText("not connected", 0);

    splitter_ = new wxSplitterWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE);
    splitter_->SetMinimumPaneSize(80);

    tabs_ = new wxNotebook(splitter_, wxID_ANY);

    auto* connection = new panels::ConnectionPanel(tabs_, session_);
    auto* device = new panels::DevicePanel(tabs_, session_);
    auto* unique = new panels::UniquePanel(tabs_, session_);
    auto* plu = new panels::PluPanel(tabs_, session_);
    auto* buffer = new panels::BufferPanel(tabs_, session_);
    auto* terminal = new panels::TerminalPanel(tabs_, session_);
    auto* dialog = new panels::DialogPanel(tabs_, session_);
    auto* softkey = new panels::SoftkeyPanel(tabs_, session_);
    auto* ftp = new panels::FtpPanel(tabs_, session_);
    auto* console = new panels::ConsolePanel(tabs_, session_);
    auto* reference = new panels::ReferencePanel(tabs_, session_);

    tabs_->AddPage(connection, "Connection", true);
    tabs_->AddPage(device, "Device");
    tabs_->AddPage(unique, "Unique data");
    tabs_->AddPage(plu, "PLU");
    tabs_->AddPage(buffer, "Buffer");
    tabs_->AddPage(terminal, "Terminal");
    tabs_->AddPage(dialog, "Dialogs");
    tabs_->AddPage(softkey, "Softkeys");
    tabs_->AddPage(ftp, "FTP");
    tabs_->AddPage(console, "Console");
    tabs_->AddPage(reference, "Reference");

    panels_ = {connection, device, unique, plu, buffer, terminal, dialog, softkey, ftp, console, reference};

    log_view_ = new panels::LogView(splitter_, session_);

    splitter_->SplitHorizontally(tabs_, log_view_, -220);
    // Growing the window grows the tab above, not the log below. The default
    // gravity is 0.0, which does the opposite: the log took every pixel of a
    // maximised window while the panel that the pixels were for stayed the size
    // it started at.
    splitter_->SetSashGravity(1.0);

    Bind(wxEVT_TIMER, &MainFrame::onTimer, this, ID_Timer);
    Bind(wxEVT_CLOSE_WINDOW, &MainFrame::onClose, this);
    Bind(wxEVT_MENU, &MainFrame::onConnectToggle, this, ID_Connect);
    Bind(wxEVT_MENU, &MainFrame::onQuit, this, wxID_EXIT);
    Bind(wxEVT_MENU, &MainFrame::onAbout, this, wxID_ABOUT);

    timer_.Start(kTimerIntervalMs);
}

void MainFrame::onTimer(wxTimerEvent&) {
    // Runs every completion the transport thread has finished. Panel callbacks
    // therefore execute here, on the UI thread, and may touch controls freely.
    session_.update();

    log_view_->refresh();

    const bool connected = session_.connected();
    if (connected != was_connected_) {
        was_connected_ = connected;
        for (panels::Panel* panel : panels_) {
            panel->onConnectionChanged(connected);
        }
    }

    for (panels::Panel* panel : panels_) {
        panel->onTick(kTimerIntervalMs / 1000.0);
    }

    SetStatusText(wx(session_.status()), 0);

    const Session::Settings& settings = session_.settings();
    SetStatusText(
        wxString::Format("%s | %s", settings.kind == Session::Kind::Bcs ? "BCS" : "in-memory", wx(settings.device)), 1);

    if (const std::size_t pending = session_.pending(); pending > 0) {
        SetStatusText(wxString::Format("%zu queued", pending), 2);
    } else {
        SetStatusText(session_.busy() ? "working" : "", 2);
    }
}

void MainFrame::onConnectToggle(wxCommandEvent&) {
    if (session_.connected())
        session_.disconnect();
    else
        session_.connect();
}

void MainFrame::onQuit(wxCommandEvent&) { Close(true); }

void MainFrame::onAbout(wxCommandEvent&) {
    wxMessageBox(wxString::Format("gxdemo %s\n"
                                  "build %s, commit %s, %s\n\n"
                                  "A bench for the GxNet telegram language and the link to a Bizerba\n"
                                  "Gx device through _connect.BRAIN.\n\n"
                                  "The in-memory device remembers what is written to it and reports\n"
                                  "it back. It is not a simulation of a labeller.",
                                  kVersion, kBuild, kCommit, kBuildDate),
                 "About gxdemo", wxOK | wxICON_INFORMATION, this);
}

void MainFrame::onClose(wxCloseEvent& event) {
    timer_.Stop();
    saveSettings();

    // Before the panels go: disconnecting drops the worker thread, and with it
    // every queued completion that would otherwise fire into destroyed windows.
    session_.disconnect();

    event.Skip();
}

void MainFrame::loadSettings() {
    wxConfigBase* config = wxConfig::Get();
    Session::Settings& settings = session_.settings();

    settings.device = utf8(config->Read("device", wx(settings.device)));
    settings.user = utf8(config->Read("user", wx(settings.user)));
    settings.prog_id = utf8(config->Read("prog-id", wx(settings.prog_id)));
    settings.timeout_ms = static_cast<int>(config->ReadLong("timeout-ms", settings.timeout_ms));

    const wxString kind = config->Read("transport", "mock");
    settings.kind = (kind == "bcs" && link::BcsTransport::available()) ? Session::Kind::Bcs : Session::Kind::Mock;
}

void MainFrame::saveSettings() {
    wxConfigBase* config = wxConfig::Get();
    const Session::Settings& settings = session_.settings();

    config->Write("device", wx(settings.device));
    config->Write("user", wx(settings.user));
    config->Write("prog-id", wx(settings.prog_id));
    config->Write("timeout-ms", static_cast<long>(settings.timeout_ms));
    config->Write("transport", settings.kind == Session::Kind::Bcs ? "bcs" : "mock");
    config->Flush();
}

}  // namespace gxdemo
