#include <wx/wx.h>
#include <wx/clipbrd.h>
#include <wx/srchctrl.h>
#include <wx/time.h>
#include <algorithm>
#include <cstring>
#include <vector>
#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>
#endif
#include "Updater.h"
#include "FriendsListFrame.h"
#include "FriendPanel.h"
#include "ControlFrame.h"
#include "GameInfo.h"
#include "TrayIcon.h"
#include "UiHelpers.h"

static const wxColour kReadyGreen(30, 130, 50);
static const wxColour kNoticeOrange(190, 100, 0);

FriendsListFrame::FriendsListFrame(RemotePlayInviteHandler* handler):
    wxFrame(NULL, wxID_ANY, _("Remote Play Whatever"), wxPoint(50, 50), wxSize(275, 650)),
    m_recentSizer(nullptr),
    m_onlineSizer(nullptr),
    m_offlineSizer(nullptr),
    m_recentLabel(nullptr),
    m_onlineLabel(nullptr),
    m_offlineLabel(nullptr),
    m_tray(nullptr),
    m_search(nullptr),
    m_statusPanel(nullptr),
    m_status(nullptr),
    m_copyLinkButton(nullptr),
    m_endButton(nullptr),
    m_waitingSizer(nullptr),
    m_updateButton(nullptr),
    m_updater(nullptr),
    m_noticeUntil(0),
    m_copiedUntil(0),
    m_tick(this),
    m_statusRefreshTicks(0),
    m_askingGroupFallback(false),
    m_inviteQueued(false),
    m_rosterSeen(0),
    m_remoteInviteResultCb(this, &FriendsListFrame::OnRemotePlayInviteResult),
    m_personaStateCb(this, &FriendsListFrame::OnPersonaStateChange)
{
    SetSizeHints( wxDefaultSize, wxDefaultSize );

    SetMinSize(wxSize(275, 300));
    SetMaxSize(wxSize(350, -1));
    SetIcons(AppIcons());

    m_pRemoteInvite = handler;

    wxBoxSizer* frameSizer = new wxBoxSizer( wxVERTICAL );

    // What will be shared, so it's clear before inviting anyone.
    wxPanel* statusPanel = new wxPanel(this, wxID_ANY);
    m_statusPanel = statusPanel;
    statusPanel->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
    wxBoxSizer* statusSizer = new wxBoxSizer(wxVERTICAL);
    m_status = new wxStaticText(statusPanel, wxID_ANY, wxEmptyString);
    m_status->SetFont(m_status->GetFont().Bold());
    statusSizer->Add(m_status, 0, wxALL | wxEXPAND, 10);
    wxBoxSizer* sessionButtons = new wxBoxSizer(wxHORIZONTAL);
    m_copyLinkButton = new wxButton(statusPanel, wxID_ANY, "Copy guest link");
    m_copyLinkButton->SetToolTip("Copy this session's guest link again.");
    m_copyLinkButton->Bind(wxEVT_BUTTON, &FriendsListFrame::OnCopyLink, this);
    m_copyLinkButton->Hide();
    sessionButtons->Add(m_copyLinkButton, 1, wxRIGHT, 6);
    m_endButton = new wxButton(statusPanel, wxID_ANY, "End session");
    m_endButton->SetToolTip("Disconnect everyone, cancel unused invites and guest links, and stop sharing your screen.");
    m_endButton->Bind(wxEVT_BUTTON, &FriendsListFrame::OnEndSession, this);
    m_endButton->Hide();
    sessionButtons->Add(m_endButton, 1);
    statusSizer->Add(sessionButtons, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 10);
    // Invites nobody has used yet, each with a Cancel button (RefreshWaitingInvites).
    m_waitingSizer = new wxBoxSizer(wxVERTICAL);
    statusSizer->Add(m_waitingSizer, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 10);
    m_updateButton = new wxButton(statusPanel, wxID_ANY, "Update");
    m_updateButton->SetToolTip("Download the new version from GitHub and restart Remote Play Whatever with it.");
    m_updateButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { StartUpdate(); });
    m_updateButton->Hide();
    statusSizer->Add(m_updateButton, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 10);
    statusPanel->SetSizer(statusSizer);
    frameSizer->Add(statusPanel, 0, wxEXPAND | wxBOTTOM, 1);

    m_friendsListContainer = new wxScrolledWindow( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL );
    m_friendsListContainer->SetScrollRate( 5, 5 );
    m_friendsListSizer = new wxBoxSizer( wxVERTICAL );

    BtnPanel* guestBtn = new BtnPanel(m_friendsListContainer, rpwID_INVITE_GUEST, "Create guest invite link");
    m_friendsListSizer->Add(guestBtn, 0, wxALL | wxEXPAND, 0);

    wxStaticText* flLabel = new wxStaticText(m_friendsListContainer, wxID_ANY, "or invite a friend", wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL);
    m_friendsListSizer->Add(flLabel, 0, wxALL | wxEXPAND, 10);

    m_search = new wxSearchCtrl(m_friendsListContainer, wxID_ANY);
    m_search->SetDescriptiveText("Search friends");
    m_search->ShowCancelButton(true);
    m_friendsListSizer->Add(m_search, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 8);
    m_search->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { ApplyFilter(); });
    m_search->Bind(wxEVT_SEARCHCTRL_CANCEL_BTN, [this](wxCommandEvent&) { m_search->Clear(); ApplyFilter(); });

    // Friends who've joined before, then online friends, then the ones who
    // appear offline (they may just be invisible, and an invite link reaches
    // them in Steam chat anyway).
    wxColour headerColour = wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT);
    m_recentLabel = new wxStaticText(m_friendsListContainer, wxID_ANY, "Played with recently");
    m_recentLabel->SetForegroundColour(headerColour);
    m_friendsListSizer->Add(m_recentLabel, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
    m_recentSizer = new wxBoxSizer(wxVERTICAL);
    m_friendsListSizer->Add(m_recentSizer, 0, wxEXPAND | wxBOTTOM, 8);
    m_onlineLabel = new wxStaticText(m_friendsListContainer, wxID_ANY, "Online");
    m_onlineLabel->SetForegroundColour(headerColour);
    m_friendsListSizer->Add(m_onlineLabel, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
    m_onlineSizer = new wxBoxSizer(wxVERTICAL);
    m_friendsListSizer->Add(m_onlineSizer, 0, wxEXPAND);
    m_offlineLabel = new wxStaticText(m_friendsListContainer, wxID_ANY, "Offline");
    m_offlineLabel->SetForegroundColour(headerColour);
    m_friendsListSizer->Add(m_offlineLabel, 0, wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, 8);
    m_offlineSizer = new wxBoxSizer(wxVERTICAL);
    m_friendsListSizer->Add(m_offlineSizer, 0, wxEXPAND);

    m_joinTimes = LoadGuestJoinTimes();
    m_rosterSeen = m_pRemoteInvite->RosterRevision();
    int cFriends = GClientContext()->SteamFriends()->GetFriendCount(k_EFriendFlagImmediate);
    for(int i = 0; i < cFriends; ++i)
    {
        AddFreiendPanel(GClientContext()->SteamFriends()->GetFriendByIndex(i, k_EFriendFlagImmediate));
    }

    m_friendsListContainer->SetSizer( m_friendsListSizer );
    ArrangeFriendPanels();
    m_friendsListContainer->Layout();
    m_friendsListSizer->Fit( m_friendsListContainer );
    frameSizer->Add( m_friendsListContainer, 1, wxEXPAND | wxALL, 0 );

    SetSizer( frameSizer );
    Layout();

    Bind(BTN_PANEL_LEFT_CLICK, &FriendsListFrame::OnFriendPanelClick, this, rpwID_INVITE_FRIEND);
    Bind(BTN_PANEL_LEFT_CLICK, &FriendsListFrame::OnGuestPanelClick, this, rpwID_INVITE_GUEST);
    Bind(wxEVT_TIMER, &FriendsListFrame::OnTick, this);
    Bind(wxEVT_CLOSE_WINDOW, &FriendsListFrame::OnClose, this);
    Bind(wxEVT_ICONIZE, &FriendsListFrame::OnIconize, this);
    m_tick.Start(500);
    UpdateStatus();

    // Never deleted: a download Windows is still finishing when the app closes
    // may report back to it, so it stays until the process ends (see OnClose).
    m_updater = new AppUpdater([this]() { OnUpdaterChange(); },
                               [this]() { return !m_pRemoteInvite->IsSessionActive(); });
    m_updater->StartAutomaticChecks();
}

void FriendsListFrame::OnClose(wxCloseEvent& event)
{
    // Closing the main window quits the app. Take the (usually hidden) control
    // window and the tray icon with it; otherwise they keep the app running
    // invisibly. Only our own windows are destroyed here: the tray icon owns
    // and destroys its own hidden window.
    m_tick.Stop();
    m_updater->Shutdown();
    std::vector<wxWindow*> controls;
    for (wxWindowList::compatibility_iterator node = wxTopLevelWindows.GetFirst(); node; node = node->GetNext())
    {
        if (dynamic_cast<ControlFrame*>(node->GetData()))
        {
            controls.push_back(node->GetData());
        }
    }
    for (size_t i = 0; i < controls.size(); ++i)
    {
        controls[i]->Destroy();
    }
    if (m_tray)
    {
        m_tray->RemoveIcon();
        m_tray->Destroy();
        m_tray = nullptr;
    }
    event.Skip();
}

void FriendsListFrame::OnIconize(wxIconizeEvent& event)
{
    // With a tray icon, minimizing tucks the window away next to the clock.
    if (event.IsIconized() && m_tray && m_tray->IsIconInstalled())
    {
        Hide();
        m_tray->ShowHiddenHintOnce();
    }
    event.Skip();
}

void FriendsListFrame::ShowFromTray()
{
    // Show first, then restore: restoring a hidden window only updates wx's
    // idea of its state and leaves it minimized once it's shown.
    if (!IsShown())
    {
        Show();
    }
    if (IsIconized())
    {
        Iconize(false);
    }
    Raise();
#ifdef __WXMSW__
    ::SetForegroundWindow(GetHWND());
#endif
}

void FriendsListFrame::SetTray(RpwTrayIcon* tray)
{
    m_tray = tray;
    if (m_tray)
    {
        m_tray->SetStatus(m_statusShown);
    }
}

#ifdef __WXMSW__
WXLRESULT FriendsListFrame::MSWWindowProc(WXUINT msg, WXWPARAM wParam, WXLPARAM lParam)
{
    static const UINT showMessage = ::RegisterWindowMessageW(L"RemotePlayWhatever-Show");
    if (msg == showMessage)
    {
        ShowFromTray();
        return 0;
    }
    return wxFrame::MSWWindowProc(msg, wParam, lParam);
}
#endif

void FriendsListFrame::UpdateStatus()
{
    wxString notice = wxString::FromUTF8(m_pRemoteInvite->TakeNotice().c_str());
    if (!notice.empty())
    {
        m_notice = notice;
        m_noticeUntil = wxGetLocalTimeMillis() + 15000;
    }

    wxString text;
    wxColour colour = wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT);
    bool session = m_pRemoteInvite->IsSessionActive();

    if (!m_notice.empty() && wxGetLocalTimeMillis() < m_noticeUntil)
    {
        text = m_notice;
        colour = kNoticeOrange;
    }
    else if (!GClientContext()->SteamUser()->BLoggedOn())
    {
        text = "Steam isn't signed in.";
    }
    else
    {
        m_notice.clear();
        CGameID game = GetRunningGameID();
        bool nonSteam = game.IsValid() &&
                        !(game.IsSteamApp() && game.AppID() != m_pRemoteInvite->GetNonSteamAppID());
        wxString name = game.IsValid() ?
            GetGameName(game, GClientContext()->SteamUser()->GetSteamID().GetAccountID()) : wxString();
        if (name.empty())
        {
            name = nonSteam ? "a non-Steam game" : "your game";
        }

        if (session)
        {
            unsigned guests = (unsigned)m_pRemoteInvite->Guests().size();
            text = "Sharing " + name + ": ";
            text += guests == 0 ? wxString("waiting for guests to join.") :
                    guests == 1 ? wxString("1 guest connected.") :
                    wxString::Format("%u guests connected.", guests);
            if (m_pRemoteInvite->IsSharingDesktop())
            {
                text += " Guests can see your whole screen.";
            }
            colour = kReadyGreen;
        }
        else if (!game.IsValid())
        {
            text = "No game running. Start a game through Steam, then invite someone.";
        }
        else
        {
            text = "Ready to share: " + name;
            if (nonSteam)
            {
                text += " (non-Steam game, so your whole screen will be shared)";
            }
            colour = kReadyGreen;
        }
    }

    bool relayout = false;
    if (text != m_statusShown)
    {
        m_statusShown = text;
        m_status->SetLabel(text);
        m_status->SetForegroundColour(colour);
        m_status->Wrap(FromDIP(235));
        relayout = true;
        if (m_tray)
        {
            m_tray->SetStatus(text);
        }
    }
    if (m_endButton->IsShown() != session)
    {
        m_endButton->Show(session);
        relayout = true;
    }
    bool hasLink = session && !m_pRemoteInvite->GuestLink().empty();
    if (m_copyLinkButton->IsShown() != hasLink)
    {
        m_copyLinkButton->Show(hasLink);
        relayout = true;
    }
    if (m_copiedUntil != 0 && wxGetLocalTimeMillis() >= m_copiedUntil)
    {
        m_copyLinkButton->SetLabel("Copy guest link");
        m_copiedUntil = 0;
    }
    if (RefreshWaitingInvites())
    {
        relayout = true;
    }
    if (relayout)
    {
        m_status->GetParent()->Layout();
        Layout();
    }
}

void FriendsListFrame::OnCopyLink(wxCommandEvent&)
{
    CopyGuestLink();
}

bool FriendsListFrame::RefreshWaitingInvites()
{
    static const size_t kShown = 4;
    const std::vector<RemotePlayInviteHandler::WaitingInvite> waiting = m_pRemoteInvite->WaitingInvites();
    std::vector<wxString> labels;
    wxString shown;
    for (size_t i = 0; i < waiting.size(); ++i)
    {
        wxString label = "Guest link (not used yet)";
        if (waiting[i].steamID)
        {
            const char* persona = GClientContext()->SteamFriends()->GetFriendPersonaName(CSteamID((uint64)waiting[i].steamID));
            label = "Waiting for " + (persona && *persona ? wxString::FromUTF8(persona) : wxString("a friend"));
        }
        labels.push_back(label);
        shown += wxString::Format("%llu/%u ", (unsigned long long)waiting[i].steamID, (unsigned)waiting[i].guestID) + label + "\n";
    }
    if (shown == m_waitingShown)
    {
        return false;
    }
    m_waitingShown = shown;

    m_waitingSizer->Clear(true);
    for (size_t i = 0; i < waiting.size() && i < kShown; ++i)
    {
        const uint64 id = waiting[i].steamID;
        const uint32 guest = waiting[i].guestID;
        wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
        wxStaticText* text = new wxStaticText(m_statusPanel, wxID_ANY, labels[i], wxDefaultPosition, wxDefaultSize,
                                              wxST_NO_AUTORESIZE | wxST_ELLIPSIZE_END);
        text->SetMinSize(wxSize(FromDIP(40), text->GetBestSize().GetHeight()));
        text->SetToolTip(id ? labels[i] + " to join." : wxString("Nobody has opened this guest link yet."));
        row->Add(text, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        wxButton* cancel = new wxButton(m_statusPanel, wxID_ANY, "Cancel", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
        cancel->SetToolTip(id ? wxString("Cancel this invite, so it no longer works.")
                              : wxString("Make this guest link stop working."));
        cancel->Bind(wxEVT_BUTTON, [this, id, guest](wxCommandEvent&)
        {
            // Cancelling rebuilds these rows, this button included, so do it
            // once the click has been handled.
            CallAfter([this, id, guest]()
            {
                m_pRemoteInvite->CancelWaitingInvite(id, guest);
                UpdateStatus();
            });
        });
        row->Add(cancel, 0, wxALIGN_CENTER_VERTICAL);
        m_waitingSizer->Add(row, 0, wxEXPAND | wxBOTTOM, 4);
    }
    if (waiting.size() > kShown)
    {
        m_waitingSizer->Add(new wxStaticText(m_statusPanel, wxID_ANY,
                                             wxString::Format("and %u more", (unsigned)(waiting.size() - kShown))),
                            0, wxBOTTOM, 4);
    }
    return true;
}

void FriendsListFrame::CopyGuestLink()
{
    wxString link = wxString::FromUTF8(m_pRemoteInvite->GuestLink().c_str());
    if (!link.empty() && wxTheClipboard->Open())
    {
        wxTheClipboard->SetData(new wxTextDataObject(link));
        wxTheClipboard->Flush();
        wxTheClipboard->Close();
        m_copyLinkButton->SetLabel("Copied!");
        m_copiedUntil = wxGetLocalTimeMillis() + 2000;
    }
}

void FriendsListFrame::OnEndSession(wxCommandEvent&)
{
    ConfirmEndSession();
}

void FriendsListFrame::ConfirmEndSession()
{
    int answer = wxMessageBox(
        "Disconnect everyone and stop sharing?\n\nUnused invites and guest links will stop working too.",
        "Remote Play Whatever",
        wxYES_NO | wxICON_QUESTION,
        IsShown() ? this : nullptr
    );
    if (answer == wxYES)
    {
        m_pRemoteInvite->EndSession();
        UpdateStatus();
    }
}

void FriendsListFrame::ShowNotice(const wxString& text)
{
    m_notice = text;
    m_noticeUntil = wxGetLocalTimeMillis() + 15000;
    UpdateStatus();
}

bool FriendsListFrame::UpdatesSupported() const
{
    return AppUpdater::Supported();
}

long FriendsListFrame::AvailableUpdate() const
{
    return m_updater->AvailableBuild();
}

bool FriendsListFrame::UpdateBusy() const
{
    return m_updater->Busy();
}

void FriendsListFrame::CheckForUpdates()
{
    m_updater->Check(true);
}

void FriendsListFrame::StartUpdate()
{
    if (m_updater->AvailableBuild() <= 0 || m_updater->Busy())
    {
        return;
    }
    if (m_pRemoteInvite->IsSessionActive())
    {
        wxMessageBox(
            "Updating restarts Remote Play Whatever, which would end your Remote Play session.\n\n"
            "End the session first, then update.",
            "Remote Play Whatever",
            wxOK | wxICON_INFORMATION,
            IsShown() ? this : nullptr
        );
        return;
    }
    m_updater->Install();
}

void FriendsListFrame::RefreshUpdateButton()
{
    const long build = m_updater->AvailableBuild();
    const AppUpdater::State state = m_updater->GetState();
    const bool working = state == AppUpdater::Downloading || state == AppUpdater::Installing ||
                         state == AppUpdater::Restarting;
    wxString label = wxString::Format("Update to build %ld", build);
    if (state == AppUpdater::Downloading)
    {
        label = "Downloading the update...";
    }
    else if (state == AppUpdater::Installing || state == AppUpdater::Restarting)
    {
        label = "Installing the update...";
    }

    bool relayout = false;
    if (m_updateButton->IsShown() != (build > 0))
    {
        m_updateButton->Show(build > 0);
        relayout = true;
    }
    if (m_updateButton->GetLabel() != label)
    {
        m_updateButton->SetLabel(label);
        relayout = true;
    }
    m_updateButton->Enable(!working);
    if (relayout)
    {
        m_updateButton->GetParent()->Layout();
        Layout();
    }
}

void FriendsListFrame::OnUpdaterChange()
{
    RefreshUpdateButton();

    // Dialogs wait until the updater's own event has been handled.
    const bool manual = m_updater->LastCheckWasManual();
    switch (m_updater->GetState())
    {
    case AppUpdater::Available:
        if (manual)
        {
            CallAfter([this]()
            {
                int answer = wxMessageBox(
                    wxString::Format("Build %ld is available (you have build %ld).\n\n"
                                     "Update now? Remote Play Whatever restarts afterwards.",
                                     m_updater->AvailableBuild(), AppUpdater::CurrentBuild()),
                    "Remote Play Whatever",
                    wxYES_NO | wxICON_QUESTION,
                    IsShown() ? this : nullptr
                );
                if (answer == wxYES)
                {
                    StartUpdate();
                }
            });
        }
        else if (m_tray && !IsShown())
        {
            m_tray->ShowUpdateBalloon(m_updater->AvailableBuild());
        }
        break;
    case AppUpdater::UpToDate:
        if (manual)
        {
            CallAfter([this]()
            {
                wxMessageBox(wxString::Format("You have the latest version (build %ld).", AppUpdater::CurrentBuild()),
                             "Remote Play Whatever", wxOK | wxICON_INFORMATION, IsShown() ? this : nullptr);
            });
        }
        break;
    case AppUpdater::Failed:
        if (manual)
        {
            const wxString error = m_updater->Error();
            CallAfter([this, error]()
            {
                int answer = wxMessageBox(error + "\n\nOpen the download page instead?", "Remote Play Whatever",
                                          wxYES_NO | wxICON_WARNING, IsShown() ? this : nullptr);
                if (answer == wxYES)
                {
                    wxLaunchDefaultBrowser(AppUpdater::ReleasesPage());
                }
            });
        }
        break;
    case AppUpdater::Restarting:
        // The new version has started and waits for this one to close.
        CallAfter([this]() { Close(true); });
        break;
    case AppUpdater::Postponed:
        CallAfter([this]()
        {
            wxMessageBox(
                wxString::Format("A Remote Play session started while the update was downloading, so it wasn't "
                                 "installed.\n\nClick \"Update to build %ld\" again when the session is over.",
                                 m_updater->AvailableBuild()),
                "Remote Play Whatever",
                wxOK | wxICON_INFORMATION,
                IsShown() ? this : nullptr
            );
        });
        break;
    default:
        break;
    }
}

void FriendsListFrame::OnTick(wxTimerEvent&)
{
    UpdateStatus();

    // A friend who joins a session moves up to "Played with recently".
    if (m_pRemoteInvite->RosterRevision() != m_rosterSeen)
    {
        m_rosterSeen = m_pRemoteInvite->RosterRevision();
        std::map<uint64, long> times = LoadGuestJoinTimes();
        if (times != m_joinTimes)
        {
            m_joinTimes = times;
            ArrangeFriendPanels();
        }
    }

    // Every 30 s, refresh what friends are doing: Steam can fill in a game's
    // name a little after the friend started playing it.
    if (++m_statusRefreshTicks >= 60)
    {
        m_statusRefreshTicks = 0;
        for (auto it = m_friendPanels.begin(); it != m_friendPanels.end(); ++it)
        {
            if (it->second->IsOnline())
            {
                it->second->UpdateStatusLine();
            }
        }
    }

    // Timers keep firing while a message box is open, so don't ask twice.
    if (m_pRemoteInvite->HasGroupFailure() && !m_askingGroupFallback)
    {
        m_askingGroupFallback = true;
        int answer = wxMessageBox(
            "Steam didn't set up a Remote Play Together group for this game, so your "
            "guest might only be able to watch, not play.\n\n"
            "Send the invite anyway?\n\n"
            "If they can't play, quit the game, start it again through Steam, and send a new invite.",
            "Remote Play Whatever",
            wxYES_NO | wxICON_WARNING,
            this
        );
        m_pRemoteInvite->ResolveGroupFailure(answer == wxYES);
        m_askingGroupFallback = false;
    }
}

static wxString DescribeLaunchResult(ERemoteClientLaunchResult result)
{
    switch (result)
    {
    case k_ERemoteClientLaunchResultTimeout:
    case k_ERemoteClientLaunchResultStreamTimeout:      return "Steam timed out while starting the session.";
    case k_ERemoteClientLaunchResultOtherGameRunning:   return "Another game is already being shared.";
    case k_ERemoteClientLaunchResultAccessDenied:       return "Steam refused the request.";
    case k_ERemoteClientLaunchResultNetworkError:       return "There was a network error.";
    case k_ERemoteClientLaunchResultScreenLocked:       return "This PC's screen is locked.";
    case k_ERemoteClientLaunchResultUnsupported:        return "Steam says this game can't be shared.";
    case k_ERemoteClientLaunchResultDisabledLocal:      return "Remote Play is turned off in this PC's Steam settings.";
    case k_ERemoteClientLaunchResultDisabledRemote:     return "Remote Play is turned off on your friend's side.";
    case k_ERemoteClientLaunchResultBroadcasting:       return "Steam can't share the game while you're broadcasting.";
    case k_ERemoteClientLaunchResultBusy:               return "Steam is busy with another stream.";
    case k_ERemoteClientLaunchResultDriversNotInstalled:return "Steam's streaming drivers aren't installed.";
    case k_ERemoteClientLaunchResultCanceled:           return "The invite was canceled.";
    default:                                            return "Steam couldn't start the session.";
    }
}

void FriendsListFrame::AddFreiendPanel(CSteamID friendID)
{
    uint64 friendID64 = friendID.ConvertToUint64();
    auto it = m_friendPanels.find(friendID64);
    if(it != m_friendPanels.end())
    {
        (*it).second->UpdatePersona();
        return;
    }
    m_friendPanels[friendID64] = new FriendPanel(m_friendsListContainer, rpwID_INVITE_FRIEND, friendID);
}

void FriendsListFrame::RemoveFriendPanel(CSteamID friendID)
{
    uint64 friendID64 = friendID.ConvertToUint64();
    auto it = m_friendPanels.find(friendID64);
    if(it != m_friendPanels.end())
    {
        FriendPanel* panel = (*it).second;
        m_recentSizer->Detach(panel);
        m_onlineSizer->Detach(panel);
        m_offlineSizer->Detach(panel);
        panel->Destroy();
        m_friendPanels.erase(it);
        m_recentIDs.erase(friendID64);
    }
}

void FriendsListFrame::ArrangeFriendPanels()
{
    // Up to kRecentFriends friends who joined a session before, most recent
    // first; everyone else under Online or Offline, by name.
    static const size_t kRecentFriends = 8;
    std::vector<std::pair<long, FriendPanel*>> played;
    std::vector<FriendPanel*> online, offline;
    for (auto it = m_friendPanels.begin(); it != m_friendPanels.end(); ++it)
    {
        auto joined = m_joinTimes.find(it->first);
        if (joined != m_joinTimes.end())
        {
            played.push_back(std::make_pair(joined->second, it->second));
        }
    }
    std::sort(played.begin(), played.end(), [](const std::pair<long, FriendPanel*>& a,
                                               const std::pair<long, FriendPanel*>& b)
    {
        return a.first > b.first;
    });
    if (played.size() > kRecentFriends)
    {
        played.resize(kRecentFriends);
    }
    m_recentIDs.clear();
    for (size_t i = 0; i < played.size(); ++i)
    {
        m_recentIDs.insert(played[i].second->GetSteamID().ConvertToUint64());
    }

    for (auto it = m_friendPanels.begin(); it != m_friendPanels.end(); ++it)
    {
        if (m_recentIDs.count(it->first) == 0)
        {
            (it->second->IsOnline() ? online : offline).push_back(it->second);
        }
    }
    auto byName = [](FriendPanel* a, FriendPanel* b)
    {
        return a->GetDisplayPersonaName().CmpNoCase(b->GetDisplayPersonaName()) < 0;
    };
    std::sort(online.begin(), online.end(), byName);
    std::sort(offline.begin(), offline.end(), byName);

    // Detach only; the panels themselves stay.
    m_recentSizer->Clear(false);
    m_onlineSizer->Clear(false);
    m_offlineSizer->Clear(false);
    for (size_t i = 0; i < played.size(); ++i)
    {
        m_recentSizer->Add(played[i].second, 0, wxBOTTOM | wxEXPAND, 1);
    }
    for (size_t i = 0; i < online.size(); ++i)
    {
        m_onlineSizer->Add(online[i], 0, wxBOTTOM | wxEXPAND, 1);
    }
    for (size_t i = 0; i < offline.size(); ++i)
    {
        m_offlineSizer->Add(offline[i], 0, wxBOTTOM | wxEXPAND, 1);
    }
    ApplyFilter();
}

void FriendsListFrame::OnPersonaStateChange(PersonaStateChange_t* personaStateCb)
{
    if(personaStateCb->m_ulSteamID == GClientContext()->SteamUser()->GetSteamID())
    {
        return;
    }

    CSteamID steamID(personaStateCb->m_ulSteamID);
    bool known = m_friendPanels.count(steamID.ConvertToUint64()) != 0;
    if(GClientContext()->SteamFriends()->GetFriendRelationship(steamID) != k_EFriendRelationshipFriend)
    {
        // Unfriended (or someone who isn't a friend, like a link guest).
        if(known)
        {
            RemoveFriendPanel(steamID);
            ApplyFilter();
        }
        return;
    }

    // Playing a different game, going away and so on don't change the list,
    // only that friend's status line.
    const int listChanges = k_EPersonaChangeComeOnline | k_EPersonaChangeGoneOffline | k_EPersonaChangeName |
                            k_EPersonaChangeNameFirstSet | k_EPersonaChangeAvatar | k_EPersonaChangeRelationshipChanged;
    if(known && !(personaStateCb->m_nChangeFlags & listChanges))
    {
        m_friendPanels[steamID.ConvertToUint64()]->UpdateStatusLine();
        return;
    }

    AddFreiendPanel(steamID);
    ArrangeFriendPanels();
}

void FriendsListFrame::ApplyFilter()
{
    wxString needle = m_search ? m_search->GetValue().Lower().Trim().Trim(false) : wxString();
    int recent = 0, online = 0, offline = 0;
    for (auto it = m_friendPanels.begin(); it != m_friendPanels.end(); ++it)
    {
        FriendPanel* panel = it->second;
        // A search finds names and what friends are doing ("playing ...").
        bool match = needle.empty() || panel->GetDisplayPersonaName().Lower().Contains(needle) ||
                     panel->GetDisplayPersonaState().Lower().Contains(needle);
        panel->Show(match);
        if (match)
        {
            if (m_recentIDs.count(it->first))
            {
                ++recent;
            }
            else
            {
                ++(panel->IsOnline() ? online : offline);
            }
        }
    }
    m_recentLabel->SetLabel(wxString::Format("Played with recently (%d)", recent));
    m_recentLabel->Show(recent > 0);
    m_onlineLabel->SetLabel(wxString::Format("Online (%d)", online));
    m_onlineLabel->Show(online > 0);
    m_offlineLabel->SetLabel(wxString::Format("Offline (%d)", offline));
    m_offlineLabel->Show(offline > 0);
    m_friendsListContainer->FitInside();
    m_friendsListContainer->Layout();
}

void FriendsListFrame::OnRemotePlayInviteResult(RemotePlayInviteResult_t* inviteResultCb)
{
    if (inviteResultCb->m_eResult != k_ERemoteClientLaunchResultOK)
    {
        wxMessageBox(
            "Couldn't start Remote Play. " + DescribeLaunchResult(inviteResultCb->m_eResult) +
            wxString::Format("\n\n(Steam result %d)", (int)inviteResultCb->m_eResult),
            "Remote Play Whatever",
            wxOK | wxICON_ERROR
        );
    }
    else
    {
        if(inviteResultCb->m_player.m_playerID.IsValid())
        {
            const char* persona = GClientContext()->SteamFriends()->GetFriendPersonaName(inviteResultCb->m_player.m_playerID);
            wxMessageBox(
                "Invite sent to " + wxString::FromUTF8(persona && *persona ? persona : "your friend") +
                ". They'll get the link in Steam chat.",
                "Remote Play Whatever",
                wxOK | wxICON_INFORMATION
            );
        }
        else
        {
            wxString link = wxString::FromUTF8(ClickableURL(inviteResultCb->m_szConnectURL).c_str());
            bool copied = false;
            // Close the clipboard before showing the message box; keeping it open
            // while the box is up blocks pasting the link anywhere else.
            if(wxTheClipboard->Open())
            {
                copied = wxTheClipboard->SetData(new wxTextDataObject(link));
                wxTheClipboard->Flush();
                wxTheClipboard->Close();
            }
            wxMessageBox(
                (copied ? "Guest invite link copied to the clipboard:\n\n" : "Guest invite link:\n\n") + link +
                "\n\nSend it to whoever you want to play with. Anyone with the link can join.",
                "Remote Play Whatever",
                wxOK | wxICON_INFORMATION
            );
        }
    }
}

bool FriendsListFrame::CanSendInvite()
{
    if (!GClientContext()->SteamUser()->BLoggedOn() || !GetRunningGameID().IsValid())
    {
        wxMessageBox(
            "No game is running. Start the game through Steam first, then invite.",
            "No game running",
            wxOK | wxICON_INFORMATION
        );
        return false;
    }
    return true;
}

bool FriendsListFrame::ShowNonSteamWarning()
{
    AppId_t nonSteamAppID = m_pRemoteInvite->GetNonSteamAppID();
    CGameID gameRunning = GetRunningGameID();

    if (gameRunning.IsSteamApp() && gameRunning.AppID() != nonSteamAppID)
    {
        return true;
    }

    bool dlgResult = false;
    wxMessageDialog* dlg = new wxMessageDialog(this,
                                               "Starting a Remote Play Together session for a non-Steam game\n"
                                               "will expose your entire desktop to the remote player.\n"
                                               "The Steam client might need to be restarted to stop\n"
                                               "the desktop sharing session!\n\n"
                                               "Continue?",
                                               "Non-Steam Game confirmation",
                                               wxOK | wxCANCEL);
    if(dlg->ShowModal() == wxID_OK)
    {
        dlgResult = true;
    }
    delete dlg;
    return dlgResult;
}

void FriendsListFrame::OnFriendPanelClick(wxCommandEvent &event)
{
    // Ask from the main window once this click has been handled: the clicked
    // panel can go away while a dialog is open (if the friendship ends), so
    // nothing below may touch it.
    if(m_inviteQueued)
    {
        return;
    }
    FriendPanel* sender = (FriendPanel*)event.GetEventObject();
    CSteamID invitee = sender->GetSteamID();
    wxString name = sender->GetDisplayPersonaName();
    bool online = sender->IsOnline();
    m_inviteQueued = true;
    CallAfter([this, invitee, name, online]()
    {
        ConfirmFriendInvite(invitee, name, online);
        m_inviteQueued = false;
    });
}

void FriendsListFrame::ConfirmFriendInvite(CSteamID invitee, const wxString& name, bool online)
{
    if(!CanSendInvite())
    {
        return;
    }

    if(!ShowNonSteamWarning())
    {
        return;
    }

    wxString question = "Send remote play invite to " + name + "?";
    if(!online)
    {
        question = name + " appears offline (they may be invisible). "
                   "The invite link goes to them in Steam chat, and it should work once they're on Steam, "
                   "as long as you're still sharing.\n\nSend the invite?";
    }
    wxMessageDialog* dlg = new wxMessageDialog(this, question, "Confirm Remote Play Invite", wxOK | wxCANCEL);
    if(dlg->ShowModal() == wxID_OK)
    {
        m_notice.clear();   // an old message would be about something else
        m_pRemoteInvite->SendInvite(invitee);
    }
    delete dlg;
}

void FriendsListFrame::OnGuestPanelClick(wxCommandEvent &event)
{
    if(!CanSendInvite())
    {
        return;
    }

    if(!ShowNonSteamWarning())
    {
        return;
    }

    wxMessageDialog* dlg = new wxMessageDialog(this, "Create guest invite link?", "Confirm Remote Play Invite", wxOK | wxCANCEL);
    if(dlg->ShowModal() == wxID_OK)
    {
        m_notice.clear();   // an old message would be about something else
        m_pRemoteInvite->SendInvite(CSteamID(0, k_EUniversePublic, k_EAccountTypeIndividual));
    }
    delete dlg;
}

