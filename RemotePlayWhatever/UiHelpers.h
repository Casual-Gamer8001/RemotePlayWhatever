#ifndef UIHELPERS_H
#define UIHELPERS_H
#include <map>
#include <set>
#include <wx/iconbndl.h>
#include <wx/timer.h>
#include <wx/toplevel.h>
#include <Steamworks.h>

// The app icon in every size it comes in (from the exe's resources on
// Windows), so windows and the tray icon stay sharp at any display scale.
wxIconBundle AppIcons();

// "Play sounds when guests join or leave" (saved setting, on by default).
bool SoundsEnabled();
void SetSoundsEnabled(bool enabled);

// Short system sound for a guest joining or leaving (if sounds are enabled).
void PlayGuestSound(bool joined);

// "Notifications when guests join or leave" (saved setting, on by default):
// a short message with their name, also by the clock while you're in the game.
bool GuestNoticesEnabled();
void SetGuestNoticesEnabled(bool enabled);

// Friends pinned to the top of the list, by SteamID (saved).
std::set<uint64> LoadPinnedFriends();
void SavePinnedFriends(const std::set<uint64>& pinned);

// What a friend was last allowed to use (keyboard, mouse, controller), saved
// per SteamID so it's applied again the next time they join.
bool LoadGuestControls(uint64 steamID, bool& kbd, bool& mouse, bool& ctrl);
void SaveGuestControls(uint64 steamID, bool kbd, bool mouse, bool ctrl);

// When each friend last joined one of the host's sessions (Unix time, per
// SteamID), for the "Played with recently" group in the friends list.
void SaveGuestJoined(uint64 steamID);
std::map<uint64, long> LoadGuestJoinTimes();

// While the whole screen is shared with guests (non-Steam games), keep this
// app's own windows (main window, guest controls, message boxes, menus) out of
// what they see. Windows 10 2004 or later (older ones show a black box);
// does nothing on other systems. Cheap to call repeatedly.
void HideFromScreenCapture(bool hide);

// "Start with Windows" (Windows only): a Run entry that opens the app hidden in
// the tray when the user signs in.
bool StartsWithWindows();
void SetStartWithWindows(bool on);
// If the entry points at a copy that's gone (its folder was moved), point it
// at this one.
void RefreshStartWithWindows();

// Whether Ctrl+Alt+<key> (a virtual-key code such as 'L') is free to use as a
// global shortcut. Windows reports AltGr as Ctrl+Alt, so on layouts where
// AltGr+<key> types something (Polish AltGr+L is "ł"), a hotkey would swallow
// that character in every app. Checks every keyboard layout the user has.
bool CtrlAltKeyIsFree(int key);

// Light or dark: follow Windows (the default), or always one of them (Options
// > Theme, or the tray menu). Saved as Options\Theme = auto / light / dark.
enum ThemeChoice
{
    ThemeAuto,
    ThemeLight,
    ThemeDark,
};
ThemeChoice LoadThemeChoice();
void SaveThemeChoice(ThemeChoice choice);
// Switch the windows to the saved choice (for Auto: Windows' current mode).
void ApplySavedTheme();

// Where a window was and how big, saved under /Windows/<name>. Creating this
// puts the window back there (unless that's no longer on a screen, say a
// monitor was unplugged); then its place is saved a second after it moves or
// changes size, and when this is deleted (with the window: the window owns
// it). It follows the window while it's on screen and neither minimized nor
// maximized, so a maximized window keeps the place it goes back to.
// withHeight: the height (and being maximized) is kept too; false for windows
// that fit their height to what they show (the guest controls).
class WindowPlace : public wxTimer
{
public:
    WindowPlace(wxTopLevelWindow* window, const wxString& name, bool withHeight);
    virtual ~WindowPlace();
    virtual void Notify() override;

private:
    void Restore();
    void OnMove(wxMoveEvent& event);
    void OnSize(wxSizeEvent& event);
    void Changed();
    void Save();

    wxTopLevelWindow* m_window;
    wxString m_path;
    bool m_withHeight;
    int m_x;            // pixels
    int m_y;
    int m_width;        // DIPs, so it stays right if the display scale changes
    int m_height;
    bool m_maximized;
    bool m_dirty;
};

#endif // UIHELPERS_H
