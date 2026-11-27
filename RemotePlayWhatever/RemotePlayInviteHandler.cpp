#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include "SteamStuff.h"
#include "RemotePlayInviteHandler.h"
#include "UiHelpers.h"
#include "GameProcess.h"

// Made-up guests of throwaway test builds have this streaming session, which
// Steam never uses.
static const uint32 kFakeGuestSession = 0xFFFFFFFF;

// Steam's own Remote Play Together panel in the game's overlay. The app used
// to open it with every new group and link, but while it's open Steam pauses
// the guests' input ("Input is temporarily disabled while the host is busy"),
// and the guest controls window does its job. RPW_SHOW_STEAM_PANEL=1 opens it
// again, to compare if Steam ever turns out to need it.
static void ShowSteamPanel(CRemoteClientManagerByName* rcm, AppId_t appID)
{
    static const bool wanted = std::getenv("RPW_SHOW_STEAM_PANEL") != NULL;
    if (wanted)
    {
        clientvt::Log("[rp] opening Steam's Remote Play Together panel (app %u)\n", (unsigned)appID);
        rcm->ShowRemotePlayTogetherUI(appID);
    }
}

RemotePlayInviteHandler::RemotePlayInviteHandler() :
    m_enabledDesktopStreaming(false),
    m_remoteGuestID(1),
    m_nonsteamAppID(0),
    m_groupID(1),
    m_gamePID(0),
    m_gamePIDGame(0),
    m_gamePIDKnown(false),
    m_lastLinkedGroup(0),
    m_groupState(GroupNone),
    m_groupAppID(0),
    m_groupForNoGame(false),
    m_groupTicks(0),
    m_autoFallback(false),
    m_groupFallbacks(0),
    m_keepGroups(false),
    m_quiet(false),
    m_sessionGame(0),
    m_sessionGameGoneTicks(0),
    m_rosterRevision(0),
    m_sessionEnds(0),
    m_lastGuestSync(0),
    m_resultSlot(0),
    m_resultGroup(0),
    m_resultCode(0),
    m_resultFriend(0),
    m_remoteStartCb(this, &RemotePlayInviteHandler::OnRemotePlayStart),
    m_remoteStopCb(this, &RemotePlayInviteHandler::OnRemotePlayStop),
    m_remoteInviteResultCb(this, &RemotePlayInviteHandler::OnRemotePlayInviteResult),
    m_sessionConnectedCb(this, &RemotePlayInviteHandler::OnSessionConnected),
    m_sessionDisconnectedCb(this, &RemotePlayInviteHandler::OnSessionDisconnected)
{
}

AppId_t RemotePlayInviteHandler::GetNonSteamAppID()
{
    return m_nonsteamAppID;
}

CSteamID RemotePlayInviteHandler::GuestLinkInvitee()
{
    return CSteamID(0, k_EUniversePublic, k_EAccountTypeIndividual);
}

uint32 RemotePlayInviteHandler::LiveGroup(CRemoteClientManagerByName* rcm) const
{
    // The group Steam keeps for this host and game, or else the one it ties to
    // the game's process.
    uint32 g = rcm->GetLocalRemotePlayTogetherGroupID(m_groupAppID);
    if (!g && m_gamePID)
    {
        g = rcm->GetRemotePlayTogetherGroupIDForOverlayPID(m_gamePID);
    }
    // A session that had to settle for the group for no game keeps using it.
    if (!g && m_groupForNoGame)
    {
        g = rcm->GetLocalRemotePlayTogetherGroupID(0);
    }
    return g;
}

// Whether Steam returns this group for the game's process, which is how the
// overlay finds the group whose players it lists.
bool RemotePlayInviteHandler::GroupLinkedToGame(CRemoteClientManagerByName* rcm, uint32 group) const
{
    return !m_gamePID || rcm->GetRemotePlayTogetherGroupIDForOverlayPID(m_gamePID) == group;
}

// Queue an invite and kick off async group creation. The actual Steam invite is
// fired from GroupTick() once the group is ready, so this returns immediately
// and never blocks the UI (stock RPW hardcoded groupID=1 and never created one).
void RemotePlayInviteHandler::SendInvite(CSteamID invitee)
{
    CGameID gameID = GetRunningGameID();

    if (!gameID.IsValid())
    {
        return;
    }

    bool desktop = !(gameID.IsSteamApp() && gameID.AppID() != m_nonsteamAppID);
    AppId_t appID = desktop ? m_nonsteamAppID : gameID.AppID();

    // Leftovers from a session for a game that has since closed or changed.
    if (m_sessionGame && m_sessionGame != gameID.ToUint64())
    {
        EndSession();
    }
    m_sessionGame = gameID.ToUint64();
    m_sessionGameGoneTicks = 0;

    CRemoteClientManagerByName* rcm = GClientContext()->RemoteClientManager();

    // Steam turns whole-screen sharing off again when the last guest's stream
    // ends, so it's checked, not just remembered.
    if (desktop && (!m_enabledDesktopStreaming || !rcm->BStreamingDesktopToRemotePlayTogetherEnabled()))
    {
        rcm->SetStreamingDesktopToRemotePlayTogetherEnabled(true);
        m_enabledDesktopStreaming = true;
    }

    // The game's process, which Steam's overlay looks its group up by (a second
    // way to find the group, and for diagnostics). Finding it reads every
    // process's module list (about a quarter of a second), so it's looked for
    // once per game, and again only if that process has gone.
    if (!m_gamePIDKnown || m_gamePIDGame != gameID.ToUint64() || (m_gamePID && !ProcessStillRunning(m_gamePID)))
    {
        m_gamePID = FindGameOverlayPID();
        m_gamePIDGame = gameID.ToUint64();
        m_gamePIDKnown = true;
    }
    if (clientvt::DebugEnabled())
    {
        clientvt::Log("[rp] invite requested: invitee=%llu friend=%d game=%llu app=%u desktop=%d overlayPID=%u"
                      " | groups: forOverlayPID=%u local(0)=%u local(app)=%u\n",
                      (unsigned long long)invitee.ConvertToUint64(), (int)(invitee.GetAccountID() != 0),
                      (unsigned long long)gameID.ToUint64(), (unsigned)appID, (int)desktop, m_gamePID,
                      m_gamePID ? rcm->GetRemotePlayTogetherGroupIDForOverlayPID(m_gamePID) : 0,
                      rcm->GetLocalRemotePlayTogetherGroupID(0),
                      appID ? rcm->GetLocalRemotePlayTogetherGroupID(appID) : 0);
    }

    // A group from an earlier invite can be gone by now (everyone left, or the
    // game was restarted or switched). Check it again instead of inviting into
    // a group that no longer exists, which leaves guests only able to watch.
    if (m_groupState == GroupReady || m_groupState == GroupFailed)
    {
        uint32 live = LiveGroup(rcm);
        if (live && appID == m_groupAppID)
        {
            m_groupID = live;
            m_groupState = GroupReady;
        }
        else
        {
            m_groupState = GroupNone;
        }
    }

    PendingInvite pi;
    pi.invitee = invitee;
    pi.appID = appID;
    pi.asLink = desktop && invitee.GetAccountID() != 0;
    m_pending.push_back(pi);

    if (m_groupState == GroupNone)
    {
        m_groupAppID = appID;
        m_groupForNoGame = false;
        // Only the game's own group will do: in a group for another game (or
        // for none, which is what asking for game 0 gives) a guest's mouse
        // movement never reaches the game.
        uint32 leftover = LiveGroup(rcm);
        // A Steam game's session can also find the group for no game left over.
        uint32 leftoverNoGame = appID ? rcm->GetLocalRemotePlayTogetherGroupID(0) : 0;
        if (leftoverNoGame == leftover)
        {
            leftoverNoGame = 0;
        }
        // Someone may be in a group this copy of the app knows nothing about:
        // guests who stayed after the app was closed and opened again, people
        // invited through Steam itself, or the main window's session while this
        // is a command-line run. Steam's streams say so; then it's kept.
        const bool inUse = !m_guests.empty() || !m_invited.empty() || m_keepGroups ||
                           rcm->BIsStreamingSessionActive();
        if (leftover && inUse)
        {
            m_groupID = leftover;
            m_groupState = GroupReady;
            clientvt::Log("[rp] reusing group %u (in use)\n", leftover);
        }
        else
        {
            // A new session gets a freshly created group. A guest who joined a
            // group left over from an earlier session couldn't move the mouse
            // (keyboard and clicks still worked), so it's disbanded first.
            if (!inUse)
            {
                const uint32 old[] = { leftover, leftoverNoGame };
                for (size_t i = 0; i < sizeof(old) / sizeof(old[0]); ++i)
                {
                    if (old[i])
                    {
                        rcm->DisbandRemotePlayTogetherGroup(old[i]);
                        clientvt::Log("[rp] disbanded leftover group %u\n", old[i]);
                    }
                }
            }
            if (appID)
            {
                ShowSteamPanel(rcm, appID);
            }
            bool created = rcm->BCreateRemotePlayGroup(appID);
            clientvt::Log("[rp] BCreateRemotePlayGroup(app %u) -> %d\n", (unsigned)appID, (int)created);
            m_groupState = GroupWaiting;
            m_groupTicks = 0;
        }
    }
    // If Waiting, the invite goes out once the group is ready.
}

// Drives async group creation and flushes queued invites. Runs every ~200ms.
void RemotePlayInviteHandler::GroupTick()
{
    if (clientvt::DebugEnabled() && m_gamePID && m_sessionGame)
    {
        // Diagnostics: when Steam links (or unlinks) a group to the game's
        // process. The overlay's player list depends on that link.
        uint32 linked = GClientContext()->RemoteClientManager()->GetRemotePlayTogetherGroupIDForOverlayPID(m_gamePID);
        if (linked != m_lastLinkedGroup)
        {
            clientvt::Log("[rp] group linked to game pid %u: %u -> %u\n", m_gamePID, m_lastLinkedGroup, linked);
            m_lastLinkedGroup = linked;
        }
    }

    if (m_groupState == GroupWaiting)
    {
        CRemoteClientManagerByName* rcm = GClientContext()->RemoteClientManager();
        // Polling settles it. The game's own group is wanted; after ~3s with no
        // group at all, settle for the group for no game, like before (guests
        // can join it, but it's outside the overlay's menu).
        uint32 g = LiveGroup(rcm);
        const bool tiedToGame = g != 0;
        const bool linked = g && GroupLinkedToGame(rcm, g);
        if (!g && m_groupAppID && m_groupTicks >= 15)
        {
            g = rcm->GetLocalRemotePlayTogetherGroupID(0);
            m_groupForNoGame = g != 0;
        }
        if (g)
        {
            clientvt::Log("[rp] group ready after %d ticks: group=%u tiedToGame=%d linkedToProcess=%d"
                          " (local(app)=%u forOverlayPID=%u local(0)=%u)\n",
                          m_groupTicks, g, (int)tiedToGame, (int)linked,
                          rcm->GetLocalRemotePlayTogetherGroupID(m_groupAppID),
                          m_gamePID ? rcm->GetRemotePlayTogetherGroupIDForOverlayPID(m_gamePID) : 0,
                          rcm->GetLocalRemotePlayTogetherGroupID(0));
            m_groupID = g;
            m_groupState = GroupReady;
        }
        else
        {
            ++m_groupTicks;
            // Ask Steam again in case the first request didn't take.
            if (m_groupTicks == 5 || m_groupTicks == 12)
            {
                bool created = rcm->BCreateRemotePlayGroup(m_groupAppID);
                clientvt::Log("[rp] BCreateRemotePlayGroup(app %u) -> %d\n", (unsigned)m_groupAppID, (int)created);
            }
            // Still no group for the game: ask for the group for no game, which
            // the session settles for (see above), like before.
            if (m_groupAppID && (m_groupTicks == 15 || m_groupTicks == 25))
            {
                bool created = rcm->BCreateRemotePlayGroup(0);
                clientvt::Log("[rp] BCreateRemotePlayGroup(app 0) -> %d\n", (int)created);
            }
            if (m_groupTicks >= 50)                        // ~10s and still no group
            {
                m_groupState = GroupFailed;
                if (m_autoFallback)
                {
                    std::cout << "Warning: Steam didn't create a Remote Play Together group; "
                                 "inviting anyway (guests may only be able to watch)." << std::endl;
                    ResolveGroupFailure(true);
                }
            }
        }
    }

    if (m_groupState == GroupReady && !m_pending.empty())
    {
        CRemoteClientManagerByName* rcm = GClientContext()->RemoteClientManager();
        bool refused = false;
        for (size_t i = 0; i < m_pending.size(); ++i)
        {
            // Steam's own UI invites a friend with guest id 0 and only gives
            // guest links a guest id. With a guest id, Steam treats a friend's
            // invite as a guest link too: it only hands back a URL (no invite
            // card in their chat) and they join as "Guest N", outside the
            // overlay's player list. Steam's own invite only works for a Steam
            // game, though: for one it doesn't run (a non-Steam game, Share my
            // screen) it starts the friend's stream, stops it at once and
            // answers "failed". There the friend gets a guest link, made like
            // any other, which is sent to them in Steam chat once Steam has it.
            const bool isFriend = m_pending[i].invitee.GetAccountID() != 0;
            const bool asLink = isFriend && m_pending[i].asLink;
            uint32 guest = 0;
            if (!isFriend || asLink)
            {
                guest = rcm->GetAvailableRemotePlayTogetherGuestID(m_groupID);
                if (!guest)
                {
                    guest = m_remoteGuestID;
                }
                m_remoteGuestID = guest + 1;
            }

            const CSteamID invitee = asLink ? GuestLinkInvitee() : m_pending[i].invitee;
            RemotePlayPlayer_t rppInvitee = { invitee, guest, m_groupID, 0, 0, 0 };
            bool sent = rcm->BCreateRemotePlayInviteAndSession(rppInvitee, m_pending[i].appID);
            clientvt::Log("[rp] invite %s: friend=%d steam=%llu guest=%u group=%u app=%u\n",
                          sent ? "sent" : "refused by Steam", (int)isFriend,
                          (unsigned long long)rppInvitee.m_playerID.ConvertToUint64(), guest, m_groupID,
                          (unsigned)m_pending[i].appID);
            if (asLink)
            {
                clientvt::Log("[rp]   (a guest link for friend %llu, to send in Steam chat: not a Steam game)\n",
                              (unsigned long long)m_pending[i].invitee.ConvertToUint64());
            }
            if (sent)
            {
                // Steam hands a guest slot out again once the guest who came in
                // with its link has left, so a new link can get an old, used
                // link's slot. That old link is over; forget it, or the new one
                // would count as used too and never be shown.
                for (size_t j = m_invited.size(); guest && j > 0; --j)
                {
                    if (m_invited[j - 1].m_playerID.GetAccountID() == 0 && m_invited[j - 1].m_unGuestID == guest)
                    {
                        m_invited.erase(m_invited.begin() + (j - 1));
                    }
                }
                if (guest)
                {
                    m_takenLinks.erase(guest);
                    m_linkURLs.erase(guest);
                    m_linkFriends.erase(guest);
                }
                if (asLink)
                {
                    m_linkFriends[guest] = m_pending[i].invitee.ConvertToUint64();
                }
                m_invited.push_back(rppInvitee);
                ++m_rosterRevision;
            }
            else
            {
                // Steam didn't create it, so there's nothing to wait for.
                refused = true;
                std::cout << "Error: Steam didn't send the invite." << std::endl;
            }
        }
        m_pending.clear();

        if (refused)
        {
            // Nothing left to share, so don't keep the screen shared for nobody.
            if (m_guests.empty() && m_invited.empty())
            {
                EndSession();
            }
            m_notice = "Steam didn't send the invite. Try again, or restart the game through Steam if it keeps happening.";
        }
    }
}

void RemotePlayInviteHandler::CancelInvite(CSteamID invitee, uint32 guestID)
{
    if(GClientContext()->RemoteClientManager()->BIsStreamingSessionActive())
    {
        RemotePlayPlayer_t rppInvitee = { invitee, guestID, m_groupID, 0, 0, 0 };
        GClientContext()->RemoteClientManager()->CancelRemotePlayInviteAndSession(rppInvitee);
    }
}

bool RemotePlayInviteHandler::IsSessionActive() const
{
    return !m_guests.empty() || !m_invited.empty() || !m_pending.empty() ||
           m_enabledDesktopStreaming || m_groupState == GroupWaiting;
}

void RemotePlayInviteHandler::EndSession()
{
    CRemoteClientManagerByName* rcm = GClientContext()->RemoteClientManager();

    // Connected guests: end their streams and their places in the session.
    for (size_t i = 0; i < m_guests.size(); ++i)
    {
        if (m_guests[i].sessionID)
        {
            rcm->StopStreamingSession(m_guests[i].sessionID);
        }
        rcm->CancelRemotePlayInviteAndSession(m_guests[i].player);
    }

    // Invites nobody has used yet, including guest links.
    for (size_t i = 0; i < m_invited.size(); ++i)
    {
        rcm->CancelRemotePlayInviteAndSession(m_invited[i]);
    }

    if (m_enabledDesktopStreaming || rcm->BStreamingDesktopToRemotePlayTogetherEnabled())
    {
        rcm->SetStreamingDesktopToRemotePlayTogetherEnabled(false);
    }

    ++m_sessionEnds;
    ResetSession();
}

void RemotePlayInviteHandler::ResetSession()
{
    m_enabledDesktopStreaming = false;
    m_sessionGame = 0;
    m_sessionGameGoneTicks = 0;
    m_remoteGuestID = 1;
    m_groupState = GroupNone;
    m_groupForNoGame = false;
    m_pending.clear();
    m_invited.clear();
    m_guests.clear();
    m_linkURLs.clear();
    m_takenLinks.clear();
    m_friendLinks.clear();
    m_linkFriends.clear();
    m_sessionPlayers.clear();
    m_lastGuestSync = 0;
    ++m_rosterRevision;
}

void RemotePlayInviteHandler::RemoveGuest(uint64 key)
{
    CRemoteClientManagerByName* rcm = GClientContext()->RemoteClientManager();
    for (size_t i = 0; i < m_guests.size(); ++i)
    {
        if (m_guests[i].key != key)
        {
            continue;
        }

        ConnectedGuest g = m_guests[i];
        m_guests.erase(m_guests.begin() + i);
        ++m_rosterRevision;
        // So Steam's list of connected clients, which may still have them for
        // a moment, doesn't bring them back.
        m_sessionPlayers.erase(g.sessionID);

        if (g.sessionID)
        {
            rcm->StopStreamingSession(g.sessionID);
        }
        rcm->CancelRemotePlayInviteAndSession(g.player);

        // A friend's own invite would let them straight back in, and so would
        // the guest link this guest came in with; cancel those too.
        for (size_t j = 0; j < m_invited.size();)
        {
            const RemotePlayPlayer_t& sent = m_invited[j];
            const bool theirLink = sent.m_playerID.GetAccountID() == 0 && sent.m_unGuestID == g.player.m_unGuestID;
            if ((g.steamID && sent.m_playerID.ConvertToUint64() == g.steamID) || theirLink)
            {
                rcm->CancelRemotePlayInviteAndSession(sent);
                m_linkURLs.erase(sent.m_unGuestID);
                m_takenLinks.erase(sent.m_unGuestID);
                if (theirLink)
                {
                    m_linkFriends.erase(sent.m_unGuestID);
                }
                if (g.steamID)
                {
                    m_friendLinks.erase(g.steamID);
                }
                m_invited.erase(m_invited.begin() + j);
            }
            else
            {
                ++j;
            }
        }
        return;
    }
}

std::vector<RemotePlayInviteHandler::WaitingInvite> RemotePlayInviteHandler::WaitingInvites() const
{
    std::vector<WaitingInvite> waiting;
    for (size_t i = 0; i < m_invited.size(); ++i)
    {
        const RemotePlayPlayer_t& sent = m_invited[i];
        const bool isLink = sent.m_playerID.GetAccountID() == 0;
        // A guest link sent to a friend in Steam chat waits for that friend.
        const uint64 friendID = isLink ? LinkInviteFriend(sent.m_unGuestID) : sent.m_playerID.ConvertToUint64();
        // A guest takes the slot their invite reserved; a friend can also be
        // recognised by their SteamID. A guest link someone has come in with
        // stays used after they leave.
        bool used = isLink && m_takenLinks.count(sent.m_unGuestID) != 0;
        for (size_t g = 0; g < m_guests.size() && !used; ++g)
        {
            // Friends are invited with guest id 0, so only a guest link's slot
            // identifies who it was for.
            used = (sent.m_unGuestID && m_guests[g].player.m_unGuestID == sent.m_unGuestID) ||
                   (friendID && m_guests[g].steamID == friendID);
        }
        if (!used)
        {
            WaitingInvite w;
            w.steamID = friendID;
            w.guestID = sent.m_unGuestID;
            waiting.push_back(w);
        }
    }
    return waiting;
}

std::string RemotePlayInviteHandler::GuestLink() const
{
    const std::vector<WaitingInvite> waiting = WaitingInvites();
    for (size_t i = waiting.size(); i > 0; --i)
    {
        if (!waiting[i - 1].steamID)
        {
            std::map<uint32, std::string>::const_iterator url = m_linkURLs.find(waiting[i - 1].guestID);
            if (url != m_linkURLs.end() && !url->second.empty())
            {
                return url->second;
            }
        }
    }
    return std::string();
}

std::string RemotePlayInviteHandler::GuestLinkFor(uint32 guestID) const
{
    const std::vector<WaitingInvite> waiting = WaitingInvites();
    for (size_t i = 0; i < waiting.size(); ++i)
    {
        if (!waiting[i].steamID && waiting[i].guestID == guestID)
        {
            std::map<uint32, std::string>::const_iterator url = m_linkURLs.find(guestID);
            return url != m_linkURLs.end() ? url->second : std::string();
        }
    }
    return std::string();
}

std::string RemotePlayInviteHandler::FriendLink(uint64 steamID) const
{
    std::map<uint64, std::string>::const_iterator url = m_friendLinks.find(steamID);
    if (!steamID || url == m_friendLinks.end())
    {
        return std::string();
    }
    const std::vector<WaitingInvite> waiting = WaitingInvites();
    for (size_t i = 0; i < waiting.size(); ++i)
    {
        if (waiting[i].steamID == steamID)
        {
            return url->second;
        }
    }
    return std::string();
}

uint64 RemotePlayInviteHandler::LinkInviteFriend(uint32 guestID) const
{
    std::map<uint32, uint64>::const_iterator f = guestID ? m_linkFriends.find(guestID) : m_linkFriends.end();
    return f != m_linkFriends.end() ? f->second : 0;
}

uint64 RemotePlayInviteHandler::ResultLinkFriend(const RemotePlayInviteResult_t* result) const
{
    if (result->m_player.m_playerID.GetAccountID() != 0)
    {
        return 0;       // Steam's own invite to a friend
    }
    if (result->m_player.m_unGuestID == m_resultSlot && result->m_player.m_unOverlayPIDGroupID == m_resultGroup &&
        (int)result->m_eResult == m_resultCode)
    {
        return m_resultFriend;
    }
    return LinkInviteFriend(result->m_player.m_unGuestID);
}

void RemotePlayInviteHandler::SendLinkToFriend(uint64 friendID, const std::string& link)
{
    if (link.empty())
    {
        return;
    }
    m_friendLinks[friendID] = link;
    ++m_rosterRevision;
    const CSteamID who((uint64)friendID);
    const std::string msg = "Join my game with Steam Remote Play: " + link;
    const bool sent = GClientContext()->SteamFriends()->ReplyToFriendMessage(who, msg.c_str());
    clientvt::Log("[rp] guest link for friend %llu sent in Steam chat: %s\n", (unsigned long long)friendID,
                  sent ? "yes" : "NO");
    if (!sent)
    {
        const char* persona = GClientContext()->SteamFriends()->GetFriendPersonaName(who);
        const std::string name = persona && *persona ? persona : "your friend";
        m_notice = "Steam didn't send the link to " + name + "'s chat. Use Copy link next to \"Waiting for " + name +
                   "\" and send it another way.";
    }
}

void RemotePlayInviteHandler::CancelWaitingInvite(uint64 steamID, uint32 guestID)
{
    for (size_t i = 0; i < m_invited.size(); ++i)
    {
        const RemotePlayPlayer_t sent = m_invited[i];
        const bool link = sent.m_playerID.GetAccountID() == 0;
        const uint64 who = link ? LinkInviteFriend(sent.m_unGuestID) : sent.m_playerID.ConvertToUint64();
        if (who != steamID || sent.m_unGuestID != guestID)
        {
            continue;
        }

        GClientContext()->RemoteClientManager()->CancelRemotePlayInviteAndSession(sent);
        m_invited.erase(m_invited.begin() + i);
        m_linkURLs.erase(sent.m_unGuestID);
        m_takenLinks.erase(sent.m_unGuestID);
        if (link)
        {
            m_linkFriends.erase(sent.m_unGuestID);
        }
        if (who)
        {
            m_friendLinks.erase(who);
        }
        ++m_rosterRevision;

        std::string notice = who ? "Invite cancelled." : "Guest link cancelled.";
        // Nothing left to share, so don't keep the screen shared for nobody.
        if (m_guests.empty() && m_invited.empty() && m_pending.empty())
        {
            EndSession();
            notice += " Nobody else was invited, so the session ended.";
        }
        m_notice = notice;
        return;
    }
}

void RemotePlayInviteHandler::SessionTick()
{
    if (!m_sessionGame || !IsSessionActive())
    {
        return;
    }

    SyncConnectedGuests(false);

    CGameID running = GetRunningGameID();
    if (running.IsValid() && running.ToUint64() == m_sessionGame)
    {
        m_sessionGameGoneTicks = 0;
        return;
    }

    // Give it ~3s in case Steam briefly reports no game (e.g. while an
    // emulator recreates its window).
    if (++m_sessionGameGoneTicks >= 15)
    {
        bool desktop = m_enabledDesktopStreaming;
        EndSession();
        m_notice = desktop ?
            "The game closed, so screen sharing stopped." :
            "The game closed, so the session ended.";
    }
}

std::string RemotePlayInviteHandler::TakeNotice()
{
    std::string notice;
    notice.swap(m_notice);
    return notice;
}

void RemotePlayInviteHandler::ResolveGroupFailure(bool sendAnyway)
{
    if (m_groupState != GroupFailed)
    {
        return;
    }

    if (sendAnyway)
    {
        // Stock RPW behaviour: invite with the default group id.
        m_groupID = 1;
        m_groupState = GroupReady;
        ++m_groupFallbacks;
    }
    else
    {
        m_pending.clear();
        m_groupState = GroupNone;
        // Nothing was sent, so don't leave the screen shared for nobody.
        if (m_guests.empty() && m_invited.empty())
        {
            EndSession();
        }
    }
}

void RemotePlayInviteHandler::SetNonSteamAppID(AppId_t appID)
{
    m_nonsteamAppID = appID;
}

void RemotePlayInviteHandler::SetGuestID(uint32 guestID)
{
    m_remoteGuestID = guestID;
}

// Enable the guest's chosen peripherals. SetPerUser* takes the exact
// RemotePlayPlayer_t Steam gave us on connect; on drifted builds this is the
// only way to hand a guest control (they never appear in the overlay list).
void RemotePlayInviteHandler::ApplyGuest(const ConnectedGuest& g)
{
    clientvt::Log("[rp] set input: steam=%llu guest=%u group=%u kbd=%d mouse=%d ctrl=%d\n",
                  (unsigned long long)g.player.m_playerID.ConvertToUint64(), g.player.m_unGuestID,
                  g.player.m_unOverlayPIDGroupID, (int)g.wantKbd, (int)g.wantMouse, (int)g.wantCtrl);
    CRemoteClientManagerByName* rcm = GClientContext()->RemoteClientManager();
    rcm->SetPerUserKeyboardInputEnabled(g.player, g.wantKbd);
    rcm->SetPerUserMouseInputEnabled(g.player, g.wantMouse);
    rcm->SetPerUserControllerInputEnabled(g.player, g.wantCtrl);
}

void RemotePlayInviteHandler::GrantControlTick()
{
    // Each guest's input is applied when they join and whenever the host
    // changes it. With friends joining the game's own group, that held for
    // the whole session in the Rammy-3 fork's tests, so it isn't applied again
    // every few seconds any more (which could also undo changes made in
    // Steam's own menu). Set RPW_REASSERT=1 for the old behaviour.
    static const bool reassert = std::getenv("RPW_REASSERT") != NULL;
    if (!reassert)
    {
        return;
    }
    for (size_t i = 0; i < m_guests.size(); ++i)
    {
        ApplyGuest(m_guests[i]);
    }
}

void RemotePlayInviteHandler::SetGuestInput(uint64 key, bool kbd, bool mouse, bool ctrl)
{
    for (size_t i = 0; i < m_guests.size(); ++i)
    {
        if (m_guests[i].key == key)
        {
            m_guests[i].wantKbd = kbd;
            m_guests[i].wantMouse = mouse;
            m_guests[i].wantCtrl = ctrl;
            m_guests[i].hasSaved = false;
            ApplyGuest(m_guests[i]);
            SaveGuestControls(m_guests[i].steamID, kbd, mouse, ctrl);
            ++m_rosterRevision;
            return;
        }
    }
}

static bool& Peripheral(ConnectedGuest& g, int which)
{
    if (which == 0) return g.wantKbd;
    if (which == 1) return g.wantMouse;
    return g.wantCtrl;
}

bool RemotePlayInviteHandler::AllGuestsHave(int which) const
{
    if (m_guests.empty())
    {
        return false;
    }
    for (size_t i = 0; i < m_guests.size(); ++i)
    {
        const ConnectedGuest& g = m_guests[i];
        bool on = which == 0 ? g.wantKbd : which == 1 ? g.wantMouse : g.wantCtrl;
        if (!on)
        {
            return false;
        }
    }
    return true;
}

void RemotePlayInviteHandler::SetAllGuestsInput(int which, bool on, bool remember)
{
    for (size_t i = 0; i < m_guests.size(); ++i)
    {
        ConnectedGuest& g = m_guests[i];
        Peripheral(g, which) = on;
        g.hasSaved = false;
        ApplyGuest(g);
        if (remember)
        {
            SaveGuestControls(g.steamID, g.wantKbd, g.wantMouse, g.wantCtrl);
        }
    }
    ++m_rosterRevision;
}

void RemotePlayInviteHandler::TakeAllControl()
{
    for (size_t i = 0; i < m_guests.size(); ++i)
    {
        ConnectedGuest& g = m_guests[i];
        if (!g.hasSaved)
        {
            g.savedKbd = g.wantKbd;
            g.savedMouse = g.wantMouse;
            g.savedCtrl = g.wantCtrl;
            g.hasSaved = true;
        }
        g.wantKbd = g.wantMouse = g.wantCtrl = false;
        ApplyGuest(g);
    }
    ++m_rosterRevision;
}

void RemotePlayInviteHandler::GiveBackControl()
{
    for (size_t i = 0; i < m_guests.size(); ++i)
    {
        ConnectedGuest& g = m_guests[i];
        if (g.hasSaved)
        {
            g.wantKbd = g.savedKbd;
            g.wantMouse = g.savedMouse;
            g.wantCtrl = g.savedCtrl;
            g.hasSaved = false;
        }
        else
        {
            g.wantKbd = g.wantMouse = g.wantCtrl = true;
        }
        ApplyGuest(g);
    }
    ++m_rosterRevision;
}

uint64 RemotePlayInviteHandler::GuestKey(const RemotePlayPlayer_t& player)
{
    if (player.m_playerID.GetAccountID() != 0)
    {
        return player.m_playerID.ConvertToUint64();
    }
    return 0xFFFFFFFF00000000ULL | player.m_unGuestID;
}

// Steam started a stream for one of our invites. For a guest link that's when
// the link is made, not when someone comes in with it (and a friend's may come
// before they accept), so this only notes whose stream it is; the guest is
// added once Steam lists the stream as connected (SyncConnectedGuests).
void RemotePlayInviteHandler::OnRemotePlayStart(RemoteClientStartStreamSession_t* cb)
{
    // (The struct's fields after m_GameID have moved in current Steam, so its
    // flags aren't logged.)
    clientvt::Log("[rp] stream start: session=%u steam=%llu guest=%u group=%u game=%llu | liveGroup=%u state=%d invited=%zu\n",
                  cb->m_unStreamingSessionID, (unsigned long long)cb->m_player.m_playerID.ConvertToUint64(),
                  cb->m_player.m_unGuestID, cb->m_player.m_unOverlayPIDGroupID,
                  (unsigned long long)cb->m_GameID.ToUint64(), m_groupID, (int)m_groupState, m_invited.size());

    // Our own stream isn't a guest.
    if (cb->m_player.m_playerID.GetAccountID() != 0 &&
        cb->m_player.m_playerID == GClientContext()->SteamUser()->GetSteamID())
    {
        return;
    }

    // No SteamID at all: not a guest but the host streaming to a device of
    // their own (the Steam Link app signed in to their account, which also
    // opens Big Picture mode).
    if (cb->m_player.m_playerID.ConvertToUint64() == 0)
    {
        clientvt::Log("[rp] stream %u is to the host's own device; not a guest\n", cb->m_unStreamingSessionID);
        return;
    }

    m_sessionPlayers[cb->m_unStreamingSessionID] = cb->m_player;

    // Without Steam's list of connected clients, fall back to taking a stream
    // start as a join (wrong for guest links, whose stream starts when the link
    // is made, so those are left out).
    if (!GClientContext()->SteamRemotePlay())
    {
        if (cb->m_player.m_playerID.GetAccountID() != 0)
        {
            AddGuest(cb->m_player, cb->m_unStreamingSessionID);
        }
        return;
    }
    SyncConnectedGuests(true);
}

void RemotePlayInviteHandler::OnSessionConnected(SteamRemotePlaySessionConnected_t* cb)
{
    clientvt::Log("[rp] Remote Play client connected: session=%u\n", cb->m_unSessionID);
    SyncConnectedGuests(true);
}

void RemotePlayInviteHandler::OnSessionDisconnected(SteamRemotePlaySessionDisconnected_t* cb)
{
    clientvt::Log("[rp] Remote Play client disconnected: session=%u\n", cb->m_unSessionID);
    GuestLeft(cb->m_unSessionID, nullptr);
}

void RemotePlayInviteHandler::SyncConnectedGuests(bool now)
{
    const long long ms = (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now().time_since_epoch()).count();
    if (!now && m_lastGuestSync && ms - m_lastGuestSync < 1000)
    {
        return;
    }
    m_lastGuestSync = ms;

    // Steam turns whole-screen sharing off whenever its last guest leaves, even
    // with invites or links still waiting, so it's turned back on for them.
    CRemoteClientManagerByName* rcm = GClientContext()->RemoteClientManager();
    if (m_enabledDesktopStreaming && !rcm->BStreamingDesktopToRemotePlayTogetherEnabled())
    {
        clientvt::Log("[rp] Steam turned whole-screen sharing off; turning it back on\n");
        rcm->SetStreamingDesktopToRemotePlayTogetherEnabled(true);
    }

    ISteamRemotePlay001* rp = GClientContext()->SteamRemotePlay();
    if (!rp)
    {
        return;
    }

    std::set<uint32> connected;
    const uint32 count = rp->GetSessionCount();
    for (uint32 i = 0; i < count && i < 64; ++i)
    {
        const uint32 id = rp->GetSessionID((int)i);
        if (id)
        {
            connected.insert(id);
        }
    }

    // Newly connected: the stream of one of our invites.
    const CSteamID me = GClientContext()->SteamUser()->GetSteamID();
    for (std::set<uint32>::const_iterator it = connected.begin(); it != connected.end(); ++it)
    {
        const uint32 id = *it;
        bool listed = false;
        for (size_t i = 0; i < m_guests.size() && !listed; ++i)
        {
            if (m_guests[i].sessionID == id)
            {
                m_guests[i].missingChecks = 0;
                listed = true;
            }
        }
        if (listed)
        {
            continue;
        }
        const CSteamID who = rp->GetSessionSteamID(id);
        if (who == me)
        {
            continue;       // the host's own device
        }
        std::map<uint32, RemotePlayPlayer_t>::const_iterator known = m_sessionPlayers.find(id);
        if (known != m_sessionPlayers.end())
        {
            const RemotePlayPlayer_t player = known->second;
            AddGuest(player, id);
            continue;
        }
        // A friend whose stream Steam hasn't reported: known by SteamID.
        for (size_t i = 0; who.GetAccountID() != 0 && i < m_invited.size(); ++i)
        {
            if (m_invited[i].m_playerID == who)
            {
                const RemotePlayPlayer_t player = m_invited[i];
                AddGuest(player, id);
                break;
            }
        }
    }

    // Gone without Steam saying so: not listed for 3 checks in a row.
    for (size_t i = m_guests.size(); i > 0; --i)
    {
        ConnectedGuest& g = m_guests[i - 1];
        if (!g.sessionID || g.sessionID == kFakeGuestSession || connected.count(g.sessionID))
        {
            continue;
        }
        if (++g.missingChecks >= 3)
        {
            const uint32 session = g.sessionID;
            const RemotePlayPlayer_t player = g.player;
            GuestLeft(session, &player);
        }
    }
}

// A guest connected. We have the RemotePlayPlayer_t of their stream (SteamID,
// guest id, group id), so we can grant control immediately.
void RemotePlayInviteHandler::AddGuest(const RemotePlayPlayer_t& player, uint32 sessionID)
{
    clientvt::Log("[rp] guest joined: session=%u steam=%llu guest=%u group=%u\n", sessionID,
                  (unsigned long long)player.m_playerID.ConvertToUint64(), player.m_unGuestID,
                  player.m_unOverlayPIDGroupID);
    uint64 key = GuestKey(player);

    for (size_t i = 0; i < m_guests.size(); ++i)
    {
        if (m_guests[i].key == key)
        {
            m_guests[i].player = player;  // refresh with latest struct
            m_guests[i].sessionID = sessionID;
            m_guests[i].missingChecks = 0;
            ApplyGuest(m_guests[i]);
            return;
        }
    }

    ConnectedGuest g;
    g.player = player;
    g.key = key;
    g.steamID = player.m_playerID.GetAccountID() != 0 ? player.m_playerID.ConvertToUint64() : 0;
    // Whoever comes in with a guest link sent to a friend in Steam chat is
    // taken to be that friend: their name, their remembered controls.
    if (!g.steamID)
    {
        g.steamID = LinkInviteFriend(player.m_unGuestID);
    }
    g.sessionID = sessionID;
    g.missingChecks = 0;
    g.wantKbd = true;
    g.wantMouse = true;
    g.wantCtrl = true;              // default: give a new guest full control
    // ...unless the host chose differently for this friend last time.
    LoadGuestControls(g.steamID, g.wantKbd, g.wantMouse, g.wantCtrl);
    g.joinedAt = (long long)std::time(nullptr);
    g.hasSaved = false;
    g.savedKbd = g.savedMouse = g.savedCtrl = false;
    m_guests.push_back(g);
    ++m_rosterRevision;
    if (!m_quiet)
    {
        PlayGuestSound(true);
    }
    // For the friends list's "Played with recently" group.
    SaveGuestJoined(g.steamID);
    // A guest link is used up once someone has come in with it.
    for (size_t i = 0; i < m_invited.size(); ++i)
    {
        if (m_invited[i].m_playerID.GetAccountID() == 0 && m_invited[i].m_unGuestID == g.player.m_unGuestID)
        {
            m_takenLinks.insert(g.player.m_unGuestID);
        }
    }

    if (g.steamID)
    {
        // Someone who joined through a guest link may not be a friend, so ask
        // Steam for their name for the control window.
        GClientContext()->SteamFriends()->RequestUserInformation(CSteamID((uint64)g.steamID), true);
    }
    ApplyGuest(g);
}

void RemotePlayInviteHandler::GuestLeft(uint32 sessionID, const RemotePlayPlayer_t* player)
{
    const uint64 key = player ? GuestKey(*player) : 0;
    for (size_t i = 0; i < m_guests.size(); ++i)
    {
        const ConnectedGuest& g = m_guests[i];
        if (g.sessionID == kFakeGuestSession || !((sessionID && g.sessionID == sessionID) || (key && g.key == key)))
        {
            continue;
        }
        clientvt::Log("[rp] guest left: session=%u steam=%llu guest=%u\n", g.sessionID,
                      (unsigned long long)g.player.m_playerID.ConvertToUint64(), g.player.m_unGuestID);
        m_guests.erase(m_guests.begin() + i);
        ++m_rosterRevision;
        if (!m_quiet)
        {
            PlayGuestSound(false);
        }
        break;
    }
    if (sessionID)
    {
        m_sessionPlayers.erase(sessionID);
    }
}

void RemotePlayInviteHandler::OnRemotePlayInviteResult(RemotePlayInviteResult_t* inviteResultCb)
{
    clientvt::Log("[rp] invite result: result=%d steam=%llu guest=%u group=%u url=%s\n",
                  (int)inviteResultCb->m_eResult,
                  (unsigned long long)inviteResultCb->m_player.m_playerID.ConvertToUint64(),
                  inviteResultCb->m_player.m_unGuestID, inviteResultCb->m_player.m_unOverlayPIDGroupID,
                  inviteResultCb->m_szConnectURL);

    // Whose answer this is, for the window, --serve and the command line, which
    // hear it after this handler (see ResultLinkFriend). Refined below.
    m_resultSlot = inviteResultCb->m_player.m_unGuestID;
    m_resultGroup = inviteResultCb->m_player.m_unOverlayPIDGroupID;
    m_resultCode = (int)inviteResultCb->m_eResult;
    m_resultFriend = inviteResultCb->m_player.m_playerID.GetAccountID() == 0 ? LinkInviteFriend(m_resultSlot) : 0;

    if (inviteResultCb->m_eResult == k_ERemoteClientLaunchResultOK)
    {
        if (inviteResultCb->m_player.m_playerID.IsValid())
        {
            // Steam sends a friend its own invite (in their chat), so a link of
            // ours would only be a second, worse invite. The link is kept for
            // the host to send another way if Steam's doesn't arrive. Set
            // RPW_SEND_LINK=1 to message the link as well, like before.
            const std::string link = ClickableURL(inviteResultCb->m_szConnectURL);
            if (!link.empty())
            {
                m_friendLinks[inviteResultCb->m_player.m_playerID.ConvertToUint64()] = link;
                ++m_rosterRevision;
            }
            if (std::getenv("RPW_SEND_LINK") != NULL)
            {
                std::string msg = "Follow this link to join remote game: " + link;
                GClientContext()->SteamFriends()->ReplyToFriendMessage(inviteResultCb->m_player.m_playerID, msg.c_str());
            }
        }
        else
        {
            // Which link this is: the guest slot Steam names, or else the
            // newest link invite that doesn't have its URL yet.
            uint32 slot = 0;
            for (size_t i = 0; i < m_invited.size(); ++i)
            {
                if (m_invited[i].m_playerID.GetAccountID() == 0 && inviteResultCb->m_player.m_unGuestID &&
                    m_invited[i].m_unGuestID == inviteResultCb->m_player.m_unGuestID)
                {
                    slot = m_invited[i].m_unGuestID;
                }
            }
            for (size_t i = m_invited.size(); i > 0 && !slot; --i)
            {
                const RemotePlayPlayer_t& sent = m_invited[i - 1];
                if (sent.m_playerID.GetAccountID() == 0 && !m_linkURLs.count(sent.m_unGuestID))
                {
                    slot = sent.m_unGuestID;
                }
            }
            if (slot)
            {
                m_linkURLs[slot] = ClickableURL(inviteResultCb->m_szConnectURL);
                ++m_rosterRevision;
                // A friend's invite for a game Steam doesn't run: send them the link.
                const uint64 friendID = LinkInviteFriend(slot);
                if (friendID)
                {
                    m_resultFriend = friendID;
                    SendLinkToFriend(friendID, m_linkURLs[slot]);
                }
            }
        }
        ShowSteamPanel(GClientContext()->RemoteClientManager(), GetRunningGameID().AppID());
    }
    else
    {
        // A failed or declined invite isn't part of the session any more. A
        // friend's invite is theirs by SteamID (it has no guest slot of its
        // own); a guest link is known by its slot.
        for (size_t i = 0; i < m_invited.size(); ++i)
        {
            const RemotePlayPlayer_t& sent = m_invited[i];
            const bool friendInvite = sent.m_playerID.GetAccountID() != 0;
            if (sent.m_playerID == inviteResultCb->m_player.m_playerID &&
                (friendInvite || sent.m_unGuestID == inviteResultCb->m_player.m_unGuestID ||
                 inviteResultCb->m_player.m_unGuestID == 0))
            {
                if (friendInvite)
                {
                    m_friendLinks.erase(sent.m_playerID.ConvertToUint64());
                }
                else if (LinkInviteFriend(sent.m_unGuestID))
                {
                    m_resultFriend = LinkInviteFriend(sent.m_unGuestID);
                    m_friendLinks.erase(m_resultFriend);
                }
                m_invited.erase(m_invited.begin() + i);
                ++m_rosterRevision;
                break;
            }
        }
        // Nothing left to share, so don't keep the screen shared for nobody.
        if (m_guests.empty() && m_invited.empty() && m_pending.empty())
        {
            EndSession();
        }
    }
}

void RemotePlayInviteHandler::OnRemotePlayStop(RemoteClientStopStreamSession_t* streamStopCb)
{
    clientvt::Log("[rp] stream stop: session=%u steam=%llu guest=%u group=%u\n",
                  streamStopCb->m_unStreamingSessionID,
                  (unsigned long long)streamStopCb->m_player.m_playerID.ConvertToUint64(),
                  streamStopCb->m_player.m_unGuestID, streamStopCb->m_player.m_unOverlayPIDGroupID);

    if (streamStopCb->m_player.m_playerID == GClientContext()->SteamUser()->GetSteamID())
    {
        // The whole session is gone; a future invite must create a fresh group.
        GClientContext()->RemoteClientManager()->SetStreamingDesktopToRemotePlayTogetherEnabled(false);
        ResetSession();
        return;
    }

    // A guest left (or the stream of an invite nobody used ended).
    GuestLeft(streamStopCb->m_unStreamingSessionID, &streamStopCb->m_player);
}
