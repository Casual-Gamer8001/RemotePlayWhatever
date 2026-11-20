#include "wxSteamStuff.h"
#include <algorithm>
#include <wx/dcbuffer.h>
#include "GameInfo.h"
#include "UiWidgets.h"
#include "FriendPanel.h"

wxDEFINE_EVENT(EVT_FRIEND_INVITE, wxCommandEvent);

namespace
{
    const int kAvatar = 30;         // DIPs
    const int kRowHeight = 42;
}

FriendPanel::FriendPanel(wxWindow* parent, CSteamID friendID) :
    wxPanel(parent, wxID_ANY),
    m_invite(nullptr),
    m_steamID(friendID),
    m_mediumPicture(false),
    m_personaState(k_EPersonaStateOffline),
    m_online(false),
    m_inGame(false),
    m_requestedInfo(false),
    m_hover(false),
    m_inviteState(InviteReady)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    Ui::SetColours(this, &Ui::kWindow);

    m_invite = new FlatButton(this, "Invite", FlatButton::Secondary, 0, true);
    // Wide enough for "Sending...", so the name beside it doesn't move.
    m_invite->SetMinSize(wxSize(FromDIP(76), -1));
    m_invite->Bind(wxEVT_BUTTON, [this](wxCommandEvent&)
    {
        wxCommandEvent invite(EVT_FRIEND_INVITE, GetId());
        invite.SetEventObject(this);
        ProcessWindowEvent(invite);
    });

    // The avatar and the text are drawn in the space left of the button.
    wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(FromDIP(12 + kAvatar + 10), FromDIP(kRowHeight));
    sizer->AddStretchSpacer();
    sizer->Add(m_invite, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(12));
    SetSizer(sizer);

    // The row is highlighted while the mouse is over it or its button.
    auto hover = [this](wxMouseEvent& event)
    {
        event.Skip();
        UpdateHover();
    };
    Bind(wxEVT_ENTER_WINDOW, hover);
    Bind(wxEVT_LEAVE_WINDOW, hover);
    m_invite->Bind(wxEVT_ENTER_WINDOW, hover);
    m_invite->Bind(wxEVT_LEAVE_WINDOW, hover);
    Bind(wxEVT_PAINT, &FriendPanel::OnPaint, this);
    Bind(wxEVT_SIZE, [this](wxSizeEvent& event)
    {
        Refresh();
        event.Skip();
    });

    UpdatePersona();
}

void FriendPanel::UpdatePersona()
{
    if (!m_steamID.IsValid())
    {
        return;
    }

    ISteamFriends015* friends = GClientContext()->SteamFriends();
    m_personaState = friends->GetFriendPersonaState(m_steamID);
    m_online = m_personaState != k_EPersonaStateOffline && m_personaState != k_EPersonaStateInvisible;

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
        m_name = name;
    }

    bool medium = false;
    wxImage picture = SteamAvatarImage(m_steamID, &medium);
    if (picture.IsOk())
    {
        m_picture = picture;
        m_mediumPicture = medium;
    }
    m_round = wxNullBitmap;     // online or not changes how it's drawn
    UpdateStatusLine();
    ApplyInviteState();
}

void FriendPanel::UpdateStatusLine()
{
    if (!m_steamID.IsValid())
    {
        return;
    }

    ISteamFriends015* friends = GClientContext()->SteamFriends();
    // The sharper avatar may have finished loading since.
    if (!m_mediumPicture)
    {
        const int medium = friends->GetMediumFriendAvatar(m_steamID);
        if (medium > 0)
        {
            wxImage picture = SteamImageToImage(medium);
            if (picture.IsOk())
            {
                m_picture = picture;
                m_mediumPicture = true;
                m_round = wxNullBitmap;
            }
        }
    }

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
    m_status = status;
    ShowStatus();
}

void FriendPanel::SetInviteState(InviteState state)
{
    if (state != m_inviteState)
    {
        m_inviteState = state;
        ApplyInviteState();
    }
}

void FriendPanel::ApplyInviteState()
{
    wxString label = "Invite";
    wxString tip;
    bool enabled = true;
    bool muted = false;
    switch (m_inviteState)
    {
    case InviteReady:
        tip = m_online ? "Send " + m_name + " an invite in Steam chat."
                       : m_name + " appears offline (maybe invisible). The invite waits for them in Steam chat.";
        break;
    case InviteNoGame:
        muted = true;
        tip = "Start a game through Steam first, then invite.";
        break;
    case InviteSending:
        label = "Sending...";
        enabled = false;
        break;
    case InviteSent:
        label = "Sent";
        enabled = false;
        break;
    case InviteJoined:
        label = "Joined";
        enabled = false;
        break;
    }
    m_invite->SetLabel(label);
    m_invite->SetMuted(muted);
    m_invite->Enable(enabled);
    m_invite->SetKind(m_inviteState == InviteReady && m_hover ? FlatButton::Primary : FlatButton::Secondary);
    if (tip.empty())
    {
        m_invite->UnsetToolTip();
    }
    else if (m_invite->GetToolTipText() != tip)
    {
        m_invite->SetToolTip(tip);
    }
    ShowStatus();
}

void FriendPanel::ShowStatus()
{
    wxString shown = m_status;
    switch (m_inviteState)
    {
    case InviteSending: shown = "Sending an invite..."; break;
    case InviteSent:    shown = "Invited" + Ui::Separator() + "waiting to join"; break;
    case InviteJoined:  shown = "In your session"; break;
    default:            break;
    }
    m_shown = shown;
    // The row's window text, for automation (the name and status are drawn).
    const wxString label = m_name + "\n" + m_shown;
    if (GetLabel() != label)
    {
        SetLabel(label);
    }
    Refresh();
}

void FriendPanel::UpdateHover()
{
    // Over the row or its button; parts scrolled out of view don't count.
    wxRect visible = GetScreenRect();
    if (GetParent())
    {
        visible.Intersect(GetParent()->GetScreenRect());
    }
    const bool hover = IsShownOnScreen() && visible.Contains(wxGetMousePosition());
    if (hover == m_hover)
    {
        return;
    }
    m_hover = hover;
    SetBackgroundColour(hover ? Ui::kHover : Ui::kWindow);
    m_invite->SetKind(m_inviteState == InviteReady && hover ? FlatButton::Primary : FlatButton::Secondary);
    Refresh();
    m_invite->Refresh();
}

wxColour FriendPanel::DotColour() const
{
    if (!m_online)
    {
        return wxNullColour;
    }
    if (m_inGame)
    {
        return Ui::kDotInGame;
    }
    switch (m_personaState)
    {
    case k_EPersonaStateAway:
    case k_EPersonaStateSnooze: return Ui::kDotAway;
    case k_EPersonaStateBusy:   return Ui::kDotBusy;
    default:                    return Ui::kDotOnline;
    }
}

wxColour FriendPanel::StatusColour() const
{
    if (m_inviteState == InviteJoined)
    {
        return Ui::kGoodText;
    }
    if (m_inviteState == InviteSent || m_inviteState == InviteSending)
    {
        return Ui::kTextSecondary;
    }
    // In a game in green, like Steam's friends list; anything else in grey.
    if (m_inGame)
    {
        return Ui::kPlaying;
    }
    return m_online ? Ui::kTextSecondary : Ui::kTextMuted;
}

void FriendPanel::OnPaint(wxPaintEvent&)
{
    // The highlight colour comes from the palette, which changes with the theme.
    const wxColour want = m_hover ? Ui::kHover : Ui::kWindow;
    if (GetBackgroundColour() != want)
    {
        SetBackgroundColour(want);
        m_invite->Refresh();
    }
    wxAutoBufferedPaintDC dc(this);
    const wxColour background = GetBackgroundColour();
    dc.SetBackground(wxBrush(background));
    dc.Clear();
    const wxSize size = GetClientSize();

    const int avatar = FromDIP(kAvatar);
    const wxRect avatarRect(FromDIP(12), (size.y - avatar) / 2, avatar, avatar);
    if (m_picture.IsOk() && (!m_round.IsOk() || m_round.GetWidth() != avatar))
    {
        m_round = Ui::RoundPicture(m_picture, avatar, !m_online);
    }
    dc.SetFont(GetFont());
    Ui::DrawAvatar(dc, avatarRect, m_picture.IsOk() ? m_round : wxNullBitmap, Ui::Initials(m_name),
                   Ui::PlaceholderColour(m_steamID.ConvertToUint64()), DotColour(), background);

    const int textX = avatarRect.GetRight() + 1 + FromDIP(10);
    const int textRight = (m_invite->IsShown() ? m_invite->GetPosition().x : size.x) - FromDIP(8);
    const int room = std::max(0, textRight - textX);
    const wxFont nameFont = Ui::SemiBold(GetFont());
    const wxFont statusFont = GetFont().Scaled(0.92f);
    dc.SetFont(statusFont);
    const int statusHeight = dc.GetCharHeight();
    dc.SetFont(nameFont);
    const int nameHeight = dc.GetCharHeight();
    const int gap = FromDIP(1);
    const int y = (size.y - nameHeight - (m_shown.empty() ? 0 : gap + statusHeight)) / 2;

    dc.SetBackgroundMode(wxBRUSHSTYLE_TRANSPARENT);
    dc.SetTextForeground(m_online ? Ui::kText : Ui::kTextMuted);
    const wxString name = wxControl::Ellipsize(m_name, dc, wxELLIPSIZE_END, room);
    dc.DrawText(name, textX, y);
    wxString status;
    if (!m_shown.empty())
    {
        dc.SetFont(statusFont);
        dc.SetTextForeground(StatusColour());
        status = wxControl::Ellipsize(m_shown, dc, wxELLIPSIZE_END, room);
        dc.DrawText(status, textX, y + nameHeight + gap);
    }

    // The whole name and status in a tooltip, when either is cut short.
    const wxString tip = name != m_name || status != m_shown ? m_name + (m_shown.empty() ? wxString() : "\n" + m_shown)
                                                             : wxString();
    if (tip != m_tooltip)
    {
        m_tooltip = tip;
        if (tip.empty())
        {
            UnsetToolTip();
        }
        else
        {
            SetToolTip(tip);
        }
    }
}
