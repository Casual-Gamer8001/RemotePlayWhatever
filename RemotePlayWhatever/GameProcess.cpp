#include <wx/defs.h>
// Windows headers come first: Steamworks.h includes windows.h itself, and
// wx's copy must come before it so the winsock headers don't clash.
#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>
#include <tlhelp32.h>
#endif
#include <cwchar>
#include "GameProcess.h"
#include "ClientVTableByName.h"

#ifdef __WXMSW__
static bool HasSteamOverlay(DWORD pid)
{
    HANDLE mods = ::CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (mods == INVALID_HANDLE_VALUE)
    {
        return false;   // gone, or not ours to look at (e.g. elevated)
    }
    bool overlay = false;
    MODULEENTRY32W me;
    me.dwSize = sizeof(me);
    for (BOOL more = ::Module32FirstW(mods, &me); more && !overlay; more = ::Module32NextW(mods, &me))
    {
        overlay = _wcsicmp(me.szModule, L"gameoverlayrenderer64.dll") == 0 ||
                  _wcsicmp(me.szModule, L"gameoverlayrenderer.dll") == 0;
    }
    ::CloseHandle(mods);
    return overlay;
}

static unsigned long long StartTime(DWORD pid)
{
    unsigned long long start = 0;
    HANDLE process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process)
    {
        FILETIME created, exited, kernel, user;
        if (::GetProcessTimes(process, &created, &exited, &kernel, &user))
        {
            start = ((unsigned long long)created.dwHighDateTime << 32) | created.dwLowDateTime;
        }
        ::CloseHandle(process);
    }
    return start;
}
#endif

uint32_t FindGameOverlayPID()
{
#ifdef __WXMSW__
    const ULONGLONG began = ::GetTickCount64();
    HANDLE procs = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (procs == INVALID_HANDLE_VALUE)
    {
        return 0;
    }

    const DWORD self = ::GetCurrentProcessId();
    DWORD session = 0;
    const bool haveSession = ::ProcessIdToSessionId(self, &session) != FALSE;
    DWORD best = 0;
    unsigned long long bestStart = 0;
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    for (BOOL more = ::Process32FirstW(procs, &pe); more; more = ::Process32NextW(procs, &pe))
    {
        const DWORD pid = pe.th32ProcessID;
        // Steam's own processes aren't the game.
        if (pid == 0 || pid == 4 || pid == self ||
            _wcsicmp(pe.szExeFile, L"steam.exe") == 0 ||
            _wcsicmp(pe.szExeFile, L"steamwebhelper.exe") == 0 ||
            _wcsnicmp(pe.szExeFile, L"gameoverlayui", 13) == 0)
        {
            continue;
        }
        // The game runs on this desktop: skip other sessions' processes (the
        // services', another user's) and ones that won't even say which.
        DWORD theirs = 0;
        if (haveSession && (!::ProcessIdToSessionId(pid, &theirs) || theirs != session))
        {
            continue;
        }
        if (!HasSteamOverlay(pid))
        {
            continue;
        }
        const unsigned long long start = StartTime(pid);
        clientvt::Log("[rp] overlay found in pid=%lu (%ls)\n", (unsigned long)pid, pe.szExeFile);
        if (!best || start > bestStart)
        {
            best = pid;
            bestStart = start;
        }
    }
    ::CloseHandle(procs);
    clientvt::Log("[rp] game overlay pid %lu (looked for it in %llu ms)\n", (unsigned long)best,
                  (unsigned long long)(::GetTickCount64() - began));
    return (uint32_t)best;
#else
    return 0;
#endif
}

bool ProcessStillRunning(uint32_t pid)
{
#ifdef __WXMSW__
    HANDLE process = ::OpenProcess(SYNCHRONIZE, FALSE, (DWORD)pid);
    if (!process)
    {
        return false;
    }
    const bool running = ::WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    ::CloseHandle(process);
    return running;
#else
    (void)pid;
    return false;
#endif
}
