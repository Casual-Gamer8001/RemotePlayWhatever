#ifndef STEAMSTUFF_H
#define STEAMSTUFF_H

#include <string>
#include <Steamworks.h>
#include "ClientVTableByName.h"

// IClientRemoteClientManager drifts between Steam builds, so instead of calling
// it through the compiler-fixed vtable order we resolve each method by name at
// runtime (see ClientVTableByName.h) and call through the resolved index. The
// method set below is exactly what this app uses; the call sites are unchanged.
//
// Every wrapper checks Has() first: calling a name Steam no longer has would
// otherwise jump to vtable slot 0 with the wrong arguments. A missing method
// becomes a no-op (or false/0) instead of a crash.
class CRemoteClientManagerByName : public clientvt::NamedVTable
{
public:
    bool BCreateRemotePlayInviteAndSession(RemotePlayPlayer_t player, AppId_t appID)
    {
        return Has("BCreateRemotePlayInviteAndSession") &&
               Invoke<bool>("BCreateRemotePlayInviteAndSession", player, appID);
    }
    void CancelRemotePlayInviteAndSession(RemotePlayPlayer_t player)
    {
        // Current builds embed this method's name. Older builds didn't, so fall
        // back to the slot between its two named neighbours
        // BCreateRemotePlayInviteAndSession and JoinRemotePlaySession
        // (…Create, Cancel, Join…). If those anchors don't bracket exactly one
        // slot, the layout changed and we skip rather than call the wrong method.
        if (Has("CancelRemotePlayInviteAndSession"))
        {
            Invoke<void>("CancelRemotePlayInviteAndSession", player);
            return;
        }
        int bc = IndexOf("BCreateRemotePlayInviteAndSession");
        int jn = IndexOf("JoinRemotePlaySession");
        if (bc >= 0 && jn == bc + 2)
        {
            InvokeAt<void>(bc + 1, player);
        }
    }
    void StopStreamingSession(uint32 sessionID)
    {
        if (Has("StopStreamingSession"))
        {
            Invoke<void>("StopStreamingSession", sessionID);
        }
    }
    bool BIsStreamingSessionActive()
    {
        return Has("BIsStreamingSessionActive") && Invoke<bool>("BIsStreamingSessionActive");
    }
    bool BStreamingDesktopToRemotePlayTogetherEnabled()
    {
        return Has("BStreamingDesktopToRemotePlayTogetherEnabled") &&
               Invoke<bool>("BStreamingDesktopToRemotePlayTogetherEnabled");
    }
    void SetStreamingDesktopToRemotePlayTogetherEnabled(bool enabled)
    {
        if (Has("SetStreamingDesktopToRemotePlayTogetherEnabled"))
        {
            Invoke<void>("SetStreamingDesktopToRemotePlayTogetherEnabled", enabled);
        }
    }
    void ShowRemotePlayTogetherUI(AppId_t appID)
    {
        if (Has("ShowRemotePlayTogetherUI"))
        {
            Invoke<void>("ShowRemotePlayTogetherUI", appID);
        }
    }
    // --- Remote Play Together group + per-user input control ---------------
    // On drifted builds the guest never appears in the Steam overlay player
    // list, so granting control through the overlay is impossible; the only
    // way to give a guest input is to enable it per-user programmatically.
    //
    // Steam keeps one Remote Play Together group per host and game. Creating
    // and looking up "the local group" both take the game as a 32-bit ID (the
    // app ID for a Steam game); 0 is a group for no game at all, where a
    // guest's mouse movement never reaches the game (found by the Rammy-3
    // fork). The ForOverlayPID lookup takes a process ID and returns the group
    // of the game it runs. Each of these takes one 4-byte argument (checked in
    // steamclient64.dll 11.23.65.88's IClientRemoteClientManager stubs).
    uint32 GetLocalRemotePlayTogetherGroupID(uint32 app = 0)
    {
        return Has("GetLocalRemotePlayTogetherGroupID") ?
               Invoke<uint32>("GetLocalRemotePlayTogetherGroupID", app) : 0;
    }
    uint32 GetRemotePlayTogetherGroupIDForOverlayPID(uint32 pid)
    {
        return Has("GetRemotePlayTogetherGroupIDForOverlayPID") ?
               Invoke<uint32>("GetRemotePlayTogetherGroupIDForOverlayPID", pid) : 0;
    }
    bool BCreateRemotePlayGroup(uint32 app = 0)
    {
        return Has("BCreateRemotePlayGroup") && Invoke<bool>("BCreateRemotePlayGroup", app);
    }
    uint32 GetAvailableRemotePlayTogetherGuestID(uint32 group)
    {
        return Has("GetAvailableRemotePlayTogetherGuestID") ?
               Invoke<uint32>("GetAvailableRemotePlayTogetherGuestID", group) : 0;
    }
    // These take the 40-byte RemotePlayPlayer_t by value (passed by hidden
    // pointer per the x64 ABI) — NOT (group,guest) ints, which crashes.
    void SetPerUserKeyboardInputEnabled(RemotePlayPlayer_t player, bool enabled)
    {
        if (Has("SetPerUserKeyboardInputEnabled"))
        {
            Invoke<void>("SetPerUserKeyboardInputEnabled", player, enabled);
        }
    }
    void SetPerUserMouseInputEnabled(RemotePlayPlayer_t player, bool enabled)
    {
        if (Has("SetPerUserMouseInputEnabled"))
        {
            Invoke<void>("SetPerUserMouseInputEnabled", player, enabled);
        }
    }
    void SetPerUserControllerInputEnabled(RemotePlayPlayer_t player, bool enabled)
    {
        if (Has("SetPerUserControllerInputEnabled"))
        {
            Invoke<void>("SetPerUserControllerInputEnabled", player, enabled);
        }
    }
    void DisbandRemotePlayTogetherGroup(uint32 group)
    {
        if (Has("DisbandRemotePlayTogetherGroup"))
        {
            Invoke<void>("DisbandRemotePlayTogetherGroup", group);
        }
    }
    // Steam's panel calls this (no arguments) right after it moves a
    // controller to another player slot.
    void OnRemotePlayUIMovedController()
    {
        if (Has("OnRemotePlayUIMovedController"))
        {
            Invoke<void>("OnRemotePlayUIMovedController");
        }
    }
};

// IClientShortcuts (Steam's non-Steam game shortcuts), resolved by name like
// the Remote Play manager, with only what "Share my screen" needs: a temporary
// shortcut (Steam forgets it when it restarts) that Steam starts as a game.
// The arguments of each were checked in steamclient64.dll 11.23.65.88's
// IClientShortcutsMap stubs: three strings -> a 4-byte app ID; app ID + string
// (the setters); app ID. Its LaunchShortcut is deliberately not used: it
// doesn't start the program, but writes a steam_appid.txt next to it, after
// which Steam refuses that program's connections ("nobody is signed in").
// The shortcut is started like any game, with steam://rungameid/. Its program
// and folder go in without quotes: Steam would keep them as part of the path.
class CShortcutsByName : public clientvt::NamedVTable
{
public:
    bool CanAddTemporary() const
    {
        return Ok() && Has("AddTemporaryShortcut") && Has("SetShortcutCommandLine") && Has("SetShortcutStartDir") &&
               Has("RemoveShortcut");
    }
    // name, program, icon -> the shortcut's app ID (0: none was made).
    uint32 AddTemporaryShortcut(const char* name, const char* exe, const char* icon)
    {
        return Has("AddTemporaryShortcut") ? Invoke<uint32>("AddTemporaryShortcut", name, exe, icon) : 0;
    }
    // The shortcut's launch options.
    void SetShortcutCommandLine(uint32 appID, const char* commandLine)
    {
        if (Has("SetShortcutCommandLine"))
        {
            Invoke<void>("SetShortcutCommandLine", appID, commandLine);
        }
    }
    // The folder the program starts in. Without one, Steam starts it in its
    // own folder.
    void SetShortcutStartDir(uint32 appID, const char* dir)
    {
        if (Has("SetShortcutStartDir"))
        {
            Invoke<void>("SetShortcutStartDir", appID, dir);
        }
    }
    void RemoveShortcut(uint32 appID)
    {
        if (Has("RemoveShortcut"))
        {
            Invoke<void>("RemoveShortcut", appID);
        }
    }
};

// IClientControllerSerialized (Steam Input), resolved by name like the others,
// with what moving a controller to another player needs. Steam's Remote Play
// Together panel does it like this (SteamUI.dll, Steam 1794536281): read the
// counter, swap the two slots with it (the swap is refused if the controllers
// changed in between), then tell the Remote Play manager
// (OnRemotePlayUIMovedController); and it rumbles a slot's controller to show
// whose it is. A "gamepad index" is a player slot (0 = player 1); a
// "controller index" is Steam's number for the device itself. The arguments of
// each were checked in steamclient64.dll 11.23.65.88's
// IClientControllerSerializedMap stubs: the counter is 4 bytes; the swap takes
// it and two 4-byte slots and returns a 1-byte bool; the other two take one
// 4-byte number (the first returns 4 bytes).
class CControllerSerializedByName : public clientvt::NamedVTable
{
public:
    bool CanMove() const
    {
        return Ok() && Has("GetGamepadIndexChangeCounter") && Has("BSwapGamepadIndex");
    }
    uint32 GetGamepadIndexChangeCounter() const
    {
        return Has("GetGamepadIndexChangeCounter") ? Invoke<uint32>("GetGamepadIndexChangeCounter") : 0;
    }
    bool BSwapGamepadIndex(uint32 counter, int32 slotA, int32 slotB) const
    {
        return Has("BSwapGamepadIndex") && Invoke<bool>("BSwapGamepadIndex", counter, slotA, slotB);
    }
    // -1 if no controller has that slot.
    int32 GetControllerIndexForGamepadIndex(int32 slot) const
    {
        return Has("GetControllerIndexForGamepadIndex") ? Invoke<int32>("GetControllerIndexForGamepadIndex", slot) : -1;
    }
    void IdentifyControllerRumbleEffect(int32 controllerIndex) const
    {
        if (Has("IdentifyControllerRumbleEffect"))
        {
            Invoke<void>("IdentifyControllerRumbleEffect", controllerIndex);
        }
    }
};

class ClientContext
{
public:
    // Why Init() failed, so the app can say something more useful than
    // "could not initialize".
    enum EInitError
    {
        InitOK,
        InitSteamNotRunning,    // Steam isn't installed or isn't running
        InitNotSignedIn,        // Steam is running but nobody is signed in
        InitMethodsMissing,     // Steam changed the functions this app needs
    };

    ClientContext();
    ~ClientContext();

    ISteamUser019* SteamUser();
    ISteamFriends015* SteamFriends();
    ISteamUtils009* SteamUtils();
    // Steam's public Remote Play interface: which Remote Play clients are
    // connected right now (null if this Steam doesn't offer it).
    ISteamRemotePlay001* SteamRemotePlay();

    // Steam's name for any game it has information about (including ones that
    // aren't installed here), or empty. Uses the public SteamApps001 interface.
    std::string AppName(AppId_t appID);

    CRemoteClientManagerByName* RemoteClientManager();
    // Steam's shortcuts interface, looked up the first time it's needed. Its
    // Ok() is false if this Steam doesn't have it where expected (then "Share
    // my screen" isn't offered); nothing else depends on it.
    CShortcutsByName* Shortcuts();
    // Steam Input, looked up the first time it's needed (Windows only: its
    // place in Steam's engine is read from the SteamUI.dll next to Steam's
    // library). Its CanMove() is false if it isn't there; then the guest
    // window doesn't offer to change the controller order.
    CControllerSerializedByName* ControllerSerialized();

    bool Init();
    EInitError InitError() const { return m_initError; }
    void Shutdown();
    void RunCallbacks();

private:
    HSteamPipe m_hPipe;
    HSteamUser m_hUser;

    ISteamClient019* m_pSteamClient;
    ISteamUser019* m_pSteamUser;
    ISteamFriends015* m_pSteamFriends;
    ISteamUtils009* m_pSteamUtils;
    ISteamApps001* m_pSteamApps001;
    ISteamRemotePlay001* m_pSteamRemotePlay;

    IClientEngine* m_pClientEngine;
    CRemoteClientManagerByName m_remoteManager;
    CShortcutsByName m_shortcuts;
    bool m_shortcutsTried;
    CControllerSerializedByName m_controllers;
    bool m_controllersTried;

    bool m_ShuttingDown;
    bool m_Initialized;
    EInitError m_initError;
};

ClientContext* GClientContext();
CGameID GetRunningGameID();

// Steam hands out Remote Play links without a scheme ("s.team/p/..."), which chat
// apps don't turn into clickable links. Adds https:// when no scheme is present.
std::string ClickableURL(const char* url);

// Why Steam didn't start a Remote Play session or invite, for the host (UTF-8).
std::string LaunchResultMessage(ERemoteClientLaunchResult result);

#endif // !STEAMSTUFF_H
