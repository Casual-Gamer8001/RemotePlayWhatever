#include <map>
#include <regex>
#include <string>
#include <vector>
#include <wx/datetime.h>
#include <wx/dir.h>
#include <wx/ffile.h>
#include <wx/filefn.h>
#include <wx/filename.h>
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
// shortcuts -> "<index>" -> { appid, AppName, Exe, ..., tags { ... } }.
static void ParseShortcuts(const std::string& data, std::map<uint32, wxString>& names)
{
    size_t pos = 0;
    int depth = 0;
    bool hasID = false;
    uint32 id = 0;
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
                    names[shortcutID] = wxString::FromUTF8(appName.c_str());
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
            if (pos + 4 > data.size())
            {
                return;
            }
            if (depth == 2 && SameKey(key, "appid"))
            {
                id = (uint32)(unsigned char)data[pos] |
                     ((uint32)(unsigned char)data[pos + 1] << 8) |
                     ((uint32)(unsigned char)data[pos + 2] << 16) |
                     ((uint32)(unsigned char)data[pos + 3] << 24);
                hasID = true;
            }
            pos += 4;
            break;
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

wxString GetShortcutName(uint32 shortcutID, uint32 accountID)
{
    // Re-read the file only when Steam has changed it (a shortcut was added or renamed).
    static std::map<uint32, wxString> names;
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
            names.clear();
            ParseShortcuts(ReadBinaryFile(path), names);
            loadedPath = path;
            loadedTime = modified;
        }

        auto it = names.find(shortcutID);
        if (it != names.end())
        {
            return it->second;
        }
    }
    return wxString();
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
