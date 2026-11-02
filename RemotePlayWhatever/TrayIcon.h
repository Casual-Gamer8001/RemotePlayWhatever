#ifndef TRAYICON_H
#define TRAYICON_H
#include <wx/bmpbndl.h>
#include <wx/taskbar.h>

class FriendsListFrame;
class ControlFrame;
class RemotePlayInviteHandler;

// Icon next to the clock. Clicking it brings the main window back (it hides
// there when minimized); right-click offers the session actions. The tooltip
// shows the same status as the main window.
class RpwTrayIcon : public wxTaskBarIcon
{
public:
    RpwTrayIcon(FriendsListFrame* main, ControlFrame* controls, RemotePlayInviteHandler* handler);

    void SetStatus(const wxString& status);

protected:
    virtual wxMenu* CreatePopupMenu() override;

private:
    void OnLeftClick(wxTaskBarIconEvent& event);
    void OnMenu(wxCommandEvent& event);

    FriendsListFrame* m_main;
    ControlFrame* m_controls;
    RemotePlayInviteHandler* m_handler;
    wxBitmapBundle m_icon;
};

#endif // TRAYICON_H
