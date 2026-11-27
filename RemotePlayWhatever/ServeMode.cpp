#include <wx/defs.h>
#include <wx/app.h>
#include <wx/config.h>
#include <wx/init.h>
#include <wx/time.h>
#include <wx/utils.h>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#ifdef __WXMSW__
#include <io.h>
#define rpw_dup _dup
#define rpw_dup2 _dup2
#define rpw_fdopen _fdopen
#define rpw_fileno _fileno
static const char* const kNullDevice = "NUL";
#else
#include <unistd.h>
#define rpw_dup dup
#define rpw_dup2 dup2
#define rpw_fdopen fdopen
#define rpw_fileno fileno
static const char* const kNullDevice = "/dev/null";
#endif
#include "SteamStuff.h"
#include "RemotePlayInviteHandler.h"
#include "GameInfo.h"
#include "ScreenShare.h"
#include "Updater.h"
#include "ServeMode.h"

namespace
{
    // The protocol's own copy of stdout; everything else printed goes to stderr.
    std::FILE* g_out = nullptr;

    std::string Json(const std::string& s)
    {
        std::string out = "\"";
        for (size_t i = 0; i < s.size(); ++i)
        {
            const unsigned char c = (unsigned char)s[i];
            switch (c)
            {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20)
                {
                    static const char kHex[] = "0123456789abcdef";
                    out += "\\u00";
                    out += kHex[c >> 4];
                    out += kHex[c & 15];
                }
                else
                {
                    out += (char)c;
                }
            }
        }
        return out + "\"";
    }

    std::string Json(const wxString& s) { return Json(std::string(s.utf8_str().data())); }
    std::string Json(const char* s) { return Json(std::string(s ? s : "")); }
    std::string Id(uint64 id) { return Json(std::to_string((unsigned long long)id)); }
    const char* Bool(bool b) { return b ? "true" : "false"; }

    void Emit(const std::string& line)
    {
        std::fputs(line.c_str(), g_out);
        std::fputc('\n', g_out);
        std::fflush(g_out);
    }

    void Error(const char* command, const char* error, const std::string& message)
    {
        std::string line = "{\"event\":\"error\"";
        if (command)
        {
            line += ",\"command\":" + Json(command);
        }
        Emit(line + ",\"error\":" + Json(error) + ",\"message\":" + Json(message) + "}");
    }

    std::string PersonaName(uint64 steamID)
    {
        const char* name = GClientContext()->SteamFriends()->GetFriendPersonaName(CSteamID((uint64)steamID));
        return name ? name : "";
    }

    std::string GuestName(const ConnectedGuest& g)
    {
        if (g.steamID)
        {
            return PersonaName(g.steamID);
        }
        return "Steam Link guest " + std::to_string((unsigned)g.player.m_unGuestID);
    }

    // Commands arrive on another thread (std::getline blocks); EOF is "quit".
    std::mutex g_lock;
    std::deque<std::string> g_commands;

    void ReadCommands()
    {
        std::string line;
        while (std::getline(std::cin, line))
        {
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            std::lock_guard<std::mutex> hold(g_lock);
            g_commands.push_back(line);
        }
        std::lock_guard<std::mutex> hold(g_lock);
        g_commands.push_back("quit");
    }

    class ServeSession
    {
    public:
        explicit ServeSession(RemotePlayInviteHandler* handler) :
            m_handler(handler),
            m_fallbacks(0),
            m_inviteResultCb(this, &ServeSession::OnInviteResult)
        {
        }

        void Status()
        {
            const CGameID game = GetRunningGameID();
            std::string line = "{\"event\":\"status\",\"game\":";
            if (game.IsValid())
            {
                const uint32 account = GClientContext()->SteamUser()->GetSteamID().GetAccountID();
                const bool steam = game.IsSteamApp() && game.AppID() != m_handler->GetNonSteamAppID();
                wxString name = ScreenShare::IsOurs(game) ? wxString("Your screen") : GetGameName(game, account);
                if (name.empty())
                {
                    name = steam ? "Your game" : "A non-Steam game";
                }
                line += "{\"id\":" + Id(game.ToUint64()) + ",\"name\":" + Json(name) + ",\"steam\":" + Bool(steam) + "}";
            }
            else
            {
                line += "null";
            }
            line += std::string(",\"screen\":") + Bool(m_handler->IsSharingDesktop());
            line += std::string(",\"session\":") + Bool(m_handler->IsSessionActive());
            line += ",\"link\":" + Json(m_handler->GuestLink());
            line += ",\"guests\":[";
            const std::vector<ConnectedGuest>& guests = m_handler->Guests();
            for (size_t i = 0; i < guests.size(); ++i)
            {
                const ConnectedGuest& g = guests[i];
                line += std::string(i ? "," : "") + "{\"key\":" + Id(g.key) + ",\"steamid\":" + Id(g.steamID) +
                        ",\"name\":" + Json(GuestName(g)) + ",\"keyboard\":" + Bool(g.wantKbd) + ",\"mouse\":" +
                        Bool(g.wantMouse) + ",\"controller\":" + Bool(g.wantCtrl) + ",\"joined\":" +
                        std::to_string(g.joinedAt) + "}";
            }
            line += "],\"waiting\":[";
            const std::vector<RemotePlayInviteHandler::WaitingInvite> waiting = m_handler->WaitingInvites();
            for (size_t i = 0; i < waiting.size(); ++i)
            {
                const RemotePlayInviteHandler::WaitingInvite& w = waiting[i];
                const std::string link = w.steamID ? m_handler->FriendLink(w.steamID) : m_handler->GuestLinkFor(w.guestID);
                line += std::string(i ? "," : "") + "{\"steamid\":" + Id(w.steamID) + ",\"guest\":" +
                        std::to_string((unsigned)w.guestID) + ",\"name\":" + Json(w.steamID ? PersonaName(w.steamID) : "") +
                        ",\"link\":" + Json(link) + "}";
            }
            Emit(line + "]}");
        }

        void Friends()
        {
            static const char* const kStates[] = { "offline", "online", "busy", "away", "snooze", "trade", "play" };
            ISteamFriends015* friends = GClientContext()->SteamFriends();
            const int count = friends->GetFriendCount(k_EFriendFlagImmediate);
            std::string line = "{\"event\":\"friends\",\"friends\":[";
            for (int i = 0; i < count; ++i)
            {
                const CSteamID id = friends->GetFriendByIndex(i, k_EFriendFlagImmediate);
                const int state = (int)friends->GetFriendPersonaState(id);
                FriendGameInfo_t info;
                const std::string game = friends->GetFriendGamePlayed(id, &info) && info.m_gameID.IsValid() ?
                                         std::string(GetFriendGameName(info.m_gameID).utf8_str().data()) : std::string();
                line += std::string(i ? "," : "") + "{\"steamid\":" + Id(id.ConvertToUint64()) + ",\"name\":" +
                        Json(PersonaName(id.ConvertToUint64())) + ",\"state\":" +
                        Json(state >= 0 && state < 7 ? kStates[state] : "online") + ",\"game\":" + Json(game) + "}";
            }
            Emit(line + "]}");
        }

        // Guests coming and going, notices and fallbacks, since the last tick.
        void Changes()
        {
            std::map<uint64, std::pair<uint64, std::string> > now;
            const std::vector<ConnectedGuest>& guests = m_handler->Guests();
            for (size_t i = 0; i < guests.size(); ++i)
            {
                now[guests[i].key] = std::make_pair(guests[i].steamID, GuestName(guests[i]));
                if (!m_known.count(guests[i].key))
                {
                    Emit("{\"event\":\"joined\",\"key\":" + Id(guests[i].key) + ",\"steamid\":" + Id(guests[i].steamID) +
                         ",\"name\":" + Json(now[guests[i].key].second) + "}");
                }
            }
            for (auto it = m_known.begin(); it != m_known.end(); ++it)
            {
                if (!now.count(it->first))
                {
                    Emit("{\"event\":\"left\",\"key\":" + Id(it->first) + ",\"steamid\":" + Id(it->second.first) +
                         ",\"name\":" + Json(it->second.second) + "}");
                }
            }
            m_known.swap(now);

            const std::string notice = m_handler->TakeNotice();
            if (!notice.empty())
            {
                Emit("{\"event\":\"notice\",\"message\":" + Json(notice) + "}");
            }
            if (m_handler->GroupFallbacks() != m_fallbacks)
            {
                m_fallbacks = m_handler->GroupFallbacks();
                Emit("{\"event\":\"group-failed\",\"message\":" +
                     Json("Steam didn't set up a Remote Play group for this game, so the invite was sent anyway: "
                          "guests may only be able to watch. Restarting the game through Steam usually helps.") + "}");
            }
        }

        // One command; false to stop serving.
        bool Run(const std::string& line)
        {
            std::istringstream in(line);
            std::string command;
            in >> command;
            if (command.empty())
            {
                return true;
            }
            if (command == "status")
            {
                Status();
            }
            else if (command == "friends")
            {
                Friends();
            }
            else if (command == "link" || command == "invite")
            {
                unsigned long long steamID = 0;
                if (command == "invite" && (!(in >> steamID) || !CSteamID((uint64)steamID).IsValid()))
                {
                    Error(command.c_str(), "bad-argument", "invite needs a friend's SteamID64.");
                }
                else if (!GetRunningGameID().IsValid())
                {
                    Error(command.c_str(), "no-game", "Start a game through Steam first.");
                }
                else
                {
                    m_handler->SendInvite(command == "link" ? RemotePlayInviteHandler::GuestLinkInvitee()
                                                            : CSteamID((uint64)steamID));
                }
            }
            else if (command == "cancel")
            {
                unsigned long long steamID = 0;
                unsigned guest = 0;
                if (in >> steamID >> guest)
                {
                    m_handler->CancelWaitingInvite((uint64)steamID, (uint32)guest);
                    Status();
                }
                else
                {
                    Error("cancel", "bad-argument", "cancel needs a SteamID64 (0 for a guest link) and a guest id.");
                }
            }
            else if (command == "input")
            {
                unsigned long long key = 0;
                int kbd = 0, mouse = 0, ctrl = 0;
                if (in >> key >> kbd >> mouse >> ctrl)
                {
                    m_handler->SetGuestInput((uint64)key, kbd != 0, mouse != 0, ctrl != 0);
                    Status();
                }
                else
                {
                    Error("input", "bad-argument", "input needs a guest key and three 0/1 values.");
                }
            }
            else if (command == "remove")
            {
                unsigned long long key = 0;
                if (in >> key)
                {
                    m_handler->RemoveGuest((uint64)key);
                    Status();
                }
                else
                {
                    Error("remove", "bad-argument", "remove needs a guest key.");
                }
            }
            else if (command == "end")
            {
                m_handler->EndSession();
                Emit("{\"event\":\"ended\"}");
            }
            else if (command == "quit")
            {
                Emit("{\"event\":\"bye\"}");
                return false;
            }
            else
            {
                Error(command.c_str(), "unknown-command", "Unknown command: " + command);
            }
            return true;
        }

    private:
        RemotePlayInviteHandler* m_handler;
        std::map<uint64, std::pair<uint64, std::string> > m_known;
        uint32 m_fallbacks;
        STEAM_CALLBACK(ServeSession, OnInviteResult, RemotePlayInviteResult_t, m_inviteResultCb);
    };

    void ServeSession::OnInviteResult(RemotePlayInviteResult_t* cb)
    {
        uint64 steamID = cb->m_player.m_playerID.GetAccountID() != 0 ? cb->m_player.m_playerID.ConvertToUint64() : 0;
        // A friend's invite for a game Steam doesn't run is a guest link sent to them.
        if (!steamID)
        {
            steamID = m_handler->ResultLinkFriend(cb);
        }
        std::string line = "{\"event\":\"invite\",\"steamid\":" + Id(steamID) + ",\"guest\":" +
                           std::to_string((unsigned)cb->m_player.m_unGuestID);
        if (cb->m_eResult == k_ERemoteClientLaunchResultOK)
        {
            line += ",\"result\":\"ok\",\"url\":" + Json(ClickableURL(cb->m_szConnectURL)) + "}";
        }
        else
        {
            line += ",\"result\":\"failed\",\"code\":" + std::to_string((int)cb->m_eResult) + ",\"message\":" +
                    Json(LaunchResultMessage(cb->m_eResult)) + "}";
        }
        Emit(line);
    }

    wxAppConsole* CreateServeApp()
    {
        return new wxAppConsole;
    }
}

int RunServeMode(int argc, char** argv, unsigned appID)
{
    // The protocol gets its own copy of stdout; stdout itself (std::cout,
    // printf) then goes to stderr, so nothing else can end up in the protocol.
    const int protocolFd = rpw_dup(rpw_fileno(stdout));
    g_out = protocolFd >= 0 ? rpw_fdopen(protocolFd, "w") : nullptr;
    if (!g_out)
    {
        return 2;
    }
    std::fflush(stdout);
    // POSIX dup2 returns the new descriptor, Windows' _dup2 0; both -1 on failure.
    if (rpw_dup2(rpw_fileno(stderr), rpw_fileno(stdout)) < 0)
    {
        if (!std::freopen(kNullDevice, "w", stdout))
        {
            return 2;
        }
    }

    // wxWidgets without its GUI: the app object is a plain console one (no
    // display needed), named like the app so the same settings are used.
    wxApp::SetInitializerFunction(CreateServeApp);
    wxInitializer wx(argc, argv);
    if (!wx.IsOk())
    {
        Emit("{\"event\":\"error\",\"error\":\"init\",\"message\":\"wxWidgets didn't start.\"}");
        return 1;
    }
    wxTheApp->SetAppName("RemotePlayWhatever");

    if (!GClientContext()->Init())
    {
        switch (GClientContext()->InitError())
        {
        case ClientContext::InitNotSignedIn:
            Error(nullptr, "not-signed-in", "Steam is running, but nobody is signed in.");
            break;
        case ClientContext::InitMethodsMissing:
            Error(nullptr, "methods-missing", "A Steam update changed the internal Remote Play functions this app uses; "
                                              "Remote Play Whatever needs an update.");
            break;
        default:
            Error(nullptr, "steam-not-running", "Steam isn't running.");
            break;
        }
        return 1;
    }

    RemotePlayInviteHandler* handler = new RemotePlayInviteHandler();
    handler->SetNonSteamAppID(appID);
    handler->SetAutoFallback(true);     // nobody to ask; group-failed says so
    handler->SetQuiet(true);
    int result = 0;
    {
        ServeSession session(handler);
        const CSteamID me = GClientContext()->SteamUser()->GetSteamID();
        const char* name = GClientContext()->SteamFriends()->GetPersonaName();
        Emit("{\"event\":\"ready\",\"protocol\":1,\"build\":" + std::to_string((long long)AppUpdater::CurrentBuild()) +
             ",\"steamid\":" + Id(me.ConvertToUint64()) + ",\"name\":" + Json(name) + "}");

        std::thread reader(ReadCommands);
        reader.detach();

        bool running = true;
        wxLongLong nextTick = 0;
        while (running)
        {
            std::deque<std::string> commands;
            {
                std::lock_guard<std::mutex> hold(g_lock);
                commands.swap(g_commands);
            }
            for (size_t i = 0; i < commands.size() && running; ++i)
            {
                running = session.Run(commands[i]);
            }
            const wxLongLong now = wxGetLocalTimeMillis();
            if (running && now >= nextTick)
            {
                nextTick = now + 200;
                GClientContext()->RunCallbacks();
                handler->GroupTick();
                handler->SessionTick();
                session.Changes();
            }
            if (running)
            {
                wxMilliSleep(40);
            }
        }
    }

    wxConfigBase* config = wxConfigBase::Get(false);
    if (config)
    {
        config->Flush();
    }
    delete handler;
    GClientContext()->Shutdown();
    std::fflush(g_out);
    std::fflush(stderr);
    // The command reader may still be blocked reading stdin; leave without
    // running static destructors under it.
    std::_Exit(result);
}
