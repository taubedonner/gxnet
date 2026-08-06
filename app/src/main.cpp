// SPDX-License-Identifier: MIT
//
// gxdemo -- a bench for the GxNet library and the link to a Bizerba Gx device.
//
// Two things it is for: exercising the features the line automation will need
// (unique-data changeover, buffer reads, terminal notes), and being a plain
// channel to the device when a question can only be answered by asking it.
//
// It runs against an in-memory device anywhere, and against the real one
// through _connect.BRAIN on Windows. The panels do not know which.

#include <wx/wx.h>

#include <wx/config.h>

#include "mainframe.hpp"

namespace gxdemo {

class GxDemoApp final : public wxApp {
public:
    bool OnInit() override {
        if (!wxApp::OnInit()) return false;

        // Settings land in the registry under HKCU\Software\gxnet\gxdemo on
        // Windows, and in a dotfile elsewhere.
        SetVendorName("gxnet");
        SetAppName("gxdemo");
        wxConfigBase::Set(new wxConfig("gxdemo", "gxnet"));

        auto* frame = new MainFrame();
        frame->Show(true);
        return true;
    }

    int OnExit() override {
        delete wxConfigBase::Set(nullptr);
        return wxApp::OnExit();
    }
};

}  // namespace gxdemo

wxIMPLEMENT_APP(gxdemo::GxDemoApp);
