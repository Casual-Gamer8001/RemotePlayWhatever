#include <cstdlib>
#include <wx/clipbrd.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include "GameInfo.h"
#include "SteamStuff.h"

// steam client context

ClientContext::ClientContext():
    m_hPipe(0),
    m_hUser(0),
    m_pSteamClient(nullptr),
    m_pSteamUser(nullptr),
    m_pSteamFriends(nullptr),
    m_pSteamUtils(nullptr),
    m_pSteamApps001(nullptr),
    m_pSteamRemotePlay(nullptr),
    m_pClientEngine(nullptr),
    m_shortcutsTried(false),
    m_ShuttingDown(false),
    m_Initialized(false),
    m_initError(InitOK)
{
}

ClientContext::~ClientContext()
{
}

bool ClientContext::Init()
{
    if(m_Initialized)
    {
        return true;
    }

    // Until something more specific is known, failing means Steam isn't there.
    m_initError = InitSteamNotRunning;

    // A steam_appid.txt next to this program makes Steam connect it as that
    // game and refuse it ("nobody is signed in"). This app is no Steam game and
    // never has one; Steam itself can leave one behind when it fails to start
    // a non-Steam game (a screen share), so it goes.
    static bool appIdFileChecked = false;
    if (!appIdFileChecked)
    {
        appIdFileChecked = true;
        const wxFileName appIdFile(wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath(), "steam_appid.txt");
        if (appIdFile.FileExists())
        {
            const bool removed = wxRemoveFile(appIdFile.GetFullPath());
            clientvt::Log("[rp] steam_appid.txt next to the program: %s\n", removed ? "removed" : "COULDN'T REMOVE");
        }
    }

    // Steam's library only needs loading once: when Steam isn't running yet
    // (the app started with Windows), Init() is simply called again later.
    if (!m_pSteamClient)
    {
        if (!OpenAPI_LoadLibrary())
        {
            return false;
        }
        m_pSteamClient = (ISteamClient019*)SteamInternal_CreateInterface(STEAMCLIENT_INTERFACE_VERSION_019);
        if (!m_pSteamClient)
        {
            return false;
        }
    }

    m_hPipe = m_pSteamClient->CreateSteamPipe();
    if (!m_hPipe)
    {
        return false;
    }

    m_hUser = m_pSteamClient->ConnectToGlobalUser(m_hPipe);
    if (!m_hUser)
    {
        m_initError = InitNotSignedIn;
        // Nobody to talk to yet; don't keep a pipe open for the next try.
        m_pSteamClient->BReleaseSteamPipe(m_hPipe);
        m_hPipe = 0;
        return false;
    }

    m_initError = InitMethodsMissing;

    m_pSteamUser = (ISteamUser019*)m_pSteamClient->GetISteamUser(m_hUser, m_hPipe, STEAMUSER_INTERFACE_VERSION_019);
    if (!m_pSteamUser)
    {
        return false;
    }

    m_pSteamFriends = (ISteamFriends015*)m_pSteamClient->GetISteamFriends(m_hUser, m_hPipe, STEAMFRIENDS_INTERFACE_VERSION_015);
    if (!m_pSteamFriends)
    {
        return false;
    }

    m_pSteamUtils = (ISteamUtils009*)m_pSteamClient->GetISteamUtils(m_hPipe, STEAMUTILS_INTERFACE_VERSION_009);
    if(!m_pSteamUtils)
    {
        return false;
    }

    // Optional (game names in the friends list): the oldest SteamApps interface,
    // kept for old games, still reads Steam's own information about any app.
    m_pSteamApps001 = (ISteamApps001*)m_pSteamClient->GetISteamApps(m_hUser, m_hPipe, STEAMAPPS_INTERFACE_VERSION_001);

    // Optional too: which Remote Play clients are connected. A guest who comes
    // in with a guest link is only reported here (the Remote Play manager's
    // stream-start callback comes when the link is made, not when it's used).
    m_pSteamRemotePlay = (ISteamRemotePlay001*)m_pSteamClient->GetISteamRemotePlay(m_hUser, m_hPipe,
                                                                                   STEAMREMOTEPLAY_INTERFACE_VERSION);

    m_pClientEngine = (IClientEngine*)SteamInternal_CreateInterface(CLIENTENGINE_INTERFACE_VERSION);
    if (!m_pClientEngine)
    {
        return false;
    }

    // IClientEngine's methods carry no names in Steam's code (checked with Steam
    // 1794536281), so the Remote Play manager is still fetched by position:
    // GetIClientRemoteClientManager(pipe) is slot 56 in OpenSteamworks'
    // declaration. The call is guarded (see clientvt::CallPipeGetterAt), so if
    // Steam ever reorders the engine this fails as "Steam changed its
    // functions" (and the app offers an update) instead of crashing.
    static const int kRemoteClientManagerSlot = 56;
    void* pRemoteManager = NULL;
    if (!clientvt::CallPipeGetterAt(m_pClientEngine, kRemoteClientManagerSlot, m_hPipe, &pRemoteManager))
    {
        clientvt::Log("[vt] Init: IClientEngine slot %d didn't give the Remote Play manager\n",
                      kRemoteClientManagerSlot);
        return false;
    }

    // IClientRemoteClientManager is the interface that actually drifts between
    // builds (Valve keeps adding Remote Play methods), so instead of calling it
    // through the header's fixed vtable order we resolve each method by name and
    // call through the resolved index. Confirm the methods we use exist on this
    // build before anything calls through the proxy.
    m_remoteManager.Bind(pRemoteManager);
    m_remoteManager.DebugDump(); // no-op unless RPW_VTDEBUG is set
    clientvt::Log("[rp] ---- RemotePlayWhatever started ----\n"
                  "[rp] methods: createInvite=%d forOverlayPID=%d localGroup=%d createGroup=%d guestID=%d\n",
                  (int)m_remoteManager.Has("BCreateRemotePlayInviteAndSession"),
                  (int)m_remoteManager.Has("GetRemotePlayTogetherGroupIDForOverlayPID"),
                  (int)m_remoteManager.Has("GetLocalRemotePlayTogetherGroupID"),
                  (int)m_remoteManager.Has("BCreateRemotePlayGroup"),
                  (int)m_remoteManager.Has("GetAvailableRemotePlayTogetherGuestID"));
    if (!m_remoteManager.Ok()
        || !m_remoteManager.Has("BCreateRemotePlayInviteAndSession")
        || !m_remoteManager.Has("ShowRemotePlayTogetherUI"))
    {
        clientvt::Log("[vt] Init: RCM method resolution FAILED "
                      "(ok=%d count=%zu) — refusing to call into wrong slots\n",
                      (int)m_remoteManager.Ok(), m_remoteManager.Count());
        return false;
    }

    m_initError = InitOK;
    return m_Initialized = true;
}

void ClientContext::Shutdown()
{
    if(m_ShuttingDown)
    {
        return;
    }

    m_ShuttingDown = true;

    if (m_hPipe)
    {
        if (m_hUser)
        {
            m_pSteamClient->ReleaseUser(m_hPipe, m_hUser);
        }
        if(m_pSteamClient->BReleaseSteamPipe(m_hPipe))
        {
            m_pSteamClient->BShutdownIfAllPipesClosed();
        }
    }
}

ISteamUser019* ClientContext::SteamUser()
{
    return m_pSteamUser;
}

ISteamFriends015* ClientContext::SteamFriends()
{
    return m_pSteamFriends;
}

ISteamUtils009 *ClientContext::SteamUtils()
{
    return m_pSteamUtils;
}

ISteamRemotePlay001* ClientContext::SteamRemotePlay()
{
    return m_pSteamRemotePlay;
}

std::string ClientContext::AppName(AppId_t appID)
{
    if (!m_pSteamApps001 || !appID)
    {
        return std::string();
    }
    static const char* const keys[] = { "name", "common/name" };
    for (const char* key : keys)
    {
        char name[256] = {};
        if (m_pSteamApps001->GetAppData(appID, key, name, sizeof(name)) > 0 && name[0])
        {
            name[sizeof(name) - 1] = '\0';
            return std::string(name);
        }
    }
    return std::string();
}

void ClientContext::RunCallbacks()
{
    if(!m_ShuttingDown)
    {
        Steam_RunCallbacks(m_hPipe, false);
    }
}

CRemoteClientManagerByName* ClientContext::RemoteClientManager()
{
    return &m_remoteManager;
}

CShortcutsByName* ClientContext::Shortcuts()
{
    // GetIClientShortcuts(user, pipe) is slot 58 in OpenSteamworks' IClientEngine
    // declaration (checked in Steam 11.23.65.88: it takes both and refuses 0).
    // Fetched under the same guard as the Remote Play manager, and only once.
    static const int kShortcutsSlot = 58;
    if (!m_shortcutsTried && m_Initialized && m_pClientEngine)
    {
        m_shortcutsTried = true;
        void* shortcuts = NULL;
        if (clientvt::CallUserPipeGetterAt(m_pClientEngine, kShortcutsSlot, m_hUser, m_hPipe, &shortcuts))
        {
            m_shortcuts.Bind(shortcuts);
        }
        clientvt::Log("[rp] shortcuts interface: %s (%zu named methods, temporary shortcuts %s)\n",
                      shortcuts ? "found" : "not found", m_shortcuts.Count(),
                      m_shortcuts.CanAddTemporary() ? "possible" : "not possible");
    }
    return &m_shortcuts;
}

// helper functions

ClientContext* GClientContext()
{
    static ClientContext ctx;
    return &ctx;
}

CGameID GetRunningGameID()
{
    // For tests of the second way below: RPW_TEST_NO_PLAYED_GAME skips the first.
    static const bool skipPlayed = std::getenv("RPW_TEST_NO_PLAYED_GAME") != NULL;
    FriendGameInfo_t gameInfo;
    if(!skipPlayed &&
       GClientContext()->SteamFriends()->GetFriendGamePlayed(GClientContext()->SteamUser()->GetSteamID(), &gameInfo))
    {
        return gameInfo.m_gameID;
    }

    // Steam doesn't show every game it starts as the one you're playing (games
    // added to Steam with other tools, for one), so ask which game it started.
    const AppId_t app = GetSteamRunningAppID();
    if (app)
    {
        static AppId_t logged = 0;
        if (app != logged)
        {
            logged = app;
            clientvt::Log("[rp] running game %u found through Steam's RunningAppID (not shown as played)\n",
                          (unsigned)app);
        }
        return CGameID(app);
    }

    return CGameID();
}

std::string ClickableURL(const char* url)
{
    std::string link = url ? url : "";
    if (!link.empty() && link.find("://") == std::string::npos)
    {
        link = "https://" + link;
    }
    return link;
}

std::string LaunchResultMessage(ERemoteClientLaunchResult result)
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
    case k_ERemoteClientLaunchResultInvisible:          return "Steam doesn't send Remote Play Together invites while you're Invisible. "
                                                               "Set your Steam status to Online, or use a guest link.";
    case k_ERemoteClientLaunchResultRestrictedCountry:  return "Steam says Remote Play Together isn't available in your friend's country.";
    default:                                            return "Steam couldn't start the session.";
    }
}


