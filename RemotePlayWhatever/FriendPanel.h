#ifndef FRIENDPANEL_H
#define FRIENDPANEL_H
#include "ClickablePanel.h"
#include "wxSteamStuff.h"

class FriendPanel : public ClickablePanel
{
public:
    FriendPanel(wxWindow* parent, wxWindowID winID, CSteamID friendID);
    ~FriendPanel();

    CSteamID GetSteamID();

    wxString GetDisplayPersonaName();
    wxString GetDisplayPersonaState();

    void SetDisplayPersonaName(const wxString& name);
    void SetDisaplayPersonaState(const wxString& state);

    // Re-read the friend's name, picture and online state from Steam.
    void UpdatePersona();
    // Re-read only what they're doing: "Online", "Away", "Playing <game>"...
    void UpdateStatusLine();
    bool IsOnline() const { return m_online; }

protected:
    // Offline friends are greyed out, like in Steam's friends list.
    virtual wxColour NormalForeground() const override;
    virtual void RestoreForeground() override;

private:
    wxColour StatusForeground() const;

    wxStaticBitmap* m_personaImg;
    wxStaticText* m_personaNameLabel;
    wxStaticText* m_personaStateLabel;

    CSteamID m_steamID;
    wxBitmap m_avatar;
    bool m_online;
    bool m_inGame;
    bool m_requestedInfo;
};

#endif // FRIENDPANEL_H
