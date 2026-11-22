#ifndef FRIENDSLISTFRAME_H
#define FRIENDSLISTFRAME_H
#include <map>
#include <set>
#include <wx/wx.h>
#include <wx/weakref.h>
#include "FriendPanel.h"
#include "QrCodeDialog.h"
#include "RemotePlayInviteHandler.h"

class RpwTrayIcon;
class AppUpdater;
class Card;
class FlatButton;
class SearchBox;
class StatusDot;
class WindowPlace;
class WrapLabel;

// The main window: a menu bar (File, Session, Options, Help), an update bar
// when there's a new version, a card for the game being shared (its picture,
// what's shared, the guest link and session buttons, invites nobody has used
// yet, short messages), then the searchable friends list with an Invite button
// per friend. When the window is wide the card and the friends list sit side
// by side.
class FriendsListFrame : public wxFrame
{
public:
    // How a short message in the session card looks.
    enum NoticeKind
    {
        NoticeInfo,
        NoticeSuccess,
        NoticeWarning,
    };

    FriendsListFrame(RemotePlayInviteHandler* handler);
    virtual ~FriendsListFrame();

    // Tray icon support: minimizing hides the window to the tray.
    void SetTray(RpwTrayIcon* tray);
    void ShowFromTray();
    void CopyGuestLink();
    void ConfirmEndSession();

    // Updating to a newer release (release builds on Windows).
    bool UpdatesSupported() const;
    long AvailableUpdate() const;
    bool UpdateBusy() const;
    bool CanCheckForUpdates() const;
    void CheckForUpdates();
    void StartUpdate();

    // A short message in the session card (e.g. after an update); it goes
    // away by itself after a few seconds.
    void ShowNotice(const wxString& text, NoticeKind kind = NoticeSuccess);

    // Ctrl+Alt+L (copy or create a guest link from inside a game) can be
    // used with this keyboard layout; it's held while a game or session runs.
    bool GuestLinkHotKeyUsable() const { return m_guestLinkKeyUsable; }

#ifdef __WXMSW__
    // A second copy of the app asks this one to show itself with a registered
    // window message (it may be hidden in the tray). The menu bar is drawn
    // dark here in dark mode.
    virtual WXLRESULT MSWWindowProc(WXUINT msg, WXWPARAM wParam, WXLPARAM lParam) override;
#endif

private:
    // The menu bar has the tray menu's options too; ticks, labels and greyed
    // items are brought up to date whenever one of its menus opens.
    void BuildMenuBar();
    void RefreshMenus();
    void ShowGuestControls();
    void ShowAbout();
    // The guest link (the given guest slot's, or the newest unused one) as a
    // QR code, for someone in the room to scan with their phone.
    void ShowGuestLinkQrCode(bool anyLink, uint32 guestID = 0);
    // Show or fold away the friends who appear offline (saved).
    void SetShowOffline(bool show);

    void BuildUpdateBar(wxSizer* sizer);
    void BuildSessionCard(wxSizer* sizer);
    void BuildFriendsList(wxSizer* sizer);
    // One column, or (wide window) the card beside the friends list.
    void UpdateColumns();
    // A section title in the friends list ("ONLINE (4)"), optionally with a
    // Show/Hide link.
    wxWindow* AddSectionHeader(wxStaticText** label, FlatButton** toggle);

    void AddFreiendPanel(CSteamID friendID);
    void RemoveFriendPanel(CSteamID friendID);
    // Played with recently first, then online friends, then offline ones.
    void ArrangeFriendPanels();
    void ApplyFilter();
    // Each friend's Invite button: ready, no game, sending, sent or joined.
    void RefreshInviteStates(bool canInvite);

    // Before an invite: is a game running (the card says so if not), and, for
    // a non-Steam game, is sharing the whole screen OK (asked once a session).
    bool ReadyToInvite();
    bool ConfirmScreenSharing();
    void InviteFriend(CSteamID invitee);
    void CreateGuestLink();

    RemotePlayInviteHandler* m_pRemoteInvite;
    wxPanel* m_body;
    wxBoxSizer* m_columns;          // vertical, or horizontal when wide
    wxBoxSizer* m_leftColumn;       // update bar and session card
    wxBoxSizer* m_rightColumn;      // search box and friends list
    wxWindow* m_columnDivider;
    bool m_wide;

    // Update bar.
    Card* m_updateCard;
    wxStaticText* m_updateTitle;
    WrapLabel* m_updateNotes;
    FlatButton* m_updateButton;

    // Session card.
    Card* m_gameCard;
    StatusDot* m_stateDot;
    wxStaticText* m_stateText;
    WrapLabel* m_stateDetail;
    FlatButton* m_linkButton;
    FlatButton* m_endButton;
    wxBoxSizer* m_waitingSizer;
    wxString m_waitingShown;
    Card* m_noticeCard;
    wxStaticText* m_noticeIcon;
    WrapLabel* m_noticeText;

    // Friends list.
    SearchBox* m_search;
    wxScrolledWindow* m_friendsListContainer;
    wxBoxSizer* m_friendsListSizer;
    wxWindow* m_recentHeader;
    wxWindow* m_onlineHeader;
    wxWindow* m_offlineHeader;
    wxStaticText* m_recentLabel;
    wxStaticText* m_onlineLabel;
    wxStaticText* m_offlineLabel;
    FlatButton* m_offlineToggle;
    wxStaticText* m_noMatches;
    wxBoxSizer* m_recentSizer;
    wxBoxSizer* m_onlineSizer;
    wxBoxSizer* m_offlineSizer;
    bool m_showOffline;

    std::map<uint64, FriendPanel*> m_friendPanels;
    // "Played with recently": when friends last joined, and who's shown there.
    std::map<uint64, long> m_joinTimes;
    std::set<uint64> m_recentIDs;
    // Friends whose invite is on its way to Steam, and since when.
    std::map<uint64, wxLongLong> m_sending;

    void OnFriendInvite(wxCommandEvent& event);
    void OnLinkButton();
    void OnTick(wxTimerEvent& event);
    void OnClose(wxCloseEvent& event);
    void OnIconize(wxIconizeEvent& event);
    void UpdateStatus();
    // Rows for invites nobody has used yet; true if they changed.
    bool RefreshWaitingInvites();
    void OnUpdaterChange();
    void RefreshUpdateButton();
    void OnGuestLinkHotKey();
    void UpdateGuestLinkHotKey();
    bool HotKeyLinkPending() const;
    // Tray notification plus a sound, for when the host is in the game.
    void Announce(const wxString& title, const wxString& text, bool ok = true);

    RpwTrayIcon* m_tray;
    AppUpdater* m_updater;
    WindowPlace* m_place;           // remembers where the window was
    wxString m_menuState;           // what the menu bar was last brought up to date for
    wxWeakRef<QrCodeDialog> m_qrDialog; // the QR code window, while it's open
    wxString m_trayShown;
    wxString m_notice;
    NoticeKind m_noticeKind;
    wxString m_noticeShown;
    NoticeKind m_noticeKindShown;
    wxLongLong m_noticeUntil;
    wxLongLong m_copiedUntil;
    unsigned long long m_bannerGame;    // the game whose picture is shown (CGameID, 0 = none)
    wxLongLong m_linkRequestedAt;       // when the window asked Steam for a guest link (0 = not waiting)
    wxTimer m_tick;
    int m_statusRefreshTicks;
    bool m_askingGroupFallback;
    bool m_inviteQueued;
    uint32 m_rosterSeen;
    bool m_guestLinkKeyUsable;      // Ctrl+Alt+L doesn't type anything here
    bool m_guestLinkHotKey;         // ...and is registered right now
    wxLongLong m_guestLinkKeyRetryAt;
    wxLongLong m_hotKeyLinkSince;   // when Ctrl+Alt+L asked Steam for a link (0 = not waiting)

    STEAM_CALLBACK(FriendsListFrame, OnRemotePlayInviteResult, RemotePlayInviteResult_t, m_remoteInviteResultCb);
    STEAM_CALLBACK(FriendsListFrame, OnPersonaStateChange, PersonaStateChange_t, m_personaStateCb);
};

#endif // FRIENDSLISTFRAME_H
