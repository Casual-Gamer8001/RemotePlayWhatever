#ifndef CONTROLFRAME_H
#define CONTROLFRAME_H
#include <vector>
#include <wx/wx.h>
#include "RemotePlayInviteHandler.h"

// A window that appears when guests connect, showing one row per guest
// (avatar + name + keyboard/mouse/controller toggle buttons), plus an
// "Everyone" row when there are several guests. Clicking a toggle grants or
// revokes that peripheral. Global Ctrl+Alt hotkeys do the same for everyone
// while the host is in the game. Driven entirely by the handler's roster;
// rebuilds itself whenever it changes.
class ControlFrame : public wxFrame
{
public:
    ControlFrame(RemotePlayInviteHandler* handler);
    ~ControlFrame();

    // Hidden or not, this window must never keep the app running after the
    // main window is closed.
    virtual bool ShouldPreventAppExit() const override { return false; }

    // List another in-game shortcut (one the main window owns) in the hint.
    void AddShortcutHint(const wxString& what);

private:
    void Rebuild();
    void OnPoll(wxTimerEvent& evt);
    void OnClose(wxCloseEvent& evt);
    void OnHotKey(wxKeyEvent& evt);
    // Hold the Ctrl+Alt shortcuts only while guests are connected.
    void SetHotKeysActive(bool on);
    void ShowHint();
    void AddToggle(wxWindow* parent, wxSizer* sizer, const wxString& label,
                   bool on, uint64 guestKey, int which);
    // Name shown for a guest; sets placeholder when Steam doesn't know it yet.
    wxString GuestName(const ConnectedGuest& g, bool& placeholder);

    RemotePlayInviteHandler* m_handler;
    wxScrolledWindow* m_container;
    wxBoxSizer* m_sizer;
    wxStaticText* m_hotkeyHint;
    wxTimer m_timer;
    uint32 m_lastRevision;
    size_t m_lastGuestCount;
    bool m_waitingForNames;
    bool m_hotkeysActive;
    std::vector<int> m_hotkeys;
    wxArrayString m_hintItems;      // this window's registered shortcuts
    wxArrayString m_extraHints;     // ones the main window owns
};

#endif // CONTROLFRAME_H
