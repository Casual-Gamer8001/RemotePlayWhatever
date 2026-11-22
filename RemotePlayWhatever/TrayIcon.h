#ifndef TRAYICON_H
#define TRAYICON_H
#include <wx/bmpbndl.h>
#include <wx/taskbar.h>

class FriendsListFrame;
class ControlFrame;
class RemotePlayInviteHandler;

// Icon next to the clock. Clicking it brings the main window back (it hides
// there when minimized); right-click offers the session actions. The tooltip
// shows the same status as the main window, and a green dot on the icon says
// a session is on.
class RpwTrayIcon : public wxTaskBarIcon
{
public:
    RpwTrayIcon(FriendsListFrame* main, ControlFrame* controls, RemotePlayInviteHandler* handler);

    void SetStatus(const wxString& status);
    // While a Remote Play session is on, the icon has a green dot.
    void SetSharing(bool sharing);
    // The first time the window hides here, say where it went (new tray
    // icons start out under the ^ on Windows 11).
    void ShowHiddenHintOnce();
    // Say a new version is available (once per build), with a line about
    // what's new in it if there is one.
    void ShowUpdateBalloon(long build, const wxString& whatsNew = wxString());
    // A notification by the clock (Windows), e.g. answering a shortcut used
    // in a game.
    void Notify(const wxString& title, const wxString& text, bool warning = false);

protected:
    virtual wxMenu* CreatePopupMenu() override;

private:
    void OnLeftClick(wxTaskBarIconEvent& event);
    void OnMenu(wxCommandEvent& event);
    void ShowIcon();

    FriendsListFrame* m_main;
    ControlFrame* m_controls;
    RemotePlayInviteHandler* m_handler;
    wxBitmapBundle m_icon;
    wxBitmapBundle m_sharingIcon;   // made the first time it's needed
    wxString m_tip;
    bool m_sharing;
};

#endif // TRAYICON_H
