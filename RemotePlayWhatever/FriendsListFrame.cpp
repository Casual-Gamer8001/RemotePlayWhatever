#include <wx/wx.h>
#include <wx/clipbrd.h>
#include <wx/config.h>
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
#include "UiWidgets.h"

// Hotkey id for Ctrl+Alt+L (the control window's ids start at 0xB001).
static const int kHotKeyGuestLink = 0xB010;
// Saved setting: whether the friends list shows the friends who are offline.
static const char* const kShowOfflineKey = "/Options/ShowOfflineFriends";

// Guests don't need Steam: a guest link works with the free Steam Link app.
static const char* const kLinkForAnyone = "Anyone can join with it, even without a Steam account, using the free Steam Link app.";

// Menu bar commands (Exit and About have the standard ids).
enum
{
    idMenuHideToTray = wxID_HIGHEST + 600,
    idMenuGuestLink,
    idMenuControls,
    idMenuEndSession,
    idMenuThemeAuto,
    idMenuThemeLight,
    idMenuThemeDark,
    idMenuShowOffline,
    idMenuSounds,
    idMenuStartWithWindows,
    idMenuAutoUpdates,
    idMenuCheckUpdates,
    idMenuReleaseNotes,
    idMenuProjectPage,
};

static CSteamID GuestLinkInvitee()
{
    return CSteamID(0, k_EUniversePublic, k_EAccountTypeIndividual);
}

// A game's picture, or an invalid image if there's none (or it won't load).
static wxImage LoadArtwork(const wxString& path)
{
    wxImage image;
    if (!path.empty())
    {
        wxLogNull quiet;
        image.LoadFile(path);
    }
    return image;
}

FriendsListFrame::FriendsListFrame(RemotePlayInviteHandler* handler):
    wxFrame(NULL, wxID_ANY, _("Remote Play Whatever"), wxPoint(50, 50), wxDefaultSize),
    m_pRemoteInvite(handler),
    m_body(nullptr),
    m_columns(nullptr),
    m_leftColumn(nullptr),
    m_rightColumn(nullptr),
    m_columnDivider(nullptr),
    m_wide(false),
    m_updateCard(nullptr),
    m_updateTitle(nullptr),
    m_updateNotes(nullptr),
    m_updateButton(nullptr),
    m_gameCard(nullptr),
    m_stateDot(nullptr),
    m_stateText(nullptr),
    m_stateDetail(nullptr),
    m_linkButton(nullptr),
    m_endButton(nullptr),
    m_waitingSizer(nullptr),
    m_noticeCard(nullptr),
    m_noticeIcon(nullptr),
    m_noticeText(nullptr),
    m_search(nullptr),
    m_friendsListContainer(nullptr),
    m_friendsListSizer(nullptr),
    m_recentHeader(nullptr),
    m_onlineHeader(nullptr),
    m_offlineHeader(nullptr),
    m_recentLabel(nullptr),
    m_onlineLabel(nullptr),
    m_offlineLabel(nullptr),
    m_offlineToggle(nullptr),
    m_noMatches(nullptr),
    m_recentSizer(nullptr),
    m_onlineSizer(nullptr),
    m_offlineSizer(nullptr),
    m_showOffline(false),
    m_tray(nullptr),
    m_updater(nullptr),
    m_noticeKind(NoticeInfo),
    m_noticeKindShown(NoticeInfo),
    m_noticeUntil(0),
    m_copiedUntil(0),
    m_bannerGame(0),
    m_linkRequestedAt(0),
    m_tick(this),
    m_statusRefreshTicks(0),
    m_askingGroupFallback(false),
    m_inviteQueued(false),
    m_rosterSeen(0),
    m_guestLinkKeyUsable(false),
    m_guestLinkHotKey(false),
    m_guestLinkKeyRetryAt(0),
    m_hotKeyLinkSince(0),
    m_remoteInviteResultCb(this, &FriendsListFrame::OnRemotePlayInviteResult),
    m_personaStateCb(this, &FriendsListFrame::OnPersonaStateChange)
{
    SetIcons(AppIcons());
    wxConfigBase* config = wxConfigBase::Get();
    m_showOffline = config && config->ReadBool(kShowOfflineKey, false);
    BuildMenuBar();

#if wxUSE_HOTKEY
    // Ctrl+Alt+L copies a guest link without leaving the game (OnGuestLinkHotKey).
    // It's only taken while a game runs or a session is on (UpdateGuestLinkHotKey),
    // and not at all where AltGr+L types a letter (Polish "ł", for example).
    m_guestLinkKeyUsable = CtrlAltKeyIsFree('L');
    Bind(wxEVT_HOTKEY, [this](wxKeyEvent& event)
    {
        if (event.GetId() != kHotKeyGuestLink)
        {
            event.Skip();
            return;
        }
        // Pressed in this window, it's the shortcut Session > guest link
        // shows, so it does the same; from inside a game, OnGuestLinkHotKey.
#ifdef __WXMSW__
        const bool inThisWindow = ::GetForegroundWindow() == GetHWND();
#else
        const bool inThisWindow = IsActive();
#endif
        if (inThisWindow)
        {
            OnLinkButton();
        }
        else
        {
            OnGuestLinkHotKey();
        }
    });
#endif

    // Two columns side by side in a wide window (UpdateColumns), one otherwise:
    // the update bar and session card, then the search box and friends list.
    m_body = new wxPanel(this, wxID_ANY);
    Ui::SetColours(m_body, &Ui::kWindow);
    m_columns = new wxBoxSizer(wxVERTICAL);
    m_leftColumn = new wxBoxSizer(wxVERTICAL);
    m_rightColumn = new wxBoxSizer(wxVERTICAL);
    BuildUpdateBar(m_leftColumn);
    BuildSessionCard(m_leftColumn);

    m_search = new SearchBox(m_body, "Search friends or games");
    m_rightColumn->Add(m_search, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, FromDIP(10));
    m_search->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { ApplyFilter(); });

    BuildFriendsList(m_rightColumn);
    m_columnDivider = new wxWindow(m_body, wxID_ANY);
    Ui::SetColours(m_columnDivider, &Ui::kDivider);
    m_columnDivider->SetMinSize(wxSize(1, -1));
    m_columnDivider->Hide();
    m_columns->Add(m_leftColumn, 0, wxEXPAND);
    m_columns->Add(m_columnDivider, 0, wxEXPAND);
    m_columns->Add(m_rightColumn, 1, wxEXPAND);
    m_body->SetSizer(m_columns);
    // The search box gets the focus first (as before), so typing a name
    // filters the list straight away; Tab then goes on to the buttons.
    m_search->MoveBeforeInTabOrder(m_updateCard);

    // Centred, with a maximum width when the window is very wide (UpdateColumns).
    wxBoxSizer* frameSizer = new wxBoxSizer(wxHORIZONTAL);
    frameSizer->AddStretchSpacer();
    frameSizer->Add(m_body, 0, wxEXPAND);
    frameSizer->AddStretchSpacer();
    SetSizer(frameSizer);
    Ui::SetColours(this, &Ui::kWindow);
    SetMinSize(FromDIP(wxSize(300, 440)));
    SetSize(FromDIP(wxSize(340, 690)));
    Bind(wxEVT_SIZE, [this](wxSizeEvent& event)
    {
        UpdateColumns();
        event.Skip();
    });
    UpdateColumns();
    Layout();

    Bind(EVT_FRIEND_INVITE, &FriendsListFrame::OnFriendInvite, this);
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
    RefreshMenus();
}

void FriendsListFrame::BuildMenuBar()
{
    // Menus at the top left like most Windows apps; the tray menu has the
    // same options for when the window is hidden.
    wxMenu* file = new wxMenu;
    file->Append(idMenuHideToTray, "&Hide to tray");
    file->AppendSeparator();
    file->Append(wxID_EXIT, "E&xit");

    wxMenu* session = new wxMenu;
    session->Append(idMenuGuestLink, "&Create guest link");
    session->Append(idMenuControls, "Show &guest controls");
    session->AppendSeparator();
    session->Append(idMenuEndSession, "&End session");

    wxMenu* theme = new wxMenu;
    theme->AppendRadioItem(idMenuThemeAuto, "&Match Windows");
    theme->AppendRadioItem(idMenuThemeLight, "&Light");
    theme->AppendRadioItem(idMenuThemeDark, "&Dark");
    wxMenu* options = new wxMenu;
    options->AppendSubMenu(theme, "&Theme");
    options->AppendCheckItem(idMenuShowOffline, "Show &offline friends");
    options->AppendCheckItem(idMenuSounds, "&Sounds when guests join or leave");
#ifdef __WXMSW__
    options->AppendCheckItem(idMenuStartWithWindows, "Start with &Windows");
#endif
    if (AppUpdater::Supported())
    {
        options->AppendSeparator();
        options->AppendCheckItem(idMenuAutoUpdates, "Check for updates &automatically");
    }

    wxMenu* help = new wxMenu;
    if (AppUpdater::Supported())
    {
        help->Append(idMenuCheckUpdates, wxString::Format("Check for &updates (you have build %ld)", AppUpdater::CurrentBuild()));
    }
    help->Append(idMenuReleaseNotes, "&Release notes");
    help->Append(idMenuProjectPage, "Remote Play Whatever on &GitHub");
    help->AppendSeparator();
    help->Append(wxID_ABOUT, "&About Remote Play Whatever");

    wxMenuBar* bar = new wxMenuBar;
    bar->Append(file, "&File");
    bar->Append(session, "&Session");
    bar->Append(options, "&Options");
    bar->Append(help, "&Help");
    SetMenuBar(bar);

    Bind(wxEVT_MENU_OPEN, [this](wxMenuEvent& event)
    {
        RefreshMenus();
        event.Skip();
    });
    Bind(wxEVT_MENU, [this](wxCommandEvent&)
    {
        // Where the minimize button puts it (OnIconize).
        if (m_tray && m_tray->IsIconInstalled())
        {
            Iconize(true);
        }
    }, idMenuHideToTray);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Close(true); }, wxID_EXIT);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { OnLinkButton(); }, idMenuGuestLink);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowGuestControls(); }, idMenuControls);
    Bind(wxEVT_MENU, [this](wxCommandEvent&)
    {
        if (m_pRemoteInvite->IsSessionActive())
        {
            ConfirmEndSession();
        }
    }, idMenuEndSession);
    // The settings are switched from what they are (the tray menu may have
    // changed one since this menu was last open), then the ticks follow.
    Bind(wxEVT_MENU, [this](wxCommandEvent& event)
    {
        SaveThemeChoice(event.GetId() == idMenuThemeLight ? ThemeLight :
                        event.GetId() == idMenuThemeDark ? ThemeDark : ThemeAuto);
        ApplySavedTheme();
        RefreshMenus();
    }, idMenuThemeAuto, idMenuThemeDark);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { SetShowOffline(!m_showOffline); }, idMenuShowOffline);
    Bind(wxEVT_MENU, [this](wxCommandEvent&)
    {
        SetSoundsEnabled(!SoundsEnabled());
        RefreshMenus();
    }, idMenuSounds);
#ifdef __WXMSW__
    Bind(wxEVT_MENU, [this](wxCommandEvent&)
    {
        SetStartWithWindows(!StartsWithWindows());
        RefreshMenus();
    }, idMenuStartWithWindows);
#endif
    Bind(wxEVT_MENU, [this](wxCommandEvent&)
    {
        AppUpdater::SetAutoChecksEnabled(!AppUpdater::AutoChecksEnabled());
        RefreshMenus();
    }, idMenuAutoUpdates);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { CheckForUpdates(); }, idMenuCheckUpdates);
    Bind(wxEVT_MENU, [](wxCommandEvent&) { wxLaunchDefaultBrowser(AppUpdater::ReleasesPage()); }, idMenuReleaseNotes);
    Bind(wxEVT_MENU, [](wxCommandEvent&) { wxLaunchDefaultBrowser(AppUpdater::ProjectPage()); }, idMenuProjectPage);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShowAbout(); }, wxID_ABOUT);
}

void FriendsListFrame::RefreshMenus()
{
    wxMenuBar* bar = GetMenuBar();
    if (!bar)
    {
        return;
    }
    // The same choice as the link button: create a link, copy the unused one
    // again, or make another.
    const bool session = m_pRemoteInvite->IsSessionActive();
    wxString link = !session ? "&Create guest link" :
                    !m_pRemoteInvite->GuestLink().empty() ? "&Copy guest link" : "&New guest link";
    if (m_guestLinkKeyUsable)
    {
        link += "\tCtrl+Alt+L";
    }
    wxMenuItem* linkItem = bar->FindItem(idMenuGuestLink);
    if (linkItem && linkItem->GetItemLabel() != link)
    {
        linkItem->SetItemLabel(link);
    }
    bar->Enable(idMenuGuestLink, m_linkRequestedAt == 0);
    bar->Enable(idMenuEndSession, session);
    bar->Enable(idMenuHideToTray, m_tray && m_tray->IsIconInstalled());

    const ThemeChoice theme = LoadThemeChoice();
    bar->Check(theme == ThemeLight ? idMenuThemeLight : theme == ThemeDark ? idMenuThemeDark : idMenuThemeAuto, true);
    bar->Check(idMenuShowOffline, m_showOffline);
    bar->Check(idMenuSounds, SoundsEnabled());
#ifdef __WXMSW__
    bar->Check(idMenuStartWithWindows, StartsWithWindows());
#endif
    if (bar->FindItem(idMenuAutoUpdates))
    {
        bar->Check(idMenuAutoUpdates, AppUpdater::AutoChecksEnabled());
    }
    if (bar->FindItem(idMenuCheckUpdates))
    {
        bar->Enable(idMenuCheckUpdates, !m_updater || m_updater->CanCheckNow());
    }
}

void FriendsListFrame::ShowGuestControls()
{
    for (wxWindowList::compatibility_iterator node = wxTopLevelWindows.GetFirst(); node; node = node->GetNext())
    {
        ControlFrame* controls = dynamic_cast<ControlFrame*>(node->GetData());
        if (controls)
        {
            controls->Show();
            controls->Raise();
            return;
        }
    }
}

void FriendsListFrame::ShowAbout()
{
    const long build = AppUpdater::CurrentBuild();
    wxMessageDialog about(this,
                          build > 0 ? wxString::Format("Remote Play Whatever, build %ld", build)
                                    : wxString("Remote Play Whatever (a local build)"),
                          "About Remote Play Whatever", wxOK | wxICON_INFORMATION);
    about.SetExtendedMessage("Steam Remote Play Together for any game.\n\n" + AppUpdater::ProjectPage() +
                             "\nBased on RemotePlayWhatever by m4dEngi (MIT License).");
    about.ShowModal();
}

void FriendsListFrame::SetShowOffline(bool show)
{
    m_showOffline = show;
    wxConfigBase* config = wxConfigBase::Get();
    if (config)
    {
        config->Write(kShowOfflineKey, m_showOffline);
        config->Flush();
    }
    ApplyFilter();
    RefreshMenus();
}

void FriendsListFrame::UpdateColumns()
{
    if (!m_body || !m_columns)
    {
        return;
    }
    const int width = GetClientSize().x;
    // Two columns from 660 DIP wide; back to one below 620, so it doesn't
    // flip back and forth at one width.
    const bool wide = m_wide ? width >= FromDIP(620) : width >= FromDIP(660);
    // Up to about 940 DIP, in the middle of a wider window (an ultrawide
    // screen, say), so the Invite buttons stay near the names.
    const int bodyWidth = wide ? std::min(width, FromDIP(940)) : width;
    m_body->SetMinSize(wxSize(bodyWidth, -1));
    if (wide == m_wide)
    {
        return;
    }
    m_wide = wide;
    m_columns->SetOrientation(wide ? wxHORIZONTAL : wxVERTICAL);
    m_leftColumn->SetMinSize(wide ? wxSize(FromDIP(340), -1) : wxSize(-1, -1));
    m_columnDivider->Show(wide);
    // The search box starts the right column, so there it needs a top margin too.
    wxSizerItem* search = m_rightColumn->GetItem(m_search);
    if (search)
    {
        search->SetFlag(wide ? (wxALL | wxEXPAND) : (wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND));
    }
    Layout();
    m_body->Layout();
    m_friendsListContainer->FitInside();
}

void FriendsListFrame::BuildUpdateBar(wxSizer* sizer)
{
    m_updateCard = new Card(m_body, &Ui::kInfoBg, &Ui::kInfoBorder);
    wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
    if (Ui::HasIconFont())
    {
        wxStaticText* icon = new wxStaticText(m_updateCard, wxID_ANY, wxString(Ui::kGlyphDownload));
        icon->SetFont(Ui::IconFont(GetFont()).Scaled(1.25f));
        Ui::SetColours(icon, nullptr, &Ui::kAccent);
        row->Add(icon, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(10));
    }
    wxBoxSizer* text = new wxBoxSizer(wxVERTICAL);
    m_updateTitle = new wxStaticText(m_updateCard, wxID_ANY, wxEmptyString);
    m_updateTitle->SetFont(Ui::SemiBold(GetFont()));
    Ui::SetColours(m_updateTitle, nullptr, &Ui::kText);
    text->Add(m_updateTitle, 0, wxEXPAND);
    // What's new in that version, in small grey text (all of it in the button's tooltip).
    m_updateNotes = new WrapLabel(m_updateCard);
    m_updateNotes->SetFont(GetFont().Scaled(0.92f));
    Ui::SetColours(m_updateNotes, nullptr, &Ui::kTextSecondary);
    text->Add(m_updateNotes, 0, wxEXPAND | wxTOP, FromDIP(2));
    row->Add(text, 1, wxALIGN_CENTER_VERTICAL);
    m_updateButton = new FlatButton(m_updateCard, "Update", FlatButton::Primary, 0, true);
    m_updateButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { StartUpdate(); });
    row->Add(m_updateButton, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(10));
    m_updateCard->Content()->Add(row, 0, wxEXPAND | wxALL, FromDIP(10));
    sizer->Add(m_updateCard, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(10));
    m_updateCard->Hide();
}

void FriendsListFrame::BuildSessionCard(wxSizer* sizer)
{
    // The game's picture and name across the top (when a game is running),
    // then what's shared, the buttons, waiting invites and short messages.
    m_gameCard = new Card(m_body);
    wxBoxSizer* content = m_gameCard->Content();
    const int pad = FromDIP(12);

    wxBoxSizer* stateRow = new wxBoxSizer(wxHORIZONTAL);
    m_stateDot = new StatusDot(m_gameCard, 9);
    stateRow->Add(m_stateDot, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(7));
    m_stateText = new wxStaticText(m_gameCard, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                   wxST_NO_AUTORESIZE | wxST_ELLIPSIZE_END);
    m_stateText->SetFont(Ui::SemiBold(GetFont()));
    Ui::SetColours(m_stateText, nullptr, &Ui::kText);
    m_stateText->SetMinSize(wxSize(FromDIP(40), m_stateText->GetCharHeight()));
    stateRow->Add(m_stateText, 1, wxALIGN_CENTER_VERTICAL);
    content->Add(stateRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad);

    // Under it, lined up with the text: what guests will see.
    wxBoxSizer* detailRow = new wxBoxSizer(wxHORIZONTAL);
    detailRow->AddSpacer(FromDIP(16));
    m_stateDetail = new WrapLabel(m_gameCard);
    m_stateDetail->SetFont(GetFont().Scaled(0.92f));
    Ui::SetColours(m_stateDetail, nullptr, &Ui::kTextSecondary);
    detailRow->Add(m_stateDetail, 1);
    content->AddSpacer(FromDIP(2));
    content->Add(detailRow, 0, wxEXPAND | wxLEFT | wxRIGHT, pad);

    // Create a guest link (or copy it again / make another), and End session.
    wxBoxSizer* buttons = new wxBoxSizer(wxHORIZONTAL);
    m_linkButton = new FlatButton(m_gameCard, "Create guest link", FlatButton::Primary, Ui::kGlyphLink);
    m_linkButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OnLinkButton(); });
    buttons->Add(m_linkButton, 1);
    m_endButton = new FlatButton(m_gameCard, "End session");
    m_endButton->SetToolTip("Disconnect everyone, cancel unused invites and guest links, and stop sharing your screen.");
    m_endButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { ConfirmEndSession(); });
    m_endButton->Hide();
    buttons->Add(m_endButton, 1, wxLEFT, FromDIP(8));
    content->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad);

    // Invites nobody has used yet, each with a Cancel button (RefreshWaitingInvites).
    m_waitingSizer = new wxBoxSizer(wxVERTICAL);
    content->Add(m_waitingSizer, 0, wxEXPAND | wxLEFT | wxRIGHT, pad);

    // A short message: invite sent, link copied, why a session ended...
    m_noticeCard = new Card(m_gameCard, &Ui::kGoodBg, nullptr, 6);
    wxBoxSizer* noticeRow = new wxBoxSizer(wxHORIZONTAL);
    if (Ui::HasIconFont())
    {
        m_noticeIcon = new wxStaticText(m_noticeCard, wxID_ANY, wxString(Ui::kGlyphCheck));
        m_noticeIcon->SetFont(Ui::IconFont(GetFont()));
        noticeRow->Add(m_noticeIcon, 0, wxRIGHT | wxTOP, FromDIP(2));
        noticeRow->AddSpacer(FromDIP(6));
    }
    m_noticeText = new WrapLabel(m_noticeCard);
    noticeRow->Add(m_noticeText, 1);
    m_noticeCard->Content()->Add(noticeRow, 0, wxEXPAND | wxALL, FromDIP(8));
    content->Add(m_noticeCard, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad);
    m_noticeCard->Hide();

    content->AddSpacer(pad);
    sizer->Add(m_gameCard, 0, wxEXPAND | wxALL, FromDIP(10));
}

wxWindow* FriendsListFrame::AddSectionHeader(wxStaticText** label, FlatButton** toggle)
{
    wxPanel* header = new wxPanel(m_friendsListContainer, wxID_ANY);
    Ui::SetColours(header, &Ui::kWindow);
    wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
    row->AddSpacer(FromDIP(14));
    *label = new wxStaticText(header, wxID_ANY, wxEmptyString);
    (*label)->SetFont(Ui::SemiBold(GetFont()).Scaled(0.88f));
    Ui::SetColours(*label, nullptr, &Ui::kTextSecondary);
    row->Add(*label, 1, wxALIGN_CENTER_VERTICAL);
    if (toggle)
    {
        *toggle = new FlatButton(header, "Show", FlatButton::Link, 0, true);
        (*toggle)->SetFont(GetFont().Scaled(0.92f));
        row->Add(*toggle, 0, wxALIGN_CENTER_VERTICAL);
    }
    row->AddSpacer(FromDIP(8));
    header->SetSizer(row);
    header->SetMinSize(wxSize(-1, FromDIP(30)));
    m_friendsListSizer->Add(header, 0, wxEXPAND | wxTOP, FromDIP(4));
    return header;
}

void FriendsListFrame::BuildFriendsList(wxSizer* sizer)
{
    m_friendsListContainer = new wxScrolledWindow(m_body, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
    Ui::SetColours(m_friendsListContainer, &Ui::kWindow);
    // Vertical scrolling only: the rows always fit the width.
    m_friendsListContainer->SetScrollRate(0, FromDIP(8));
    m_friendsListSizer = new wxBoxSizer(wxVERTICAL);

    // Friends who've joined before, then online friends, then the ones who
    // appear offline (they may just be invisible, and an invite link reaches
    // them in Steam chat anyway). The offline ones fold away.
    m_recentHeader = AddSectionHeader(&m_recentLabel, nullptr);
    m_recentLabel->SetLabel("PLAYED WITH RECENTLY");
    m_recentSizer = new wxBoxSizer(wxVERTICAL);
    m_friendsListSizer->Add(m_recentSizer, 0, wxEXPAND);
    m_onlineHeader = AddSectionHeader(&m_onlineLabel, nullptr);
    m_onlineSizer = new wxBoxSizer(wxVERTICAL);
    m_friendsListSizer->Add(m_onlineSizer, 0, wxEXPAND);
    m_offlineHeader = AddSectionHeader(&m_offlineLabel, &m_offlineToggle);
    m_offlineToggle->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { SetShowOffline(!m_showOffline); });
    m_offlineSizer = new wxBoxSizer(wxVERTICAL);
    m_friendsListSizer->Add(m_offlineSizer, 0, wxEXPAND);

    m_noMatches = new wxStaticText(m_friendsListContainer, wxID_ANY, "No friends match your search.");
    Ui::SetColours(m_noMatches, nullptr, &Ui::kTextSecondary);
    m_noMatches->Hide();
    m_friendsListSizer->Add(m_noMatches, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(14));
    m_friendsListSizer->AddSpacer(FromDIP(8));

    m_joinTimes = LoadGuestJoinTimes();
    m_rosterSeen = m_pRemoteInvite->RosterRevision();
    int cFriends = GClientContext()->SteamFriends()->GetFriendCount(k_EFriendFlagImmediate);
    for(int i = 0; i < cFriends; ++i)
    {
        AddFreiendPanel(GClientContext()->SteamFriends()->GetFriendByIndex(i, k_EFriendFlagImmediate));
    }

    m_friendsListContainer->SetSizer(m_friendsListSizer);
    ArrangeFriendPanels();
    sizer->Add(m_friendsListContainer, 1, wxEXPAND);
}

void FriendsListFrame::OnClose(wxCloseEvent& event)
{
    // Closing the main window quits the app. Take the (usually hidden) control
    // window and the tray icon with it; otherwise they keep the app running
    // invisibly. Only our own windows are destroyed here: the tray icon owns
    // and destroys its own hidden window.
    m_tick.Stop();
    m_updater->Shutdown();
#if wxUSE_HOTKEY
    if (m_guestLinkHotKey)
    {
        UnregisterHotKey(kHotKeyGuestLink);
        m_guestLinkHotKey = false;
    }
#endif
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
        m_tray->SetStatus(m_trayShown);
    }
    // File > Hide to tray needs the icon.
    RefreshMenus();
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
    // Windows switched between light and dark mode (Settings > Colors).
    if (msg == WM_SETTINGCHANGE && lParam && ::lstrcmpiW((LPCWSTR)lParam, L"ImmersiveColorSet") == 0)
    {
        CallAfter([]() { ApplySavedTheme(); });
    }
    WXLRESULT result = 0;
    if (Ui::MenuBarMessage(GetHWND(), msg, wParam, lParam, &result))
    {
        return result;
    }
    result = wxFrame::MSWWindowProc(msg, wParam, lParam);
    if (msg == WM_NCPAINT || msg == WM_NCACTIVATE)
    {
        Ui::MenuBarLine(GetHWND());
    }
    return result;
}
#endif

void FriendsListFrame::UpdateStatus()
{
    const wxLongLong now = wxGetLocalTimeMillis();
    const wxString handlerNotice = wxString::FromUTF8(m_pRemoteInvite->TakeNotice().c_str());
    if (!handlerNotice.empty())
    {
        m_notice = handlerNotice;
        m_noticeKind = NoticeWarning;
        m_noticeUntil = now + 15000;
    }
    if (!m_notice.empty() && now >= m_noticeUntil)
    {
        m_notice.clear();
    }
    // Requests Steam never answered (it normally does within seconds).
    if (m_linkRequestedAt != 0 && now - m_linkRequestedAt > 20000)
    {
        m_linkRequestedAt = 0;
    }
    for (std::map<uint64, wxLongLong>::iterator it = m_sending.begin(); it != m_sending.end();)
    {
        if (now - it->second > 20000)
        {
            it = m_sending.erase(it);
        }
        else
        {
            ++it;
        }
    }
    if (m_copiedUntil != 0 && now >= m_copiedUntil)
    {
        m_copiedUntil = 0;
    }

    const bool signedIn = GClientContext()->SteamUser()->BLoggedOn();
    const bool session = m_pRemoteInvite->IsSessionActive();
    const CGameID game = GetRunningGameID();
    const bool hasGame = signedIn && game.IsValid();
    const bool nonSteam = hasGame && !(game.IsSteamApp() && game.AppID() != m_pRemoteInvite->GetNonSteamAppID());
    const uint32 account = signedIn ? GClientContext()->SteamUser()->GetSteamID().GetAccountID() : 0;
    wxString name = hasGame ? GetGameName(game, account) : wxString();
    const wxString gameInText = name.empty() ? wxString(nonSteam ? "a non-Steam game" : "your game") : name;
    if (name.empty())
    {
        name = nonSteam ? "A non-Steam game" : "Your game";
    }
    const unsigned guests = (unsigned)m_pRemoteInvite->Guests().size();
    const bool desktop = m_pRemoteInvite->IsSharingDesktop();

    // The state line, the line under it, and the same in a sentence for the tray.
    wxString state, detail, trayText;
    wxColour dot = Ui::kTextMuted;
    const wxColour* detailColour = &Ui::kTextSecondary;
    if (!signedIn)
    {
        state = "Steam isn't signed in";
        detail = "Sign in to Steam, then invite someone.";
        trayText = "Steam isn't signed in.";
    }
    else if (session)
    {
        const wxString who = guests == 0 ? wxString("waiting for guests") :
                             guests == 1 ? wxString("1 guest connected") :
                             wxString::Format("%u guests connected", guests);
        state = "Sharing" + Ui::Separator() + who;
        detail = desktop ? "Guests can see your whole screen" : "Only the game is shared, not your screen";
        detailColour = desktop ? &Ui::kWarnText : &Ui::kTextSecondary;
        dot = Ui::kGood;
        trayText = "Sharing " + gameInText + ": " +
                   (guests == 0 ? wxString("waiting for guests to join.") : who + ".") +
                   (desktop ? " Guests can see your whole screen." : "");
    }
    else if (!hasGame)
    {
        state = "No game running";
        detail = "Start a game through Steam, then invite someone.";
        trayText = "No game running. Start a game through Steam, then invite someone.";
    }
    else
    {
        state = "Ready to share";
        detail = nonSteam ? "Not a Steam game, so your whole screen is shared"
                          : "Only the game will be shared, not your screen";
        detailColour = nonSteam ? &Ui::kWarnText : &Ui::kTextSecondary;
        dot = Ui::kGood;
        trayText = "Ready to share: " + gameInText +
                   (nonSteam ? " (non-Steam game, so your whole screen will be shared)" : "");
    }
    if (!m_notice.empty())
    {
        trayText = m_notice;
    }

    bool relayout = false;
    // Guests seeing the whole screen shouldn't see this app (or its links).
    HideFromScreenCapture(desktop);

    // The game's picture and name.
    const unsigned long long gameKey = hasGame ? (unsigned long long)game.ToUint64() : 0;
    if (gameKey != m_bannerGame)
    {
        m_bannerGame = gameKey;
        if (gameKey)
        {
            m_gameCard->SetBanner(LoadArtwork(GetGameArtworkPath(game, account)), name, gameKey);
        }
        else
        {
            m_gameCard->ClearBanner();
        }
        relayout = true;
    }
    else if (gameKey)
    {
        m_gameCard->SetBannerTitle(name);
    }

    if (m_stateText->GetLabelText() != state)
    {
        m_stateText->SetLabelText(state);
    }
    m_stateDot->SetColour(dot);
    if (m_stateDetail->GetLabel() != detail)
    {
        m_stateDetail->SetLabel(detail);
        relayout = true;
    }
    if (m_stateDetail->GetForegroundColour() != *detailColour)
    {
        Ui::SetColours(m_stateDetail, nullptr, detailColour);
        m_stateDetail->Refresh();
    }

    // One button for the guest link: create one, copy the unused one again,
    // or (once it's been used) make another.
    const bool hasLink = session && !m_pRemoteInvite->GuestLink().empty();
    wxString label, tip;
    wchar_t glyph = Ui::kGlyphLink;
    FlatButton::Kind kind = FlatButton::Secondary;
    if (!session)
    {
        label = "Create guest link";
        kind = FlatButton::Primary;
        const wxString anyone = "A link anyone can use to join, even without a Steam account (with the free "
                                "Steam Link app). One person per link.";
        tip = !hasGame ? "Start a game through Steam first. " + anyone :
              m_guestLinkKeyUsable ? anyone + " While you're in the game, Ctrl+Alt+L copies one too." : anyone;
    }
    else if (hasLink)
    {
        label = m_copiedUntil != 0 ? "Copied!" : "Copy guest link";
        glyph = m_copiedUntil != 0 ? Ui::kGlyphCheck : Ui::kGlyphCopy;
        tip = "Copy this session's guest link again.";
    }
    else
    {
        label = "New guest link";
        tip = "Create another guest link for this session: each link lets one person in.";
    }
    const bool creating = m_linkRequestedAt != 0;
    if (creating)
    {
        label = "Creating a link...";
    }
    if (m_linkButton->GetLabel() != label || m_linkButton->GetKind() != kind)
    {
        relayout = true;
    }
    m_linkButton->SetLabel(label);
    m_linkButton->SetGlyph(glyph);
    m_linkButton->SetKind(kind);
    m_linkButton->Enable(!creating);
    m_linkButton->SetMuted(!session && !hasGame);
    if (m_linkButton->GetToolTipText() != tip)
    {
        m_linkButton->SetToolTip(tip);
    }
    if (m_endButton->IsShown() != session)
    {
        m_endButton->Show(session);
        relayout = true;
    }
    if (RefreshWaitingInvites())
    {
        relayout = true;
    }

    // The short message, coloured by kind.
    if (m_notice != m_noticeShown || m_noticeKind != m_noticeKindShown)
    {
        m_noticeShown = m_notice;
        m_noticeKindShown = m_noticeKind;
        if (!m_notice.empty())
        {
            const wxColour* background = &Ui::kInfoBg;
            const wxColour* text = &Ui::kInfoText;
            wchar_t icon = Ui::kGlyphInfo;
            if (m_noticeKind == NoticeSuccess)
            {
                background = &Ui::kGoodBg;
                text = &Ui::kGoodText;
                icon = Ui::kGlyphCheck;
            }
            else if (m_noticeKind == NoticeWarning)
            {
                background = &Ui::kWarnBg;
                text = &Ui::kWarnText;
                icon = Ui::kGlyphWarning;
            }
            m_noticeCard->SetColours(background, nullptr);
            Ui::SetColours(m_noticeText, nullptr, text);
            m_noticeText->SetLabel(m_notice);
            m_noticeText->Refresh();
            if (m_noticeIcon)
            {
                m_noticeIcon->SetLabel(wxString(icon));
                Ui::SetColours(m_noticeIcon, nullptr, text);
            }
        }
        m_noticeCard->Show(!m_notice.empty());
        relayout = true;
    }

    if (trayText != m_trayShown)
    {
        m_trayShown = trayText;
        if (m_tray)
        {
            m_tray->SetStatus(trayText);
        }
    }

    RefreshInviteStates(hasGame);
    if (relayout)
    {
        Ui::Relayout(m_gameCard);
    }
}

void FriendsListFrame::RefreshInviteStates(bool canInvite)
{
    std::set<uint64> waiting, joined;
    const std::vector<RemotePlayInviteHandler::WaitingInvite> invites = m_pRemoteInvite->WaitingInvites();
    for (size_t i = 0; i < invites.size(); ++i)
    {
        if (invites[i].steamID)
        {
            waiting.insert(invites[i].steamID);
        }
    }
    const std::vector<ConnectedGuest>& guests = m_pRemoteInvite->Guests();
    for (size_t i = 0; i < guests.size(); ++i)
    {
        if (guests[i].steamID)
        {
            joined.insert(guests[i].steamID);
        }
    }
    for (std::map<uint64, FriendPanel*>::iterator it = m_friendPanels.begin(); it != m_friendPanels.end(); ++it)
    {
        FriendPanel::InviteState state = canInvite ? FriendPanel::InviteReady : FriendPanel::InviteNoGame;
        if (joined.count(it->first))
        {
            state = FriendPanel::InviteJoined;
        }
        else if (waiting.count(it->first))
        {
            state = FriendPanel::InviteSent;
        }
        else if (m_sending.count(it->first))
        {
            state = FriendPanel::InviteSending;
        }
        it->second->SetInviteState(state);
    }
}

bool FriendsListFrame::RefreshWaitingInvites()
{
    static const size_t kShown = 4;
    const std::vector<RemotePlayInviteHandler::WaitingInvite> waiting = m_pRemoteInvite->WaitingInvites();
    std::vector<wxString> labels;
    wxString shown;
    for (size_t i = 0; i < waiting.size(); ++i)
    {
        wxString label = "Guest link" + Ui::Separator() + "not used yet";
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
        Card* row = new Card(m_gameCard, &Ui::kSubtle, nullptr, 6);
        wxBoxSizer* line = new wxBoxSizer(wxHORIZONTAL);
        if (Ui::HasIconFont())
        {
            wxStaticText* icon = new wxStaticText(row, wxID_ANY, wxString(Ui::kGlyphClock));
            icon->SetFont(Ui::IconFont(GetFont()));
            Ui::SetColours(icon, nullptr, &Ui::kTextSecondary);
            line->Add(icon, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
        }
        wxStaticText* text = new wxStaticText(row, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                              wxST_NO_AUTORESIZE | wxST_ELLIPSIZE_END);
        text->SetLabelText(labels[i]);
        Ui::SetColours(text, nullptr, &Ui::kText);
        text->SetMinSize(wxSize(FromDIP(40), text->GetCharHeight()));
        text->SetToolTip(id ? labels[i] + " to join." : wxString("Nobody has opened this guest link yet."));
        line->Add(text, 1, wxALIGN_CENTER_VERTICAL);
        FlatButton* cancel = new FlatButton(row, "Cancel", FlatButton::Link, 0, true);
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
        line->Add(cancel, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(6));
        row->Content()->Add(line, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, FromDIP(5));
        m_waitingSizer->Add(row, 0, wxEXPAND | wxTOP, FromDIP(8));
    }
    if (waiting.size() > kShown)
    {
        wxStaticText* more = new wxStaticText(m_gameCard, wxID_ANY,
                                              wxString::Format("and %u more", (unsigned)(waiting.size() - kShown)));
        Ui::SetColours(more, nullptr, &Ui::kTextSecondary);
        m_waitingSizer->Add(more, 0, wxTOP, FromDIP(6));
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
        m_copiedUntil = wxGetLocalTimeMillis() + 2000;
        UpdateStatus();
    }
}

void FriendsListFrame::OnLinkButton()
{
    if (m_pRemoteInvite->IsSessionActive() && !m_pRemoteInvite->GuestLink().empty())
    {
        CopyGuestLink();
    }
    else
    {
        CreateGuestLink();
    }
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

void FriendsListFrame::ShowNotice(const wxString& text, NoticeKind kind)
{
    m_notice = text;
    m_noticeKind = kind;
    m_noticeUntil = wxGetLocalTimeMillis() + (kind == NoticeWarning ? 15000 : 10000);
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

bool FriendsListFrame::CanCheckForUpdates() const
{
    return m_updater->CanCheckNow();
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
    wxString label = "Update";
    if (state == AppUpdater::Downloading)
    {
        label = "Downloading...";
    }
    else if (state == AppUpdater::Installing || state == AppUpdater::Restarting)
    {
        label = "Installing...";
    }

    bool relayout = false;
    if (m_updateCard->IsShown() != (build > 0))
    {
        m_updateCard->Show(build > 0);
        relayout = true;
    }
    const wxString title = wxString::Format("Build %ld is available", build);
    if (m_updateTitle->GetLabel() != title)
    {
        m_updateTitle->SetLabel(title);
        relayout = true;
    }
    if (m_updateButton->GetLabel() != label)
    {
        m_updateButton->SetLabel(label);
        relayout = true;
    }
    m_updateButton->Enable(!working);

    // What's new: up to two short lines under the title, the whole list in the button's tooltip.
    const wxString notes = build > 0 ? m_updater->NotesSummary(70) : wxString();
    const wxString notesText = notes.empty() ? wxString("Remote Play Whatever restarts with it.") : notes;
    if (m_updateNotes->GetLabel() != notesText)
    {
        m_updateNotes->SetLabel(notesText);
        relayout = true;
    }
    wxString tip = "Download the new version from GitHub and restart Remote Play Whatever with it.";
    if (build > 0 && !m_updater->Notes().empty())
    {
        tip += "\n\nWhat's new:\n" + m_updater->NotesList();
    }
    if (m_updateButton->GetToolTipText() != tip)
    {
        m_updateButton->SetToolTip(tip);
    }

    if (relayout)
    {
        Ui::Relayout(m_updateCard);
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
                wxString question = wxString::Format("Build %ld is available (you have build %ld).",
                                                     m_updater->AvailableBuild(), AppUpdater::CurrentBuild());
                if (!m_updater->Notes().empty())
                {
                    question += "\n\nWhat's new:\n" + m_updater->NotesList();
                }
                question += "\n\nUpdate now? Remote Play Whatever restarts afterwards.";
                int answer = wxMessageBox(
                    question,
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
            m_tray->ShowUpdateBalloon(m_updater->AvailableBuild(), m_updater->NotesSummary(90));
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
                "A Remote Play session started while the update was downloading, so it wasn't "
                "installed.\n\nClick \"Update\" again when the session is over.",
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
    UpdateGuestLinkHotKey();

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
    m_friendPanels[friendID64] = new FriendPanel(m_friendsListContainer, friendID);
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
        m_sending.erase(friendID64);
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
        m_recentSizer->Add(played[i].second, 0, wxEXPAND);
    }
    for (size_t i = 0; i < online.size(); ++i)
    {
        m_onlineSizer->Add(online[i], 0, wxEXPAND);
    }
    for (size_t i = 0; i < offline.size(); ++i)
    {
        m_offlineSizer->Add(offline[i], 0, wxEXPAND);
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
    const bool searching = !needle.empty();
    // A search looks through everyone, the folded-away offline friends too.
    const bool showOffline = m_showOffline || searching;
    int recent = 0, online = 0, offline = 0;
    for (auto it = m_friendPanels.begin(); it != m_friendPanels.end(); ++it)
    {
        FriendPanel* panel = it->second;
        // A search finds names and what friends are doing ("playing ...").
        const bool match = !searching || panel->GetDisplayPersonaName().Lower().Contains(needle) ||
                           panel->GetDisplayPersonaState().Lower().Contains(needle);
        const bool isRecent = m_recentIDs.count(it->first) != 0;
        const bool isOffline = !isRecent && !panel->IsOnline();
        if (match)
        {
            ++(isRecent ? recent : isOffline ? offline : online);
        }
        panel->Show(match && (!isOffline || showOffline));
    }
    m_recentHeader->Show(recent > 0);
    m_onlineLabel->SetLabel(wxString::Format("ONLINE (%d)", online));
    m_onlineHeader->Show(online > 0);
    m_offlineLabel->SetLabel(wxString::Format("OFFLINE (%d)", offline));
    m_offlineHeader->Show(offline > 0);
    m_offlineToggle->SetLabel(m_showOffline ? "Hide" : "Show");
    m_offlineToggle->SetToolTip(m_showOffline ? "Hide the friends who appear offline."
                                              : "Show the friends who appear offline (they may be invisible).");
    m_offlineToggle->Show(!searching);
    m_offlineHeader->Layout();
    m_noMatches->Show(searching && recent + online + offline == 0);
    m_friendsListContainer->FitInside();
    m_friendsListContainer->Layout();
}

void FriendsListFrame::OnRemotePlayInviteResult(RemotePlayInviteResult_t* inviteResultCb)
{
    const bool forFriend = inviteResultCb->m_player.m_playerID.IsValid();
    // A guest link asked for with Ctrl+Alt+L: answer with a tray notification,
    // not a message box that would pull the host out of the game.
    const bool viaHotKey = HotKeyLinkPending() && !forFriend;
    if (viaHotKey)
    {
        m_hotKeyLinkSince = 0;
    }
    if (forFriend)
    {
        m_sending.erase(inviteResultCb->m_player.m_playerID.ConvertToUint64());
    }
    else
    {
        m_linkRequestedAt = 0;
    }

    if (inviteResultCb->m_eResult != k_ERemoteClientLaunchResultOK)
    {
        if (viaHotKey)
        {
            Announce("Couldn't create a guest link", DescribeLaunchResult(inviteResultCb->m_eResult), false);
            return;
        }
        UpdateStatus();
        wxMessageBox(
            "Couldn't start Remote Play. " + DescribeLaunchResult(inviteResultCb->m_eResult) +
            wxString::Format("\n\n(Steam result %d)", (int)inviteResultCb->m_eResult),
            "Remote Play Whatever",
            wxOK | wxICON_ERROR,
            IsShown() ? this : nullptr
        );
        return;
    }

    if (forFriend)
    {
        const uint64 id = inviteResultCb->m_player.m_playerID.ConvertToUint64();
        const char* persona = GClientContext()->SteamFriends()->GetFriendPersonaName(inviteResultCb->m_player.m_playerID);
        const wxString name = wxString::FromUTF8(persona && *persona ? persona : "your friend");
        std::map<uint64, FriendPanel*>::const_iterator panel = m_friendPanels.find(id);
        const bool offline = panel != m_friendPanels.end() && !panel->second->IsOnline();
        ShowNotice(offline ? "Invite sent to " + name + ". They appear offline; it waits for them in Steam chat."
                           : "Invite sent to " + name + ". They'll get the link in Steam chat.");
        return;
    }

    wxString link = wxString::FromUTF8(ClickableURL(inviteResultCb->m_szConnectURL).c_str());
    bool copied = false;
    if (wxTheClipboard->Open())
    {
        copied = wxTheClipboard->SetData(new wxTextDataObject(link));
        wxTheClipboard->Flush();
        wxTheClipboard->Close();
    }
    if (viaHotKey)
    {
        // No link in the notification: guests may be watching the whole screen.
        Announce(copied ? "Guest link copied" : "Guest link created",
                 copied ? "Paste it to whoever you want to play with. " + wxString(kLinkForAnyone)
                        : wxString("Copy it from Remote Play Whatever's window or its tray menu."),
                 copied);
        UpdateStatus();
        return;
    }
    if (copied)
    {
        ShowNotice("Guest link copied. " + wxString(kLinkForAnyone));
    }
    else
    {
        ShowNotice("Guest link created: " + link + " " + kLinkForAnyone, NoticeInfo);
    }
}

bool FriendsListFrame::ReadyToInvite()
{
    if (!GClientContext()->SteamUser()->BLoggedOn())
    {
        ShowNotice("Sign in to Steam first, then try again.", NoticeWarning);
        return false;
    }
    if (!GetRunningGameID().IsValid())
    {
        ShowNotice("Start a game through Steam first, then invite.", NoticeWarning);
        return false;
    }
    return true;
}

bool FriendsListFrame::ConfirmScreenSharing()
{
    const CGameID game = GetRunningGameID();
    if (game.IsSteamApp() && game.AppID() != m_pRemoteInvite->GetNonSteamAppID())
    {
        return true;
    }
    // Already sharing the screen this session: asking once is enough.
    if (m_pRemoteInvite->IsSharingDesktop())
    {
        return true;
    }

    wxMessageDialog dialog(this,
                           "This isn't a Steam game, so Remote Play shares your whole screen with your guests, "
                           "not just the game. Remote Play Whatever keeps its own windows out of what they see.\n\n"
                           "If the sharing doesn't stop when the session ends, restart Steam.\n\n"
                           "Continue?",
                           "Share your whole screen?",
                           wxOK | wxCANCEL | wxICON_WARNING);
    dialog.SetOKCancelLabels("Share my screen", "Cancel");
    return dialog.ShowModal() == wxID_OK;
}

void FriendsListFrame::OnFriendInvite(wxCommandEvent& event)
{
    // Invite once this click has been handled: a question may come first, and
    // the row can go away while it's open (if the friendship ends), so
    // nothing below may touch it.
    FriendPanel* panel = dynamic_cast<FriendPanel*>(event.GetEventObject());
    if (!panel || m_inviteQueued)
    {
        return;
    }
    const CSteamID invitee = panel->GetSteamID();
    m_inviteQueued = true;
    CallAfter([this, invitee]()
    {
        InviteFriend(invitee);
        m_inviteQueued = false;
    });
}

void FriendsListFrame::InviteFriend(CSteamID invitee)
{
    if (!ReadyToInvite() || !ConfirmScreenSharing())
    {
        return;
    }
    m_notice.clear();   // an old message would be about something else
    m_sending[invitee.ConvertToUint64()] = wxGetLocalTimeMillis();
    m_pRemoteInvite->SendInvite(invitee);
    UpdateStatus();
}

void FriendsListFrame::CreateGuestLink()
{
    if (m_linkRequestedAt != 0 || HotKeyLinkPending())
    {
        return;     // one is on its way
    }
    if (!ReadyToInvite() || !ConfirmScreenSharing())
    {
        return;
    }
    m_notice.clear();
    m_linkRequestedAt = wxGetLocalTimeMillis();
    m_pRemoteInvite->SendInvite(GuestLinkInvitee());
    UpdateStatus();
}

bool FriendsListFrame::HotKeyLinkPending() const
{
    // Steam answers within seconds; after 20 s, stop waiting for it.
    return m_hotKeyLinkSince != 0 && wxGetLocalTimeMillis() - m_hotKeyLinkSince < 20000;
}

void FriendsListFrame::UpdateGuestLinkHotKey()
{
#if wxUSE_HOTKEY
    // A global shortcut is only held while it's useful, so Ctrl+Alt+L stays
    // free for other apps the rest of the time.
    const bool wanted = m_guestLinkKeyUsable &&
                        (m_pRemoteInvite->IsSessionActive() || GetRunningGameID().IsValid());
    if (wanted && !m_guestLinkHotKey && wxGetLocalTimeMillis() >= m_guestLinkKeyRetryAt)
    {
        m_guestLinkHotKey = RegisterHotKey(kHotKeyGuestLink, wxMOD_CONTROL | wxMOD_ALT, 'L');
        if (!m_guestLinkHotKey)
        {
            // Another app has it; try again in a while.
            m_guestLinkKeyRetryAt = wxGetLocalTimeMillis() + 30000;
        }
    }
    else if (!wanted && m_guestLinkHotKey)
    {
        UnregisterHotKey(kHotKeyGuestLink);
        m_guestLinkHotKey = false;
    }
#endif
}

void FriendsListFrame::Announce(const wxString& title, const wxString& text, bool ok)
{
#ifdef __WXMSW__
    // A notification may not show over a fullscreen game, but the sound does.
    ::MessageBeep(ok ? MB_OK : MB_ICONEXCLAMATION);
#endif
    if (m_tray)
    {
        m_tray->Notify(title, text, !ok);
    }
}

void FriendsListFrame::OnGuestLinkHotKey()
{
    // A guest link from this session that nobody has used yet: copy it again.
    if (!m_pRemoteInvite->GuestLink().empty())
    {
        CopyGuestLink();
        Announce("Guest link copied", "Paste it to whoever you want to play with. " + wxString(kLinkForAnyone));
        return;
    }
    if (HotKeyLinkPending() || m_linkRequestedAt != 0)
    {
        return;     // one is on its way
    }

    CGameID game = GetRunningGameID();
    if (!GClientContext()->SteamUser()->BLoggedOn() || !game.IsValid())
    {
        Announce("No game running", "Start the game through Steam, then press Ctrl+Alt+L again.", false);
        return;
    }
    // A non-Steam game shares the whole screen; that needs a yes in the main
    // window first (after that, the shortcut works for it too).
    const bool desktop = !(game.IsSteamApp() && game.AppID() != m_pRemoteInvite->GetNonSteamAppID());
    if (desktop && !m_pRemoteInvite->IsSharingDesktop())
    {
        Announce("Create this link in Remote Play Whatever",
                 "This isn't a Steam game, so guests would see your whole screen. Create the first link from the main window.",
                 false);
        return;
    }

    m_hotKeyLinkSince = wxGetLocalTimeMillis();
    m_notice.clear();
    m_pRemoteInvite->SendInvite(GuestLinkInvitee());
}
