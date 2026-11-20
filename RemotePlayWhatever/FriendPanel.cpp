#include "wxSteamStuff.h"
#include "GameInfo.h"
#include "FriendPanel.h"

FriendPanel::FriendPanel(wxWindow* parent, wxWindowID winID, CSteamID friendID):
    ClickablePanel(parent, winID),
    m_personaNameLabel(nullptr),
    m_personaStateLabel(nullptr),
    m_steamID(friendID),
    m_online(true),
    m_inGame(false),
    m_requestedInfo(false)
{
    wxBoxSizer* rootSizer = new wxBoxSizer( wxHORIZONTAL );

    SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));

    m_personaImg = new wxStaticBitmap( this, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxSize( 32,32 ), 0 );
    rootSizer->Add( m_personaImg, 0, wxALL, 10 );

    wxBoxSizer* textSizer = new wxBoxSizer( wxVERTICAL );

    textSizer->Add( 0, 0, 1, wxEXPAND, 5 );
    m_personaNameLabel = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0 );
    textSizer->Add( m_personaNameLabel, 0, wxLEFT | wxRIGHT | wxEXPAND, 5 );

    // What they're doing, in smaller text; long game names end in "...".
    m_personaStateLabel = new wxStaticText(this, wxID_ANY, "Online", wxDefaultPosition, wxDefaultSize,
                                           wxST_NO_AUTORESIZE | wxST_ELLIPSIZE_END);
    m_personaStateLabel->SetFont(m_personaStateLabel->GetFont().Smaller());
    m_personaStateLabel->SetMinSize(wxSize(FromDIP(20), m_personaStateLabel->GetBestSize().GetHeight()));
    textSizer->Add( m_personaStateLabel, 0, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, 2 );

    textSizer->Add( 0, 0, 1, wxEXPAND, 5 );
    rootSizer->Add( textSizer, 1, wxALL|wxEXPAND, 5 );

    SetSizer( rootSizer );
    UpdatePersona();
    Layout();

    RBindBaseEvents();
}

FriendPanel::~FriendPanel()
{
}

void FriendPanel::UpdatePersona()
{
    if (!m_steamID.IsValid())
    {
        return;
    }

    ISteamFriends015* friends = GClientContext()->SteamFriends();
    EPersonaState state = friends->GetFriendPersonaState(m_steamID);
    m_online = state != k_EPersonaStateOffline && state != k_EPersonaStateInvisible;

    wxString name(friends->GetFriendPersonaName(m_steamID), wxConvUTF8);
    // Until Steam has a friend's profile it reports no name, "[unknown]" or just
    // their SteamID. Ask for the profile once; it arrives in a persona update.
    wxString idText = wxString::Format("%llu", (unsigned long long)m_steamID.ConvertToUint64());
    if ((name.empty() || name == "[unknown]" || name == idText) && !m_requestedInfo)
    {
        m_requestedInfo = true;
        friends->RequestUserInformation(m_steamID, false);
    }
    if (!name.empty())
    {
        m_personaNameLabel->SetLabel(name);
    }

    int av = friends->GetSmallFriendAvatar(m_steamID);
    if (av > 0)
    {
        wxBitmap avatar = SteamImageToBitmap(av);
        if (avatar.IsOk())
        {
            m_avatar = avatar;
        }
    }
    if (m_avatar.IsOk())
    {
        m_personaImg->SetBitmap(m_online ? m_avatar : m_avatar.ConvertToDisabled());
    }

    m_personaNameLabel->SetForegroundColour(NormalForeground());
    m_personaNameLabel->Refresh();
    UpdateStatusLine();
}

void FriendPanel::UpdateStatusLine()
{
    if (!m_steamID.IsValid())
    {
        return;
    }

    ISteamFriends015* friends = GClientContext()->SteamFriends();
    wxString status;
    m_inGame = false;
    if (m_online)
    {
        FriendGameInfo_t game;
        if (friends->GetFriendGamePlayed(m_steamID, &game) && game.m_gameID.IsValid())
        {
            m_inGame = true;
            const wxString name = GetFriendGameName(game.m_gameID);
            if (!name.empty())
            {
                status = "Playing " + name;
            }
            else
            {
                status = game.m_gameID.IsShortcut() ? "Playing a non-Steam game" : "In a game";
            }
        }
        else
        {
            switch (friends->GetFriendPersonaState(m_steamID))
            {
            case k_EPersonaStateBusy:           status = "Busy"; break;
            case k_EPersonaStateAway:
            case k_EPersonaStateSnooze:         status = "Away"; break;
            case k_EPersonaStateLookingToTrade: status = "Looking to trade"; break;
            case k_EPersonaStateLookingToPlay:  status = "Looking to play"; break;
            default:                            status = "Online"; break;
            }
        }
    }

    bool relayout = false;
    if (m_personaStateLabel->GetLabel() != status)
    {
        m_personaStateLabel->SetLabel(status);
        m_personaStateLabel->SetToolTip(status);
        relayout = true;
    }
    if (m_personaStateLabel->IsShown() != !status.empty())
    {
        m_personaStateLabel->Show(!status.empty());
        relayout = true;
    }
    m_personaStateLabel->SetForegroundColour(StatusForeground());
    m_personaStateLabel->Refresh();
    if (relayout)
    {
        Layout();
    }
}

wxColour FriendPanel::NormalForeground() const
{
    return wxSystemSettings::GetColour(m_online ? wxSYS_COLOUR_WINDOWTEXT : wxSYS_COLOUR_GRAYTEXT);
}

wxColour FriendPanel::StatusForeground() const
{
    // In a game in green, like Steam's friends list; anything else in grey.
    return m_inGame ? wxColour(60, 125, 30) : wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT);
}

void FriendPanel::RestoreForeground()
{
    m_personaNameLabel->SetForegroundColour(NormalForeground());
    m_personaStateLabel->SetForegroundColour(StatusForeground());
}

CSteamID FriendPanel::GetSteamID()
{
    return m_steamID;
}

wxString FriendPanel::GetDisplayPersonaName()
{
    return m_personaNameLabel->GetLabel();
}

wxString FriendPanel::GetDisplayPersonaState()
{
    return m_personaStateLabel->GetLabel();
}

void FriendPanel::SetDisplayPersonaName(const wxString &name)
{
    m_personaNameLabel->SetLabel(name);
}

void FriendPanel::SetDisaplayPersonaState(const wxString &state)
{
    m_personaStateLabel->SetLabel(state);
}

