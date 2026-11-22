#ifndef REMOTEPLAYINVITEHANDLER_H
#define REMOTEPLAYINVITEHANDLER_H

#include <Steamworks.h>
#include <map>
#include <set>
#include <string>
#include <vector>

// A guest that has connected to our Remote Play Together session. We remember
// the exact RemotePlayPlayer_t Steam handed us on connect (correct SteamID,
// guest id and group id) plus the per-peripheral control state the host wants.
struct ConnectedGuest
{
    RemotePlayPlayer_t player;
    uint64 key;         // unique per guest, see RemotePlayInviteHandler::GuestKey
    uint64 steamID;     // 0 for Steam Link guests who joined without a Steam account
    uint32 sessionID;   // Steam's streaming session for this guest
    bool wantKbd;
    bool wantMouse;
    bool wantCtrl;
    long long joinedAt; // when they connected (Unix time), for "Joined 12 min ago"
    // What the guest had before the host took all control (Ctrl+Alt+D), so
    // giving it back (Ctrl+Alt+G) restores exactly that.
    bool hasSaved;
    bool savedKbd;
    bool savedMouse;
    bool savedCtrl;
};

class RemotePlayInviteHandler
{
public:
    RemotePlayInviteHandler();
    virtual ~RemotePlayInviteHandler() {}

    void SendInvite(CSteamID invitee);
    void CancelInvite(CSteamID invitee, uint32 guestID);

    void SetNonSteamAppID(AppId_t appID);
    void SetGuestID(uint32 guestID);

    AppId_t GetNonSteamAppID();

    // Called every app-timer tick (~200ms). Advances async group creation and
    // fires any queued invites once the group is ready (so SendInvite never
    // blocks the UI). Cheap no-op when there's nothing pending.
    void GroupTick();

    // If Steam won't create a Remote Play Together group, the invite can only
    // fall back to stock RPW's default group, where guests can usually watch
    // but not play. The UI asks the host what to do; command-line (one-shot)
    // mode sends anyway, like stock RPW did.
    bool HasGroupFailure() const { return m_groupState == GroupFailed && !m_pending.empty(); }
    void ResolveGroupFailure(bool sendAnyway);
    // No one to ask (command line, --serve): if Steam won't create a group,
    // send anyway, like stock RPW did. GroupFallbacks counts those.
    void SetAutoFallback(bool sendAnyway) { m_autoFallback = sendAnyway; }
    uint32 GroupFallbacks() const { return m_groupFallbacks; }
    // Command-line (one-shot) runs never disband a group they find, since the
    // main window's session may be using it.
    void SetKeepExistingGroups(bool keep) { m_keepGroups = keep; }
    // No sounds when guests come and go (--serve: its frontend says so).
    void SetQuiet(bool quiet) { m_quiet = quiet; }

    // Who a guest link is "for": no account, so anyone can use it.
    static CSteamID GuestLinkInvitee();

    // Each guest's input is applied when they join and whenever the host
    // changes it. With RPW_REASSERT set, this also applies it again every few
    // seconds (called from the app callback timer), like older versions did.
    void GrantControlTick();

    // --- Session ------------------------------------------------------------
    // True while invites are out, guests are connected, or the whole screen is
    // being shared for a non-Steam game.
    bool IsSessionActive() const;
    bool IsSharingDesktop() const { return m_enabledDesktopStreaming; }
    // Disconnect everyone, cancel unanswered invites (so guest links stop
    // working) and stop sharing the screen. Safe to call when nothing is active.
    void EndSession();
    // GUI timer tick (~200ms): when the game the session is for closes, end the
    // session, so guests of a non-Steam game don't keep seeing the desktop.
    void SessionTick();
    // One-off message for the UI (e.g. why a session ended), cleared on read.
    std::string TakeNotice();
    // Disconnect one guest (their invite is cancelled too, so they can't rejoin
    // with it). Others stay connected.
    void RemoveGuest(uint64 key);
    // The newest guest link (with https://) that nobody has used or
    // cancelled, or empty if there's none.
    std::string GuestLink() const;
    // The link of the guest link waiting for this guest slot, or empty if
    // there's none (used, cancelled, or Steam hasn't sent it yet).
    std::string GuestLinkFor(uint32 guestID) const;
    // The link Steam made for a friend's invite (its own invite card in their
    // chat is what they normally use), for sending it another way if that
    // doesn't arrive. Empty once they've joined or the invite is gone.
    std::string FriendLink(uint64 steamID) const;

    // Invites sent this session that nobody has used yet (a friend who
    // hasn't joined, or a guest link nobody has opened).
    struct WaitingInvite
    {
        uint64 steamID;     // the friend, or 0 for a guest link
        uint32 guestID;     // the guest slot it's for
    };
    std::vector<WaitingInvite> WaitingInvites() const;
    // Cancel one, so it stops working. A session with nothing left in it ends.
    void CancelWaitingInvite(uint64 steamID, uint32 guestID);

    // Roster access + per-guest control, driven by the control window UI.
    // A guest's choices are saved per SteamID and applied when they rejoin.
    const std::vector<ConnectedGuest>& Guests() const { return m_guests; }
    void SetGuestInput(uint64 key, bool kbd, bool mouse, bool ctrl);

    // Everyone at once (control window "Everyone" row and hotkeys).
    // which: 0 = keyboard, 1 = mouse, 2 = controller. With remember, each
    // guest's new setting is saved for the next time they join (the quick
    // hotkeys don't save, so they don't change what friends get next time).
    bool AllGuestsHave(int which) const;
    void SetAllGuestsInput(int which, bool on, bool remember);
    // Take every peripheral from every guest, remembering what each one had,
    // and give it back later (everything, if nothing was remembered).
    void TakeAllControl();
    void GiveBackControl();

    // Guests with a Steam account are identified by their SteamID. Steam Link
    // guests without an account all report SteamID 0, so they're identified by
    // their guest slot instead (a value that can never be a real SteamID).
    static uint64 GuestKey(const RemotePlayPlayer_t& player);
    // Bumped whenever the roster or anyone's control changes, so the UI can
    // tell when to rebuild itself.
    uint32 RosterRevision() const { return m_rosterRevision; }
    // Bumped whenever the session is ended on purpose (End session, the game
    // closed, a failed invite), as opposed to guests leaving by themselves.
    uint32 SessionEnds() const { return m_sessionEnds; }

private:
    void ApplyGuest(const ConnectedGuest& g);

    // Async group creation. Stock RPW hardcodes groupID=1 without ever creating
    // a group; we create/settle one first, then fire the queued invite(s).
    enum EGroupState { GroupNone, GroupWaiting, GroupReady, GroupFailed };
    struct PendingInvite { CSteamID invitee; AppId_t appID; };

    // The group Steam keeps for this host and the session's game, or 0 if
    // there's none yet.
    uint32 LiveGroup(class CRemoteClientManagerByName* rcm) const;
    // Whether Steam has linked the group to the game's process (the overlay
    // finds the game's group through that link).
    bool GroupLinkedToGame(class CRemoteClientManagerByName* rcm, uint32 group) const;

    bool m_enabledDesktopStreaming;
    uint32 m_remoteGuestID;
    uint32 m_groupID;
    uint32 m_gamePID;           // the game's overlay process (0: none found)
    uint64 m_gamePIDGame;       // the game (CGameID) m_gamePID was looked for
    bool m_gamePIDKnown;        // m_gamePID has been looked for
    uint32 m_lastLinkedGroup;   // diagnostics: last group Steam linked to it
    AppId_t m_nonsteamAppID;

    EGroupState m_groupState;
    AppId_t m_groupAppID;       // the game the current group was set up for
    bool m_groupForNoGame;      // Steam made no group for the game, so it's the group for no game
    int m_groupTicks;
    bool m_autoFallback;
    uint32 m_groupFallbacks;
    bool m_keepGroups;
    bool m_quiet;
    std::vector<PendingInvite> m_pending;
    std::vector<RemotePlayPlayer_t> m_invited;  // invites sent this session

    uint64 m_sessionGame;       // game the current session is for (CGameID)
    int m_sessionGameGoneTicks;
    std::string m_notice;
    // Each guest link's URL, by the guest slot its invite reserved, and the
    // slots of links someone has already come in with.
    std::map<uint32, std::string> m_linkURLs;
    std::set<uint32> m_takenLinks;
    // The link Steam made for each friend's invite, by SteamID.
    std::map<uint64, std::string> m_friendLinks;

    std::vector<ConnectedGuest> m_guests;
    uint32 m_rosterRevision;
    uint32 m_sessionEnds;

    void ResetSession();

    STEAM_CALLBACK(RemotePlayInviteHandler, OnRemotePlayStart, RemoteClientStartStreamSession_t, m_remoteStartCb);
    STEAM_CALLBACK(RemotePlayInviteHandler, OnRemotePlayStop, RemoteClientStopStreamSession_t, m_remoteStopCb);
    STEAM_CALLBACK(RemotePlayInviteHandler, OnRemotePlayInviteResult, RemotePlayInviteResult_t, m_remoteInviteResultCb);
};

#endif // REMOTEPLAYINVITEHANDLER_H
