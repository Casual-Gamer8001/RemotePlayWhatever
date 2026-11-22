#ifndef GAMEINFO_H
#define GAMEINFO_H
#include <vector>
#include <wx/string.h>
#include <Steamworks.h>

// Name of an installed Steam game, read from its appmanifest file in the Steam
// library folders. Returns "App <id>" when the manifest can't be found.
wxString GetSteamAppName(AppId_t appID);

// Name of a non-Steam game added to Steam (a shortcut such as an emulator),
// read from the signed-in account's shortcuts.vdf. Empty if it isn't found.
wxString GetShortcutName(uint32 shortcutID, uint32 accountID);

// Display name for whatever game is running: Steam games by app name, non-Steam
// shortcuts by the name they have in the Steam library. Empty if unknown.
wxString GetGameName(CGameID game, uint32 accountID);

// Name of the game a friend is playing: Steam's own name for it (installed here
// or not), else its local appmanifest name. Empty if unknown (e.g. a friend's
// non-Steam game, or Steam has no details yet; asked again a minute later).
wxString GetFriendGameName(CGameID game);

// A wide picture of a game for the top of the main window: the library banner
// from Steam's cache (Steam games), or artwork the user set for a non-Steam
// game in Steam. Empty if there's none.
wxString GetGameArtworkPath(CGameID game, uint32 accountID);

// A game played on this computer lately, to start from the app.
struct RecentGame
{
    CGameID game;           // a Steam game, or a non-Steam game (shortcut) added to Steam
    wxString name;
    long long lastPlayed;   // Unix time
};

// Up to max installed Steam games and non-Steam games of the signed-in
// account, the most recently played first (from Steam's localconfig.vdf and
// shortcuts.vdf; re-read only when Steam has changed them). Steam's own tools
// (redistributables, Proton and such) are left out.
std::vector<RecentGame> GetRecentGames(uint32 accountID, size_t max);

// A small wide picture of a game for a list: its store header (Steam games)
// or the grid artwork set for a non-Steam game. Empty if there's none.
wxString GetGameThumbnailPath(CGameID game, uint32 accountID);

#endif // GAMEINFO_H
