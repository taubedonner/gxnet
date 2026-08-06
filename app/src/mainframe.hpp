// SPDX-License-Identifier: MIT
#pragma once

#include <wx/wx.h>

#include <wx/notebook.h>
#include <wx/splitter.h>
#include <wx/statusbr.h>
#include <wx/timer.h>

#include <vector>

#include "panels/panels.hpp"
#include "session.hpp"

namespace gxdemo {

class MainFrame final : public wxFrame {
public:
    MainFrame();

private:
    void onTimer(wxTimerEvent& event);
    void onClose(wxCloseEvent& event);
    void onConnectToggle(wxCommandEvent& event);
    void onQuit(wxCommandEvent& event);
    void onAbout(wxCommandEvent& event);

    void loadSettings();
    void saveSettings();

    Session session_;

    wxNotebook* tabs_ = nullptr;
    wxSplitterWindow* splitter_ = nullptr;
    panels::LogView* log_view_ = nullptr;
    std::vector<panels::Panel*> panels_;

    /// Drives Session::update(), which is what delivers transport results onto
    /// this thread. Nothing else moves them across, so the interval is also the
    /// worst-case latency of every answer: 50 ms is imperceptible and costs
    /// nothing, since a tick with an empty queue does no work.
    wxTimer timer_;
    bool was_connected_ = false;
};

}  // namespace gxdemo
