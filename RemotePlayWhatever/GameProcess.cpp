#include <wx/defs.h>
// Windows headers come first: Steamworks.h includes windows.h itself, and
// wx's copy must come before it so the winsock headers don't clash.
#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>
#include <tlhelp32.h>
#endif
#include <cwchar>
#include <map>
#include <string>
#include "GameProcess.h"
#include "ClientVTableByName.h"

#ifdef __WXMSW__
// Steam's own programs, which aren't the game even with the overlay loaded.
static bool IsSteamProgram(const wchar_t* exe)
{
    return _wcsicmp(exe, L"steam.exe") == 0 || _wcsicmp(exe, L"steamwebhelper.exe") == 0 ||
           _wcsnicmp(exe, L"gameoverlayui", 13) == 0;
}

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
        if (pid == 0 || pid == 4 || pid == self || IsSteamProgram(pe.szExeFile))
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

#ifdef __WXMSW__
// The programs running now (file names), by process ID.
static std::map<DWORD, std::wstring> ProcessNames()
{
    std::map<DWORD, std::wstring> names;
    HANDLE procs = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (procs == INVALID_HANDLE_VALUE)
    {
        return names;
    }
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    for (BOOL more = ::Process32FirstW(procs, &pe); more; more = ::Process32NextW(procs, &pe))
    {
        names[pe.th32ProcessID] = pe.szExeFile;
    }
    ::CloseHandle(procs);
    return names;
}

// Whether a process is a game's: Steam's overlay is loaded in it, and it isn't
// this app or one of Steam's own programs. Answers are kept in `known`.
static bool IsGameProcess(DWORD pid, const std::map<DWORD, std::wstring>& names, std::map<DWORD, bool>& known)
{
    std::map<DWORD, bool>::const_iterator answer = known.find(pid);
    if (answer != known.end())
    {
        return answer->second;
    }
    std::map<DWORD, std::wstring>::const_iterator name = names.find(pid);
    const bool game = pid != 0 && pid != ::GetCurrentProcessId() && name != names.end() &&
                      !IsSteamProgram(name->second.c_str()) && HasSteamOverlay(pid);
    known[pid] = game;
    return game;
}

struct GameWindowSearch
{
    std::map<DWORD, std::wstring> names;
    std::map<DWORD, bool> known;
    HWND found;
    DWORD pid;
};

static BOOL CALLBACK FindGameWindow(HWND w, LPARAM param)
{
    GameWindowSearch* search = reinterpret_cast<GameWindowSearch*>(param);
    // A program's own window: shown (minimized counts), not belonging to
    // another window, not a tool window, and not empty.
    RECT r;
    if (!::IsWindowVisible(w) || ::GetWindow(w, GW_OWNER) != NULL ||
        (::GetWindowLongPtrW(w, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0 ||
        (!::IsIconic(w) && (!::GetWindowRect(w, &r) || r.right <= r.left || r.bottom <= r.top)))
    {
        return TRUE;
    }
    DWORD pid = 0;
    ::GetWindowThreadProcessId(w, &pid);
    if (!IsGameProcess(pid, search->names, search->known))
    {
        return TRUE;
    }
    // Windows come front to back, so this is the game's frontmost one.
    search->found = w;
    search->pid = pid;
    return FALSE;
}
#endif

FrontWindow WindowInFront()
{
#ifdef __WXMSW__
    const HWND front = ::GetForegroundWindow();
    DWORD pid = 0;
    if (!front || !::GetWindowThreadProcessId(front, &pid))
    {
        clientvt::Log("[rp] window in front: none\n");
        return FrontNone;
    }
    const std::map<DWORD, std::wstring> names = ProcessNames();
    std::map<DWORD, bool> known;
    const bool game = IsGameProcess(pid, names, known);
    std::map<DWORD, std::wstring>::const_iterator name = names.find(pid);
    clientvt::Log("[rp] window in front: pid %lu (%ls), the game's: %d\n", (unsigned long)pid,
                  name != names.end() ? name->second.c_str() : L"?", (int)game);
    return game ? FrontGame : FrontOther;
#else
    return FrontNone;
#endif
}

bool BringGameWindowToFront()
{
#ifdef __WXMSW__
    GameWindowSearch search;
    search.names = ProcessNames();
    search.found = NULL;
    search.pid = 0;
    ::EnumWindows(FindGameWindow, reinterpret_cast<LPARAM>(&search));
    if (!search.found)
    {
        clientvt::Log("[rp] no game window to bring to the front\n");
        return false;
    }
    if (::IsIconic(search.found))
    {
        ::ShowWindow(search.found, SW_RESTORE);
    }
    // (Windows finishes the switch once the game answers, so it isn't checked here.)
    const bool done = ::SetForegroundWindow(search.found) != FALSE;
    std::map<DWORD, std::wstring>::const_iterator name = search.names.find(search.pid);
    clientvt::Log("[rp] game window of pid %lu (%ls) brought to the front: %d\n", (unsigned long)search.pid,
                  name != search.names.end() ? name->second.c_str() : L"?", (int)done);
    return done;
#else
    return false;
#endif
}
