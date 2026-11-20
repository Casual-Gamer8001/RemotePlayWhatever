#include <wx/clipbrd.h>
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
    m_pClientEngine(nullptr),
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

// helper functions

ClientContext* GClientContext()
{
    static ClientContext ctx;
    return &ctx;
}

CGameID GetRunningGameID()
{
    FriendGameInfo_t gameInfo;
    if(GClientContext()->SteamFriends()->GetFriendGamePlayed(GClientContext()->SteamUser()->GetSteamID(), &gameInfo))
    {
        return gameInfo.m_gameID;
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


