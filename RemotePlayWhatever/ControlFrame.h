#ifndef CONTROLFRAME_H
#define CONTROLFRAME_H
#include <vector>
#include <wx/wx.h>
#include "ControllerOrder.h"
#include "RemotePlayInviteHandler.h"

class AvatarCtrl;
class FlatButton;
class WindowPlace;
class WrapLabel;

// A window that appears when guests connect: a card per guest (avatar, name,
// how long they've been there, keyboard/mouse/controller toggles and a remove
// button), plus an "Everyone" row when there are several guests, and the
// controller order (which player each controller is) where Steam allows
// changing it. Clicking a toggle grants or revokes that peripheral. Global
// Ctrl+Alt hotkeys do the same for everyone while the host is in the game.
// Driven by the handler's roster: rebuilt when guests come or go, updated in
// place otherwise.
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

    // Name shown for a guest; sets placeholder when Steam doesn't know it yet.
    static wxString GuestName(const ConnectedGuest& g, bool& placeholder);

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
    // A row of "Controller order": a player slot and the controller in it.
    struct ControllerRow
    {
        int slot;
        wxStaticText* text;
        FlatButton* identify;
        FlatButton* up;
        FlatButton* down;
    };
    // What that row says.
    struct ControllerView
    {
        int slot;
        bool inUse;
        wxString text;
    };

    void Rebuild();
    // Names, join times and toggles of the cards already shown.
    void RefreshRows();
    // The rows "Controller order" should have now: players 1-4 and any later
    // slot with a controller that Steam lists or (for the first few) says is
    // in use. None while there are no guests, or if this Steam doesn't let the
    // app move controllers (then there's no section).
    std::vector<ControllerView> ControllerViews() const;
    // Whose controller it is: "You", or the guest's name.
    wxString ControllerOwner(const RemotePlayPlayer_t& player) const;
    // The "Controller order" section, at the end of the list (from Rebuild).
    void AddControllerSection();
    // Updates the rows shown; false if other rows are needed (then Rebuild).
    bool RefreshControllers(const std::vector<ControllerView>& views);
    void MoveController(int slot, int to);
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
    WindowPlace* m_place;               // remembers where the window was
    ControllerOrder m_controllerOrder;
    std::vector<ControllerRow> m_controllerRows;
    uint32 m_controllerRevision;        // Steam's list when the rows were last updated
    int m_controllerTicks;
};

#endif // CONTROLFRAME_H
