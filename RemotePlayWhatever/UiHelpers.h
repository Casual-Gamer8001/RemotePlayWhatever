#ifndef UIHELPERS_H
#define UIHELPERS_H
#include <map>
#include <wx/iconbndl.h>
#include <Steamworks.h>

// The app icon in every size it comes in (from the exe's resources on
// Windows), so windows and the tray icon stay sharp at any display scale.
wxIconBundle AppIcons();

// "Play sounds when guests join or leave" (saved setting, on by default).
bool SoundsEnabled();
void SetSoundsEnabled(bool enabled);

// Short system sound for a guest joining or leaving (if sounds are enabled).
void PlayGuestSound(bool joined);

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

// Light or dark: follow Windows (the default), or always one of them (tray
// menu > Theme). Saved as Options\Theme = auto / light / dark.
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

#endif // UIHELPERS_H
