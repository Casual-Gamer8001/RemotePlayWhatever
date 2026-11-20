#ifndef FRIENDPANEL_H
#define FRIENDPANEL_H
#include <wx/wx.h>
#include "wxSteamStuff.h"

class FlatButton;

// Sent up to the friends list when a friend's Invite button is clicked; the
// event object is the FriendPanel.
wxDECLARE_EVENT(EVT_FRIEND_INVITE, wxCommandEvent);

// One friend in the list: a round avatar with a status dot, their name, what
// they're doing, and an Invite button. The row draws its own picture and
// text (only the button is a separate control), so a long list stays light.
class FriendPanel : public wxPanel
{
public:
    // What the Invite button offers for this friend right now.
    enum InviteState
    {
        InviteReady,        // "Invite"
        InviteNoGame,       // "Invite", greyed out: no game running (a click says so)
        InviteSending,      // "Sending..."
        InviteSent,         // "Sent": waiting for them to join
        InviteJoined,       // "Joined": they're in the session
    };

    FriendPanel(wxWindow* parent, CSteamID friendID);

    CSteamID GetSteamID() const { return m_steamID; }
    wxString GetDisplayPersonaName() const { return m_name; }
    // The line under the name: what they're doing, or how their invite is going.
    wxString GetDisplayPersonaState() const { return m_shown; }

    // Re-read the friend's name, picture and online state from Steam.
    void UpdatePersona();
    // Re-read only what they're doing: "Online", "Away", "Playing <game>"...
    void UpdateStatusLine();
    bool IsOnline() const { return m_online; }

    void SetInviteState(InviteState state);
    InviteState GetInviteState() const { return m_inviteState; }

private:
    void ApplyInviteState();
    void ShowStatus();
    void UpdateHover();
    void OnPaint(wxPaintEvent& event);
    wxColour DotColour() const;
    wxColour StatusColour() const;

    FlatButton* m_invite;
    CSteamID m_steamID;
    wxString m_name;
    wxString m_status;      // from Steam: "Online", "Playing <game>"...
    wxString m_shown;       // what's under the name
    wxString m_tooltip;
    wxImage m_picture;
    wxBitmap m_round;
    bool m_mediumPicture;
    EPersonaState m_personaState;
    bool m_online;
    bool m_inGame;
    bool m_requestedInfo;
    bool m_hover;
    InviteState m_inviteState;
};

#endif // FRIENDPANEL_H
