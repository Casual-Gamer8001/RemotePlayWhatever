#ifndef CONTROLFRAME_H
#define CONTROLFRAME_H
#include <vector>
#include <wx/wx.h>
#include "RemotePlayInviteHandler.h"

class AvatarCtrl;
class FlatButton;
class WrapLabel;

// A window that appears when guests connect: a card per guest (avatar, name,
// how long they've been there, keyboard/mouse/controller toggles and a remove
// button), plus an "Everyone" row when there are several guests. Clicking a
// toggle grants or revokes that peripheral. Global Ctrl+Alt hotkeys do the
// same for everyone while the host is in the game. Driven by the handler's
// roster: rebuilt when guests come or go, updated in place otherwise.
class ControlFrame : public wxFrame
{
public:
    ControlFrame(RemotePlayInviteHandler* handler);
    ~ControlFrame();

    // Hidden or not, this window must never keep the app running after the
    // main window is closed.
    virtual bool ShouldPreventAppExit() const override { return false; }

    // List another in-game shortcut, Ctrl+Alt+key (one the main window owns),
    // in the hint at the bottom.
    void AddShortcutHint(char key, const wxString& what);

private:
    struct GuestRow
    {
        uint64 key;
        AvatarCtrl* avatar;
        wxStaticText* name;
        wxStaticText* joined;
        FlatButton* toggles[3];
        FlatButton* remove;
        wxString shownName;
        bool hasPicture;
    };
    struct HintKey
    {
        char key;
        wxString what;
    };

    void Rebuild();
    // Names, join times and toggles of the cards already shown.
    void RefreshRows();
    // The cards are for the guests connected right now (same ones, same order).
    bool RowsMatchGuests() const;
    // As tall as the cards need (within the screen), so there's no empty band.
    void FitHeight();
    void OnPoll(wxTimerEvent& evt);
    void OnClose(wxCloseEvent& evt);
    void OnHotKey(wxKeyEvent& evt);
    // Hold the Ctrl+Alt shortcuts only while guests are connected.
    void SetHotKeysActive(bool on);
    void ShowHint();
    // A keyboard (0), mouse (1) or controller (2) toggle for one guest, or
    // for everyone (guestKey 0).
    FlatButton* AddToggle(wxWindow* parent, wxSizer* sizer, int which, uint64 guestKey);
    void Toggle(uint64 guestKey, int which);
    // Name shown for a guest; sets placeholder when Steam doesn't know it yet.
    wxString GuestName(const ConnectedGuest& g, bool& placeholder);

    RemotePlayInviteHandler* m_handler;
    wxPanel* m_body;
    wxStaticText* m_title;
    FlatButton* m_endButton;
    // The "Everyone" row's toggles (inside the list, lined up with the
    // guests' toggles); null with fewer than two guests.
    FlatButton* m_everyoneToggles[3];
    wxScrolledWindow* m_container;
    wxBoxSizer* m_sizer;
    wxPanel* m_footer;
    WrapLabel* m_hint;
    std::vector<GuestRow> m_rows;
    wxTimer m_timer;
    uint32 m_lastRevision;
    size_t m_lastGuestCount;
    int m_pollTicks;
    bool m_waitingForNames;
    bool m_hotkeysActive;
    std::vector<int> m_hotkeys;
    std::vector<HintKey> m_hintKeys;    // this window's registered shortcuts
    std::vector<HintKey> m_extraHints;  // ones the main window owns
};

#endif // CONTROLFRAME_H
