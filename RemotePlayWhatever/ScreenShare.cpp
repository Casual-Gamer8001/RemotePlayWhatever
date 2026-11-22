#include <wx/defs.h>
#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>
#else
#include <csignal>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/time.h>
#include <wx/utils.h>
#include "SteamStuff.h"
#include "ScreenShare.h"

namespace
{
    uint32 g_appID = 0;             // the shortcut made, while it's in Steam
    bool g_stopping = false;        // told to stop; removed once Steam sees it stop
    bool g_seenRunning = false;     // Steam has reported it running
    unsigned long g_start = 0;      // which screen share this is: each has its own stop
    wxLongLong g_startedAt = 0;
    wxLongLong g_stopAt = 0;
    wxLongLong g_goneSince = 0;     // seen running, then not since then (0: running)
#ifdef __WXMSW__
    // Kept open, and set once stopped, so a placeholder Steam starts late
    // still finds its stop.
    std::vector<HANDLE> g_stopEvents;
#endif

    // How the owner and the placeholder find each other: by the owner's
    // process ID and the number of the screen share, so a placeholder that is
    // slow to stop can't miss its stop when the next one starts.
#ifdef __WXMSW__
    HANDLE StopEvent(unsigned long ownerPid, unsigned long start)
    {
        wchar_t name[96];
        swprintf(name, 96, L"Local\\RemotePlayWhatever-ScreenShare-%lu-%lu", ownerPid, start);
        // Manual reset: once set it stays set, so a placeholder that starts
        // after the stop still sees it.
        return ::CreateEventW(NULL, TRUE, FALSE, name);
    }
#else
    // Always /tmp: Steam may start the placeholder with another TMPDIR.
    std::string StopFile(unsigned long ownerPid, unsigned long start)
    {
        return "/tmp/RemotePlayWhatever-screenshare-" + std::to_string((unsigned long long)ownerPid) + "-" +
               std::to_string((unsigned long long)start) + ".stop";
    }
#endif

    // This program, the way Steam will start it. An AppImage runs from a
    // temporary mount, so the AppImage file itself is what Steam starts.
    std::string ProgramForSteam()
    {
        std::string path;
#ifndef __WXMSW__
        const char* appImage = std::getenv("APPIMAGE");
        if (appImage && *appImage)
        {
            path = appImage;
        }
#endif
        if (path.empty())
        {
            path = wxStandardPaths::Get().GetExecutablePath().utf8_str().data();
        }
        return path;
    }

    // Steam keeps a temporary shortcut's fields as given: quotes would become
    // part of the path, and Steam then can't start it.
    std::string Quoted(const std::string& text)
    {
        return "\"" + text + "\"";
    }

    void SignalStop()
    {
        if (!g_start)
        {
            return;
        }
#ifdef __WXMSW__
        if (!g_stopEvents.empty() && g_stopEvents.back())
        {
            ::SetEvent(g_stopEvents.back());
        }
#else
        if (std::FILE* f = std::fopen(StopFile(wxGetProcessId(), g_start).c_str(), "w"))
        {
            std::fclose(f);
        }
#endif
    }

    void RemoveShortcutNow()
    {
        if (g_appID)
        {
            GClientContext()->Shortcuts()->RemoveShortcut(g_appID);
            clientvt::Log("[rp] screen share: removed shortcut %u\n", g_appID);
        }
        g_appID = 0;
        g_stopping = false;
        g_seenRunning = false;
        g_goneSince = 0;
    }

    // Stop it and forget it at once: Steam isn't running it (any more), or
    // the app is closing.
    void Finish()
    {
        SignalStop();
        RemoveShortcutNow();
    }
}

namespace ScreenShare
{
    bool Available()
    {
        return GClientContext()->Shortcuts()->CanAddTemporary();
    }

    bool IsOurs(CGameID game)
    {
        return g_appID && game.IsValid() && game.IsShortcut() && game.ModID() == g_appID;
    }

    bool Active()
    {
        return !g_stopping && IsOurs(GetRunningGameID());
    }

    bool Starting()
    {
        return g_appID && !g_stopping && !g_seenRunning && !Active() &&
               wxGetLocalTimeMillis() - g_startedAt < 30000;
    }

    bool Start(std::string& error)
    {
        if (g_appID && !g_stopping && (Active() || Starting()))
        {
            return true;
        }
        if (g_appID)
        {
            // One that was stopped and Steam hasn't seen stop yet, or one
            // that ended by itself or never started.
            Finish();
        }
        CShortcutsByName* shortcuts = GClientContext()->Shortcuts();
        if (!shortcuts->CanAddTemporary())
        {
            error = "This Steam version doesn't let Remote Play Whatever start a screen share.";
            return false;
        }

        const unsigned long owner = wxGetProcessId();
        const unsigned long start = ++g_start;
#ifdef __WXMSW__
        g_stopEvents.push_back(StopEvent(owner, start));
#else
        std::remove(StopFile(owner, start).c_str());    // left by an earlier app with this process ID
#endif

        const std::string program = ProgramForSteam();
        const std::string folder = wxFileName(wxString::FromUTF8(program.c_str())).GetPath().utf8_str().data();
        // For tests and troubleshooting: RPW_TEST_SCREEN_SHARE=quote / nodir /
        // quote-nodir gives Steam the shortcut's fields another way.
        wxString variant;
        wxGetEnv("RPW_TEST_SCREEN_SHARE", &variant);
        const bool quote = variant.Contains("quote");
        const bool setFolder = !variant.Contains("nodir");
        const std::string exe = quote ? Quoted(program) : program;
        const uint32 appID = shortcuts->AddTemporaryShortcut("Screen sharing (Remote Play Whatever)", exe.c_str(), "");
        if (!appID)
        {
            error = "Steam didn't add the screen share.";
            return false;
        }
        if (setFolder)
        {
            const std::string dir = quote ? Quoted(folder) : folder;
            shortcuts->SetShortcutStartDir(appID, dir.c_str());
        }
        char options[64];
        const std::string commandLine = "--share-screen " + std::to_string((unsigned long long)owner) + "-" +
                                        std::to_string((unsigned long long)start);
        std::strncpy(options, commandLine.c_str(), sizeof(options) - 1);
        options[sizeof(options) - 1] = '\0';
        shortcuts->SetShortcutCommandLine(appID, options);
        // Started like any game (Recent games' Play does the same), so Steam
        // tracks it as the running game. A shortcut's game ID: its app ID in
        // the top half, type 2 (shortcut) above the 24-bit app ID.
        const uint64 gameID = ((uint64)appID << 32) | 0x02000000ull;
        const bool launched = wxLaunchDefaultBrowser(wxString::Format("steam://rungameid/%llu", (unsigned long long)gameID));
        clientvt::Log("[rp] screen share: shortcut %u for %s in %s%s, %s -> steam://rungameid/%llu %s\n", appID,
                      exe.c_str(), setFolder ? folder.c_str() : "(no folder)", quote ? " (quoted)" : "", options,
                      (unsigned long long)gameID, launched ? "opened" : "FAILED to open");
        if (!launched)
        {
            shortcuts->RemoveShortcut(appID);
            error = "Steam didn't start the screen share.";
            return false;
        }
        g_appID = appID;
        g_stopping = false;
        g_seenRunning = false;
        g_goneSince = 0;
        g_startedAt = wxGetLocalTimeMillis();
        return true;
    }

    void Stop(bool closing)
    {
        if (!g_appID)
        {
            return;
        }
        clientvt::Log("[rp] screen share: stop (closing=%d)\n", (int)closing);
        if (closing)
        {
            Finish();
            return;
        }
        // Removing a shortcut Steam still thinks is running could confuse it,
        // so that waits until Steam has seen it stop (or 15 s, Tick).
        SignalStop();
        g_stopping = true;
        g_stopAt = wxGetLocalTimeMillis();
    }

    TickResult Tick()
    {
        if (!g_appID)
        {
            return TickNothing;
        }
        const wxLongLong now = wxGetLocalTimeMillis();
        const bool running = IsOurs(GetRunningGameID());
        if (g_stopping)
        {
            if (!running || now - g_stopAt > 15000)
            {
                RemoveShortcutNow();
            }
            return TickNothing;
        }
        if (running)
        {
            g_seenRunning = true;
            g_goneSince = 0;
            return TickNothing;
        }
        if (g_seenRunning)
        {
            // Stopped from Steam, or it crashed. What Steam reports can lag a
            // moment, so it has to be gone for 3 s.
            if (g_goneSince == 0)
            {
                g_goneSince = now;
            }
            if (now - g_goneSince < 3000)
            {
                return TickNothing;
            }
            clientvt::Log("[rp] screen share: it stopped by itself\n");
            Finish();
            return TickEnded;
        }
        if (now - g_startedAt >= 30000)
        {
            clientvt::Log("[rp] screen share: Steam didn't start it\n");
            Finish();
            return TickNotStarted;
        }
        return TickNothing;
    }

    int RunPlaceholder(unsigned long ownerPid, unsigned long start)
    {
        clientvt::Log("[rp] screen share placeholder: started for %lu-%lu\n", ownerPid, start);
#ifdef __WXMSW__
        HANDLE owner = ::OpenProcess(SYNCHRONIZE, FALSE, (DWORD)ownerPid);
        if (!owner)
        {
            return 0;       // that copy of the app is gone already
        }
        HANDLE stop = StopEvent(ownerPid, start);
        HANDLE waits[2] = { owner, stop };
        ::WaitForMultipleObjects(stop ? 2 : 1, waits, FALSE, INFINITE);
        if (stop)
        {
            ::CloseHandle(stop);
        }
        ::CloseHandle(owner);
#else
        const std::string stopFile = StopFile(ownerPid, start);
        for (;;)
        {
            // Gone, or that process ID is somebody else's by now.
            if (::kill((pid_t)ownerPid, 0) != 0)
            {
                break;
            }
            struct stat st;
            if (::stat(stopFile.c_str(), &st) == 0)
            {
                std::remove(stopFile.c_str());
                break;
            }
            ::usleep(500000);
        }
#endif
        return 0;
    }

    int RunPlaceholderArg(const char* arg)
    {
        // "<pid>-<n>": both whole numbers, and a process ID that can be real.
        if (!arg)
        {
            return 2;
        }
        char* end = nullptr;
        errno = 0;
        const unsigned long pid = std::strtoul(arg, &end, 10);
        if (end == arg || *end != '-' || errno == ERANGE || pid <= 1 || pid > (unsigned long)INT_MAX)
        {
            return 2;
        }
        const char* rest = end + 1;
        const unsigned long start = std::strtoul(rest, &end, 10);
        if (end == rest || *end != '\0' || errno == ERANGE || start == 0)
        {
            return 2;
        }
        return RunPlaceholder(pid, start);
    }
}