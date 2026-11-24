#include <algorithm>
#include <cstdlib>
#include <map>
#include <regex>
#include <string>
#include <vector>
#include <wx/datetime.h>
#include <wx/dir.h>
#include <wx/ffile.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/time.h>
#include <wx/utils.h>
#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>
#include <wx/msw/registry.h>
#endif
#include "GameInfo.h"
#include "SteamStuff.h"

static std::string ReadTextFile(const wxString& path)
{
    if (!wxFileExists(path))
    {
        return std::string();
    }

    wxFFile file(path, "rb");
    wxString text;
    if (!file.IsOpened() || !file.ReadAll(&text, wxConvUTF8))
    {
        return std::string();
    }
    return std::string(text.utf8_str());
}

static std::string ReadBinaryFile(const wxString& path)
{
    std::string data;
    wxFFile file;
    if (!wxFileExists(path) || !file.Open(path, "rb"))
    {
        return data;
    }
    wxFileOffset size = file.Length();
    if (size > 0 && size < 16 * 1024 * 1024)
    {
        data.resize((size_t)size);
        if (file.Read(&data[0], data.size()) != data.size())
        {
            data.clear();
        }
    }
    return data;
}

static std::vector<wxString> SteamRoots()
{
    std::vector<wxString> roots;
#ifdef __WXMSW__
    wxRegKey key(wxRegKey::HKCU, "Software\\Valve\\Steam");
    wxString path;
    if (key.Exists() && key.QueryValue("SteamPath", path) && !path.empty())
    {
        roots.push_back(path);
    }
#elif defined(__APPLE__)
    roots.push_back(wxGetHomeDir() + "/Library/Application Support/Steam");
#else
    wxString home = wxGetHomeDir();
    roots.push_back(home + "/.steam/steam");
    roots.push_back(home + "/.local/share/Steam");
    roots.push_back(home + "/.var/app/com.valvesoftware.Steam/.local/share/Steam");
#endif
    return roots;
}

// Every Steam install plus the extra library folders listed in its
// steamapps/libraryfolders.vdf.
static std::vector<wxString> LibraryFolders()
{
    static const std::regex pathRx("\"path\"\\s+\"([^\"]+)\"");

    std::vector<wxString> folders;
    for (const wxString& root : SteamRoots())
    {
        if (!wxDirExists(root))
        {
            continue;
        }
        folders.push_back(root);

        std::string vdf = ReadTextFile(root + "/steamapps/libraryfolders.vdf");
        for (std::sregex_iterator it(vdf.begin(), vdf.end(), pathRx), end; it != end; ++it)
        {
            // VDF escapes backslashes in Windows paths.
            std::string escaped = (*it)[1].str();
            std::string path;
            for (size_t i = 0; i < escaped.size(); ++i)
            {
                if (escaped[i] == '\\' && i + 1 < escaped.size() && escaped[i + 1] == '\\')
                {
                    ++i;
                }
                path += escaped[i];
            }
            folders.push_back(wxString::FromUTF8(path.c_str()));
        }
    }
    return folders;
}

wxString GetSteamAppName(AppId_t appID)
{
    static std::map<AppId_t, wxString> cache;
    auto cached = cache.find(appID);
    if (cached != cache.end())
    {
        return cached->second;
    }

    static const std::regex nameRx("\"name\"\\s+\"([^\"]*)\"");

    wxString name;
    for (const wxString& folder : LibraryFolders())
    {
        std::string acf = ReadTextFile(wxString::Format("%s/steamapps/appmanifest_%u.acf", folder, (unsigned)appID));
        std::smatch match;
        if (std::regex_search(acf, match, nameRx) && match[1].length() > 0)
        {
            name = wxString::FromUTF8(match[1].str().c_str());
            break;
        }
    }

    if (name.empty())
    {
        name = wxString::Format("App %u", (unsigned)appID);
    }
    cache[appID] = name;
    return name;
}

static uint32 Crc32(const std::string& data)
{
    uint32 crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < data.size(); ++i)
    {
        crc ^= (unsigned char)data[i];
        for (int bit = 0; bit < 8; ++bit)
        {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

static bool SameKey(const std::string& a, const char* b)
{
    return wxString::FromUTF8(a.c_str()).IsSameAs(b, false);
}

static bool ReadCString(const std::string& data, size_t& pos, std::string& out)
{
    size_t end = data.find('\0', pos);
    if (end == std::string::npos)
    {
        return false;
    }
    out.assign(data, pos, end - pos);
    pos = end + 1;
    return true;
}

// shortcuts.vdf is Steam's binary KeyValues format: every item is a type byte,
// a NUL-terminated name and a value, and 0x08 ends a nested map. The layout is
// shortcuts -> "<index>" -> { appid, AppName, Exe, LastPlayTime, ..., tags { ... } }.
struct ShortcutInfo
{
    wxString name;
    long long lastPlayed;   // Unix time, 0 if never started through Steam
};

static void ParseShortcuts(const std::string& data, std::map<uint32, ShortcutInfo>& shortcuts)
{
    size_t pos = 0;
    int depth = 0;
    bool hasID = false;
    uint32 id = 0;
    uint32 lastPlayed = 0;
    std::string appName, exe;

    while (pos < data.size())
    {
        unsigned char type = (unsigned char)data[pos++];
        if (type == 0x08)
        {
            if (depth == 2)
            {
                // Older Steam versions don't store the id; it is derived from
                // the target and name the same way Steam does.
                uint32 shortcutID = hasID ? id : (Crc32(exe + appName) | 0x80000000u);
                if (!appName.empty())
                {
                    ShortcutInfo info;
                    info.name = wxString::FromUTF8(appName.c_str());
                    info.lastPlayed = lastPlayed;
                    shortcuts[shortcutID] = info;
                }
            }
            if (--depth < 0)
            {
                break;
            }
            continue;
        }

        std::string key;
        if (!ReadCString(data, pos, key))
        {
            return;
        }

        switch (type)
        {
        case 0x00:              // nested map
            if (++depth == 2)
            {
                hasID = false;
                id = 0;
                lastPlayed = 0;
                appName.clear();
                exe.clear();
            }
            break;
        case 0x01:              // string
        {
            std::string value;
            if (!ReadCString(data, pos, value))
            {
                return;
            }
            if (depth == 2 && SameKey(key, "AppName")) appName = value;
            else if (depth == 2 && SameKey(key, "Exe")) exe = value;
            break;
        }
        case 0x02:              // 32-bit integer
        {
            if (pos + 4 > data.size())
            {
                return;
            }
            const uint32 value = (uint32)(unsigned char)data[pos] |
                                 ((uint32)(unsigned char)data[pos + 1] << 8) |
                                 ((uint32)(unsigned char)data[pos + 2] << 16) |
                                 ((uint32)(unsigned char)data[pos + 3] << 24);
            if (depth == 2 && SameKey(key, "appid"))
            {
                id = value;
                hasID = true;
            }
            else if (depth == 2 && SameKey(key, "LastPlayTime"))
            {
                lastPlayed = value;
            }
            pos += 4;
            break;
        }
        case 0x03:              // float
        case 0x04:              // pointer
        case 0x06:              // colour
            pos += 4;
            break;
        case 0x07:              // 64-bit unsigned
        case 0x0A:              // 64-bit signed
            pos += 8;
            break;
        default:                // unknown type: stop rather than misread the rest
            return;
        }
    }
}

// The account's non-Steam games, from the first Steam install that has its
// shortcuts.vdf; re-read only when Steam has changed the file.
static const std::map<uint32, ShortcutInfo>& Shortcuts(uint32 accountID)
{
    static std::map<uint32, ShortcutInfo> shortcuts;
    static wxString loadedPath;
    static wxDateTime loadedTime;

    for (const wxString& root : SteamRoots())
    {
        wxString path = wxString::Format("%s/userdata/%u/config/shortcuts.vdf", root, (unsigned)accountID);
        if (!wxFileExists(path))
        {
            continue;
        }
        wxDateTime modified = wxFileName(path).GetModificationTime();
        if (path != loadedPath || !modified.IsValid() || !loadedTime.IsValid() || modified != loadedTime)
        {
            shortcuts.clear();
            ParseShortcuts(ReadBinaryFile(path), shortcuts);
            loadedPath = path;
            loadedTime = modified;
        }
        return shortcuts;
    }
    shortcuts.clear();
    loadedPath.clear();
    return shortcuts;
}

wxString GetShortcutName(uint32 shortcutID, uint32 accountID)
{
    const std::map<uint32, ShortcutInfo>& shortcuts = Shortcuts(accountID);
    auto it = shortcuts.find(shortcutID);
    return it != shortcuts.end() ? it->second.name : wxString();
}

wxString GetGameName(CGameID game, uint32 accountID)
{
    if (game.IsSteamApp())
    {
        return GetSteamAppName(game.AppID());
    }
    if (game.IsShortcut())
    {
        return GetShortcutName(game.ModID(), accountID);
    }
    return wxString();
}

wxString GetFriendGameName(CGameID game)
{
    if (!game.IsValid() || !game.IsSteamApp())
    {
        return wxString();
    }
    const AppId_t appID = game.AppID();
    static std::map<AppId_t, wxString> known;
    static std::map<AppId_t, wxLongLong> askedAt;
    auto it = known.find(appID);
    if (it != known.end())
    {
        return it->second;
    }
    const wxLongLong now = wxGetLocalTimeMillis();
    auto asked = askedAt.find(appID);
    if (asked != askedAt.end() && now - asked->second < 60000)
    {
        return wxString();
    }
    askedAt[appID] = now;

    wxString name = wxString::FromUTF8(GClientContext()->AppName(appID).c_str());
    if (name.empty())
    {
        const wxString local = GetSteamAppName(appID);
        if (local != wxString::Format("App %u", (unsigned)appID))
        {
            name = local;
        }
    }
    if (!name.empty())
    {
        known[appID] = name;
    }
    return name;
}

// The first of these files that exists in dir, or (with subfolders) in one of
// its subfolders.
static wxString FindFile(const wxString& dir, const std::vector<wxString>& names, bool subfolders)
{
    for (const wxString& name : names)
    {
        const wxString path = dir + "/" + name;
        if (wxFileExists(path))
        {
            return path;
        }
    }
    if (subfolders && wxDirExists(dir))
    {
        wxDir folder(dir);
        wxString sub;
        for (bool more = folder.IsOpened() && folder.GetFirst(&sub, wxEmptyString, wxDIR_DIRS); more;
             more = folder.GetNext(&sub))
        {
            for (const wxString& name : names)
            {
                const wxString path = dir + "/" + sub + "/" + name;
                if (wxFileExists(path))
                {
                    return path;
                }
            }
        }
    }
    return wxString();
}

wxString GetGameArtworkPath(CGameID game, uint32 accountID)
{
    for (const wxString& root : SteamRoots())
    {
        if (game.IsSteamApp())
        {
            // appcache/librarycache/<appid>/: library_hero.jpg is the wide
            // library banner, header.jpg the store one. Some files sit in a
            // subfolder named by a hash. Older Steam versions kept them all in
            // librarycache itself, as <appid>_library_hero.jpg and so on.
            const unsigned appID = (unsigned)game.AppID();
            const wxString cache = root + "/appcache/librarycache";
            const wxString app = wxString::Format("%s/%u", cache, appID);
            wxString path = FindFile(app, { "library_hero.jpg", "library_hero.png" }, true);
            if (path.empty())
            {
                path = FindFile(app, { "header.jpg", "library_header.jpg" }, true);
            }
            if (path.empty())
            {
                path = FindFile(cache, { wxString::Format("%u_library_hero.jpg", appID),
                                         wxString::Format("%u_header.jpg", appID) }, false);
            }
            if (!path.empty())
            {
                return path;
            }
        }
        else if (game.IsShortcut())
        {
            // Custom artwork for a non-Steam game (set in Steam, or by tools
            // such as SteamGridDB): userdata/<account>/config/grid/<id>_hero.png
            // and similar, by the shortcut's id (older versions: the 64-bit game id).
            const unsigned id = (unsigned)game.ModID();
            const unsigned long long id64 = (unsigned long long)game.ToUint64();
            const wxString grid = wxString::Format("%s/userdata/%u/config/grid", root, (unsigned)accountID);
            const wxString path = FindFile(grid, { wxString::Format("%u_hero.png", id), wxString::Format("%u_hero.jpg", id),
                                                   wxString::Format("%u.png", id), wxString::Format("%u.jpg", id),
                                                   wxString::Format("%llu.png", id64), wxString::Format("%llu.jpg", id64) },
                                           false);
            if (!path.empty())
            {
                return path;
            }
        }
    }
    return wxString();
}

// When each Steam game was last played: UserLocalConfigStore > Software >
// Valve > Steam > apps > <appid> > LastPlayed in the text KeyValues file
// localconfig.vdf (keys in any case; comments and unquoted tokens allowed).
static void ParseLastPlayed(const std::string& vdf, std::map<AppId_t, long long>& lastPlayed)
{
    static const char* const kPath[] = { "UserLocalConfigStore", "Software", "Valve", "Steam", "apps" };
    std::vector<std::string> blocks;    // the keys of the blocks we're in
    std::string key;
    bool haveKey = false;
    size_t i = 0;
    const size_t n = vdf.size();
    while (i < n)
    {
        const char c = vdf[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
        {
            ++i;
            continue;
        }
        if (c == '/' && i + 1 < n && vdf[i + 1] == '/')
        {
            while (i < n && vdf[i] != '\n')
            {
                ++i;
            }
            continue;
        }
        if (c == '{')
        {
            blocks.push_back(haveKey ? key : std::string());
            haveKey = false;
            ++i;
            continue;
        }
        if (c == '}')
        {
            if (!blocks.empty())
            {
                blocks.pop_back();
            }
            haveKey = false;
            ++i;
            continue;
        }
        std::string token;
        if (c == '"')
        {
            for (++i; i < n && vdf[i] != '"'; ++i)
            {
                if (vdf[i] == '\\' && i + 1 < n)
                {
                    ++i;
                }
                token += vdf[i];
            }
            ++i;
        }
        else
        {
            while (i < n && vdf[i] != ' ' && vdf[i] != '\t' && vdf[i] != '\r' && vdf[i] != '\n' &&
                   vdf[i] != '{' && vdf[i] != '}' && vdf[i] != '"')
            {
                token += vdf[i++];
            }
        }
        if (!haveKey)
        {
            key = token;
            haveKey = true;
            continue;
        }
        // A "key" "value" pair.
        haveKey = false;
        if (blocks.size() != 6 || !SameKey(key, "LastPlayed"))
        {
            continue;
        }
        bool inApps = true;
        for (size_t level = 0; level < 5 && inApps; ++level)
        {
            inApps = SameKey(blocks[level], kPath[level]);
        }
        const unsigned long appID = strtoul(blocks[5].c_str(), nullptr, 10);
        const long long when = strtoll(token.c_str(), nullptr, 10);
        if (inApps && appID > 0 && when > 0)
        {
            lastPlayed[(AppId_t)appID] = when;
        }
    }
}

// Steam's tools, which show up as played but aren't games to share.
static bool IsSteamTool(AppId_t appID, const wxString& name)
{
    return appID == 480 || appID == 228980 || name.StartsWith("Proton") || name.StartsWith("Steam Linux Runtime") ||
           name.Contains("Redistributable") || name == "SteamVR";
}

// Whether a Steam game is installed (has an appmanifest in a library folder).
static bool IsInstalledSteamApp(AppId_t appID, const std::vector<wxString>& folders)
{
    for (const wxString& folder : folders)
    {
        if (wxFileExists(wxString::Format("%s/steamapps/appmanifest_%u.acf", folder, (unsigned)appID)))
        {
            return true;
        }
    }
    return false;
}

AppId_t GetSteamRunningAppID()
{
#ifdef __WXMSW__
    // Asked several times a second, so read at most every half second.
    static wxLongLong lastRead = 0;
    static AppId_t lastResult = 0;
    // The last id read, and whether it's an installed game (not a tool).
    static AppId_t checkedApp = 0;
    static bool checkedIsGame = false;
    const wxLongLong now = wxGetUTCTimeMillis();
    if (lastRead != 0 && now >= lastRead && now - lastRead < 500)
    {
        return lastResult;
    }
    lastRead = now;

    AppId_t app = 0;
    {
        wxLogNull noLog;
        wxRegKey key(wxRegKey::HKCU, "Software\\Valve\\Steam");
        long value = 0;
        if (key.Exists() && key.HasValue("RunningAppID") && key.QueryValue("RunningAppID", &value))
        {
            app = (AppId_t)(unsigned long)value;
        }
    }

    // Non-Steam games (ids with the top bit set) are left to what Steam shows
    // as played. An id that isn't installed is a launcher's (EA's, say), not
    // the game's.
    if (app != checkedApp)
    {
        checkedApp = app;
        checkedIsGame = app != 0 && (app & 0x80000000u) == 0 && IsInstalledSteamApp(app, LibraryFolders()) &&
                        !IsSteamTool(app, GetSteamAppName(app));
    }
    lastResult = checkedIsGame ? app : 0;
    return lastResult;
#else
    return 0;
#endif
}

std::vector<RecentGame> GetRecentGames(uint32 accountID, size_t max)
{
    static std::vector<RecentGame> cached;
    static wxString cachedKey;

    // Steam games from localconfig.vdf (the first Steam install that has it).
    wxString localConfig;
    for (const wxString& root : SteamRoots())
    {
        const wxString path = wxString::Format("%s/userdata/%u/config/localconfig.vdf", root, (unsigned)accountID);
        if (wxFileExists(path))
        {
            localConfig = path;
            break;
        }
    }
    const std::map<uint32, ShortcutInfo>& shortcuts = Shortcuts(accountID);
    // Worked out again only when either file has changed.
    const wxDateTime changed = localConfig.empty() ? wxDateTime() : wxFileName(localConfig).GetModificationTime();
    long long shortcutsStamp = 0;
    for (auto it = shortcuts.begin(); it != shortcuts.end(); ++it)
    {
        shortcutsStamp += it->second.lastPlayed + it->first;
    }
    const wxString key = wxString::Format("%u/%u/", (unsigned)accountID, (unsigned)max) +
                         (changed.IsValid() ? changed.FormatISOCombined() : wxString("-")) +
                         wxString::Format("/%u/%lld", (unsigned)shortcuts.size(), shortcutsStamp);
    if (key == cachedKey)
    {
        return cached;
    }

    std::vector<RecentGame> games;
    std::map<AppId_t, long long> lastPlayed;
    if (!localConfig.empty())
    {
        ParseLastPlayed(ReadTextFile(localConfig), lastPlayed);
    }
    std::vector<std::pair<long long, AppId_t>> byTime;
    for (auto it = lastPlayed.begin(); it != lastPlayed.end(); ++it)
    {
        byTime.push_back(std::make_pair(it->second, it->first));
    }
    std::sort(byTime.rbegin(), byTime.rend());
    const std::vector<wxString> folders = LibraryFolders();
    for (size_t i = 0; i < byTime.size() && games.size() < max; ++i)
    {
        const AppId_t appID = byTime[i].second;
        const bool installed = IsInstalledSteamApp(appID, folders);
        const wxString name = installed ? GetSteamAppName(appID) : wxString();
        if (!installed || IsSteamTool(appID, name))
        {
            continue;
        }
        RecentGame game;
        game.game = CGameID(appID);
        game.name = name;
        game.lastPlayed = byTime[i].first;
        games.push_back(game);
    }

    // Non-Steam games started through Steam.
    for (auto it = shortcuts.begin(); it != shortcuts.end(); ++it)
    {
        if (it->second.lastPlayed <= 0)
        {
            continue;
        }
        RecentGame game;
        // A shortcut's game id: its id in the top half, type 2 (shortcut) at bit 24.
        game.game = CGameID(((uint64)it->first << 32) | ((uint64)2 << 24));
        game.name = it->second.name;
        game.lastPlayed = it->second.lastPlayed;
        games.push_back(game);
    }

    std::stable_sort(games.begin(), games.end(), [](const RecentGame& a, const RecentGame& b)
    {
        return a.lastPlayed > b.lastPlayed;
    });
    if (games.size() > max)
    {
        games.resize(max);
    }
    cached = games;
    cachedKey = key;
    return games;
}

wxString GetGameThumbnailPath(CGameID game, uint32 accountID)
{
    for (const wxString& root : SteamRoots())
    {
        if (game.IsSteamApp())
        {
            const unsigned appID = (unsigned)game.AppID();
            const wxString cache = root + "/appcache/librarycache";
            const wxString app = wxString::Format("%s/%u", cache, appID);
            wxString path = FindFile(app, { "header.jpg", "library_header.jpg" }, true);
            if (path.empty())
            {
                path = FindFile(cache, { wxString::Format("%u_header.jpg", appID) }, false);
            }
            if (path.empty())
            {
                path = FindFile(app, { "library_hero.jpg", "library_hero.png" }, true);
            }
            if (!path.empty())
            {
                return path;
            }
        }
        else if (game.IsShortcut())
        {
            // userdata/<account>/config/grid: <id>.png is the wide grid picture.
            const unsigned id = (unsigned)game.ModID();
            const unsigned long long id64 = (unsigned long long)game.ToUint64();
            const wxString grid = wxString::Format("%s/userdata/%u/config/grid", root, (unsigned)accountID);
            const wxString path = FindFile(grid, { wxString::Format("%u.png", id), wxString::Format("%u.jpg", id),
                                                   wxString::Format("%u_hero.png", id), wxString::Format("%u_hero.jpg", id),
                                                   wxString::Format("%llu.png", id64), wxString::Format("%llu.jpg", id64) },
                                           false);
            if (!path.empty())
            {
                return path;
            }
        }
    }
    return wxString();
}