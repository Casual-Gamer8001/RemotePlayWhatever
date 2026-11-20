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

#endif // UIHELPERS_H
