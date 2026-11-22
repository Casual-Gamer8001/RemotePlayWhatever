#include <wx/defs.h>
// Windows headers come first: Steamworks.h includes windows.h itself, and
// wx's copy must come before it so the winsock headers don't clash.
#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>
#include <mmsystem.h>
#endif
#include <wx/bitmap.h>
#include <wx/config.h>
#include <wx/datetime.h>
#include <wx/display.h>
#include <wx/filefn.h>
#include <wx/icon.h>
#include <wx/image.h>
#include <wx/log.h>
#include <wx/stdpaths.h>
#include <wx/timer.h>
#include <wx/tokenzr.h>
#include <wx/utils.h>
#ifdef __WXMSW__
#include <wx/msw/registry.h>
#endif
#include "UiHelpers.h"
#include "UiWidgets.h"

#ifndef __WXMSW__
#include "appicon.xpm"
#endif

wxIconBundle AppIcons()
{
#ifdef __WXMSW__
    return wxIconBundle("ID_APPICON", (WXHINSTANCE)::GetModuleHandleW(NULL));
#else
    wxIconBundle icons;
    const int sizes[] = { 16, 24, 32, 48, 64 };
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i)
    {
        wxImage image(appicon_xpm);
        image.Rescale(sizes[i], sizes[i], wxIMAGE_QUALITY_HIGH);
        wxIcon icon;
        icon.CopyFromBitmap(wxBitmap(image));
        icons.AddIcon(icon);
    }
    return icons;
#endif
}

bool SoundsEnabled()
{
    wxConfigBase* config = wxConfigBase::Get();
    return !config || config->ReadBool("/Options/Sounds", true);
}

void SetSoundsEnabled(bool enabled)
{
    wxConfigBase* config = wxConfigBase::Get();
    if (config)
    {
        config->Write("/Options/Sounds", enabled);
        config->Flush();
    }
}

bool GuestNoticesEnabled()
{
    wxConfigBase* config = wxConfigBase::Get();
    return !config || config->ReadBool("/Options/GuestNotices", true);
}

void SetGuestNoticesEnabled(bool enabled)
{
    wxConfigBase* config = wxConfigBase::Get();
    if (config)
    {
        config->Write("/Options/GuestNotices", enabled);
        config->Flush();
    }
}

std::set<uint64> LoadPinnedFriends()
{
    std::set<uint64> pinned;
    wxConfigBase* config = wxConfigBase::Get();
    const wxString list = config ? config->Read("/Options/PinnedFriends", wxString()) : wxString();
    wxStringTokenizer ids(list, ",", wxTOKEN_STRTOK);
    while (ids.HasMoreTokens())
    {
        unsigned long long id = 0;
        if (ids.GetNextToken().ToULongLong(&id) && id)
        {
            pinned.insert((uint64)id);
        }
    }
    return pinned;
}

void SavePinnedFriends(const std::set<uint64>& pinned)
{
    wxConfigBase* config = wxConfigBase::Get();
    if (!config)
    {
        return;
    }
    wxString list;
    for (std::set<uint64>::const_iterator it = pinned.begin(); it != pinned.end(); ++it)
    {
        list += (list.empty() ? "" : ",") + wxString::Format("%llu", (unsigned long long)*it);
    }
    config->Write("/Options/PinnedFriends", list);
    config->Flush();
}

void PlayGuestSound(bool joined)
{
    if (!SoundsEnabled())
    {
        return;
    }
#ifdef __WXMSW__
    // The same sounds Windows plays when a device is plugged in or removed.
    ::PlaySoundW(joined ? L"DeviceConnect" : L"DeviceDisconnect", NULL, SND_ALIAS | SND_ASYNC | SND_NODEFAULT);
#else
    if (joined)
    {
        wxBell();
    }
#endif
}

static wxString GuestKeyPath(uint64 steamID)
{
    return wxString::Format("/Guests/%llu", (unsigned long long)steamID);
}

bool LoadGuestControls(uint64 steamID, bool& kbd, bool& mouse, bool& ctrl)
{
    wxConfigBase* config = wxConfigBase::Get();
    wxString path = GuestKeyPath(steamID);
    // The friend's key can also hold just LastJoined; only saved choices count.
    if (!steamID || !config || !config->HasEntry(path + "/Keyboard"))
    {
        return false;
    }
    kbd = config->ReadBool(path + "/Keyboard", true);
    mouse = config->ReadBool(path + "/Mouse", true);
    ctrl = config->ReadBool(path + "/Controller", true);
    return true;
}

void SaveGuestControls(uint64 steamID, bool kbd, bool mouse, bool ctrl)
{
    wxConfigBase* config = wxConfigBase::Get();
    if (!steamID || !config)
    {
        return;
    }
    wxString path = GuestKeyPath(steamID);
    config->Write(path + "/Keyboard", kbd);
    config->Write(path + "/Mouse", mouse);
    config->Write(path + "/Controller", ctrl);
    config->Flush();
}

void SaveGuestJoined(uint64 steamID)
{
    wxConfigBase* config = wxConfigBase::Get();
    if (!steamID || !config)
    {
        return;
    }
    config->Write(GuestKeyPath(steamID) + "/LastJoined", (long)wxDateTime::Now().GetTicks());
    config->Flush();
}

std::map<uint64, long> LoadGuestJoinTimes()
{
    std::map<uint64, long> times;
    wxConfigBase* config = wxConfigBase::Get();
    if (!config || !config->HasGroup("/Guests"))
    {
        return times;
    }
    const wxString oldPath = config->GetPath();
    config->SetPath("/Guests");
    wxString group;
    long index = 0;
    for (bool more = config->GetFirstGroup(group, index); more; more = config->GetNextGroup(group, index))
    {
        unsigned long long id = 0;
        long when = 0;
        if (group.ToULongLong(&id) && id && config->Read(group + "/LastJoined", &when) && when > 0)
        {
            times[(uint64)id] = when;
        }
    }
    config->SetPath(oldPath);
    return times;
}

#ifdef __WXMSW__
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif
namespace
{
bool g_hideFromCapture = false;
HWINEVENTHOOK g_shownHook = nullptr;

void ApplyCaptureSetting(HWND window)
{
    // Whole windows of this process only; their controls go with them.
    DWORD process = 0;
    ::GetWindowThreadProcessId(window, &process);
    if (process != ::GetCurrentProcessId() || ::GetAncestor(window, GA_ROOT) != window)
    {
        return;
    }
    if (!g_hideFromCapture)
    {
        ::SetWindowDisplayAffinity(window, WDA_NONE);
    }
    else if (!::SetWindowDisplayAffinity(window, WDA_EXCLUDEFROMCAPTURE))
    {
        // Before Windows 10 2004 a window can only be blacked out.
        ::SetWindowDisplayAffinity(window, WDA_MONITOR);
    }
}

BOOL CALLBACK ApplyCaptureSettingTo(HWND window, LPARAM)
{
    ApplyCaptureSetting(window);
    return TRUE;
}

void CALLBACK OnWindowShown(HWINEVENTHOOK, DWORD, HWND window, LONG object, LONG child, DWORD, DWORD)
{
    if (g_hideFromCapture && window && object == OBJID_WINDOW && child == CHILDID_SELF)
    {
        ApplyCaptureSetting(window);
    }
}
} // namespace
#endif

void HideFromScreenCapture(bool hide)
{
#ifdef __WXMSW__
    if (hide == g_hideFromCapture)
    {
        return;
    }
    g_hideFromCapture = hide;
    if (hide && !g_shownHook)
    {
        // Windows that open later (message boxes, menus) as they appear.
        g_shownHook = ::SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, nullptr, OnWindowShown,
                                        ::GetCurrentProcessId(), 0, WINEVENT_OUTOFCONTEXT);
    }
    else if (!hide && g_shownHook)
    {
        ::UnhookWinEvent(g_shownHook);
        g_shownHook = nullptr;
    }
    ::EnumWindows(ApplyCaptureSettingTo, 0);
#else
    wxUnusedVar(hide);
#endif
}

#ifdef __WXMSW__
namespace
{
const char* const kRunKey = "Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const char* const kRunValue = "RemotePlayWhatever";

wxString StartCommand()
{
    return "\"" + wxStandardPaths::Get().GetExecutablePath() + "\" --minimized";
}
} // namespace
#endif

bool StartsWithWindows()
{
#ifdef __WXMSW__
    wxLogNull quiet;
    wxRegKey key(wxRegKey::HKCU, kRunKey);
    return key.Exists() && key.HasValue(kRunValue);
#else
    return false;
#endif
}

void SetStartWithWindows(bool on)
{
#ifdef __WXMSW__
    wxLogNull quiet;
    wxRegKey key(wxRegKey::HKCU, kRunKey);
    if (on)
    {
        key.Create();
        key.SetValue(kRunValue, StartCommand());
    }
    else if (key.Exists() && key.HasValue(kRunValue))
    {
        key.DeleteValue(kRunValue);
    }
#else
    wxUnusedVar(on);
#endif
}

void RefreshStartWithWindows()
{
#ifdef __WXMSW__
    wxLogNull quiet;
    wxRegKey key(wxRegKey::HKCU, kRunKey);
    wxString current;
    if (!key.Exists() || !key.HasValue(kRunValue) || !key.QueryValue(kRunValue, current))
    {
        return;
    }
    // Another copy that still exists keeps it; only a missing one is replaced.
    const wxString exe = current.AfterFirst('"').BeforeFirst('"');
    if (!exe.empty() && !wxFileExists(exe) && current != StartCommand())
    {
        key.SetValue(kRunValue, StartCommand());
    }
#endif
}

bool CtrlAltKeyIsFree(int key)
{
#ifdef __WXMSW__
    HKL layouts[32];
    const int count = ::GetKeyboardLayoutList(32, layouts);
    // AltGr arrives as left Ctrl + right Alt.
    BYTE state[256] = {};
    state[VK_CONTROL] = state[VK_LCONTROL] = 0x80;
    state[VK_MENU] = state[VK_RMENU] = 0x80;
    for (int i = 0; i < count; ++i)
    {
        const UINT scan = ::MapVirtualKeyExW((UINT)key, MAPVK_VK_TO_VSC, layouts[i]);
        WCHAR typed[8] = {};
        // Flag 4: don't disturb the keyboard state (dead keys) while asking.
        const int n = ::ToUnicodeEx((UINT)key, scan, state, typed, 8, 4, layouts[i]);
        // A character (or a dead key) means AltGr+key is in use there; a
        // control code like Ctrl+L's is harmless.
        if (n < 0 || (n > 0 && typed[0] >= 0x20))
        {
            return false;
        }
    }
    return true;
#else
    wxUnusedVar(key);
    return true;
#endif
}

ThemeChoice LoadThemeChoice()
{
    wxConfigBase* config = wxConfigBase::Get();
    const wxString value = config ? config->Read("/Options/Theme", wxString("auto")) : wxString("auto");
    if (value == "light")
    {
        return ThemeLight;
    }
    return value == "dark" ? ThemeDark : ThemeAuto;
}

void SaveThemeChoice(ThemeChoice choice)
{
    wxConfigBase* config = wxConfigBase::Get();
    if (config)
    {
        config->Write("/Options/Theme", wxString(choice == ThemeLight ? "light" : choice == ThemeDark ? "dark" : "auto"));
        config->Flush();
    }
}

void ApplySavedTheme()
{
    const ThemeChoice choice = LoadThemeChoice();
    Ui::ApplyTheme(choice == ThemeDark || (choice == ThemeAuto && Ui::SystemPrefersDark()));
}

namespace
{
// Whether a window at this place could be seen and dragged: enough of its
// title bar is on one of the screens.
bool TitleBarOnScreen(const wxRect& place, int titleHeight)
{
    const wxRect title(place.x, place.y, place.width, titleHeight);
    for (unsigned i = 0; i < wxDisplay::GetCount(); ++i)
    {
        const wxRect seen = wxDisplay(i).GetClientArea().Intersect(title);
        if (seen.width >= 80 && seen.height >= titleHeight / 2)
        {
            return true;
        }
    }
    return false;
}
} // namespace

WindowPlace::WindowPlace(wxTopLevelWindow* window, const wxString& name, bool withHeight) :
    m_window(window),
    m_path("/Windows/" + name + "/"),
    m_withHeight(withHeight),
    m_x(0),
    m_y(0),
    m_width(0),
    m_height(0),
    m_maximized(false),
    m_dirty(false)
{
    Restore();
    // Bound to this object, so they go when it's deleted.
    m_window->Bind(wxEVT_MOVE, &WindowPlace::OnMove, this);
    m_window->Bind(wxEVT_SIZE, &WindowPlace::OnSize, this);
}

WindowPlace::~WindowPlace()
{
    Stop();
    if (m_dirty)
    {
        Save();
    }
}

void WindowPlace::Notify()
{
    Save();
}

void WindowPlace::Restore()
{
    wxConfigBase* config = wxConfigBase::Get();
    long x = 0, y = 0, width = 0, height = 0;
    if (!config || !config->Read(m_path + "X", &x) || !config->Read(m_path + "Y", &y) ||
        !config->Read(m_path + "Width", &width) || width <= 0 ||
        (m_withHeight && (!config->Read(m_path + "Height", &height) || height <= 0)))
    {
        return;
    }
    const wxRect place((int)x, (int)y, m_window->FromDIP((int)width),
                       m_withHeight ? m_window->FromDIP((int)height) : m_window->GetSize().y);
    if (!TitleBarOnScreen(place, m_window->FromDIP(30)))
    {
        return;
    }
    m_window->SetSize(place);
    // A hidden window is maximized when it's shown.
    if (m_withHeight && config->ReadBool(m_path + "Maximized", false))
    {
        m_window->Maximize(true);
    }
}

void WindowPlace::OnMove(wxMoveEvent& event)
{
    Changed();
    event.Skip();
}

void WindowPlace::OnSize(wxSizeEvent& event)
{
    Changed();
    event.Skip();
}

void WindowPlace::Changed()
{
    // Hidden in the tray, or minimized: it's still where it was.
    if (!m_window->IsShown() || m_window->IsIconized())
    {
        return;
    }
    m_maximized = m_window->IsMaximized();
    if (!m_maximized)
    {
        const wxRect place = m_window->GetRect();
        m_x = place.x;
        m_y = place.y;
        m_width = m_window->ToDIP(place.width);
        m_height = m_window->ToDIP(place.height);
    }
    m_dirty = true;
    Start(1000, wxTIMER_ONE_SHOT);
}

void WindowPlace::Save()
{
    m_dirty = false;
    wxConfigBase* config = wxConfigBase::Get();
    // Nothing to save until it has been on screen in its normal state.
    if (!config || m_width <= 0)
    {
        return;
    }
    config->Write(m_path + "X", (long)m_x);
    config->Write(m_path + "Y", (long)m_y);
    config->Write(m_path + "Width", (long)m_width);
    if (m_withHeight)
    {
        config->Write(m_path + "Height", (long)m_height);
        config->Write(m_path + "Maximized", m_maximized);
    }
    config->Flush();
}