#include <wx/defs.h>
#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>
#endif
#include <algorithm>
#include <ctime>
#include <string>
#include <vector>
#include <wx/config.h>
#include <wx/filename.h>
#include <wx/time.h>
#include <wx/utils.h>
#include "SteamStuff.h"
#include "ProgramLauncher.h"

namespace
{
    const unsigned kMaxSaved = 5;
    const long kStartTimeoutMs = 45000;

    uint32 g_appID = 0;                     // the shortcut made, while it's in Steam
    ProgramLauncher::Program g_program;     // the program started last
    bool g_seenRunning = false;             // Steam has reported it running
    wxLongLong g_startedAt = 0;
    wxLongLong g_goneSince = 0;             // seen running, then not since then (0: running)

    // The same file: on Windows whatever the case and slashes.
    wxString ComparablePath(const wxString& path)
    {
#ifdef __WXMSW__
        wxString text = path;
        text.Replace("/", "\\");
        return text.Lower();
#else
        return path;
#endif
    }

    bool SameProgram(const ProgramLauncher::Program& a, const ProgramLauncher::Program& b)
    {
        return ComparablePath(a.path) == ComparablePath(b.path) && a.options == b.options;
    }

    bool NewestFirst(const ProgramLauncher::Program& a, const ProgramLauncher::Program& b)
    {
        return a.lastPlayed > b.lastPlayed;
    }

    wxString EntryKey(unsigned number, const char* field)
    {
        return wxString::Format("/Programs/%u/", number) + field;
    }

    void Save(std::vector<ProgramLauncher::Program> programs)
    {
        wxConfigBase* config = wxConfigBase::Get();
        if (!config)
        {
            return;
        }
        std::stable_sort(programs.begin(), programs.end(), NewestFirst);
        config->DeleteGroup("/Programs");
        for (unsigned i = 0; i < programs.size() && i < kMaxSaved; ++i)
        {
            config->Write(EntryKey(i + 1, "Name"), programs[i].name);
            config->Write(EntryKey(i + 1, "Path"), programs[i].path);
            config->Write(EntryKey(i + 1, "Options"), programs[i].options);
            config->Write(EntryKey(i + 1, "LastPlayed"), wxString::Format("%lld", programs[i].lastPlayed));
        }
        config->Flush();
    }

    void RemoveShortcutNow()
    {
        if (g_appID)
        {
            GClientContext()->Shortcuts()->RemoveShortcut(g_appID);
            clientvt::Log("[rp] program: removed shortcut %u\n", g_appID);
        }
        g_appID = 0;
        g_seenRunning = false;
        g_goneSince = 0;
    }
}

namespace ProgramLauncher
{
    bool Available()
    {
        return GClientContext()->Shortcuts()->CanAddTemporary();
    }

    bool ProgramExists(const wxString& path)
    {
        if (path.empty())
        {
            return false;
        }
        if (wxFileName::FileExists(path))
        {
            return true;
        }
#ifdef __WXOSX__
        return path.Lower().EndsWith(".app") && wxFileName::DirExists(path);
#else
        return false;
#endif
    }

    wxString DefaultName(const wxString& path)
    {
        const wxString name = wxFileName(path).GetName();
        return name.empty() ? path : name;
    }

    uint64 KeyOf(const Program& program)
    {
        // FNV-1a of the file and the launch options.
        const wxString text = ComparablePath(program.path) + "\n" + program.options;
        const wxScopedCharBuffer utf8 = text.utf8_str();
        uint64 hash = 14695981039346656037ull;
        for (const char* c = utf8.data(); c && *c; ++c)
        {
            hash ^= (unsigned char)*c;
            hash *= 1099511628211ull;
        }
        // A game ID's type byte (bits 24-31) is 0 to 3; 0xFF makes it none.
        return (hash & 0xFFFFFFFF00FFFFFFull) | 0x00000000FF000000ull;
    }

    std::vector<Program> Saved()
    {
        std::vector<Program> programs;
        wxConfigBase* config = wxConfigBase::Get();
        if (!config)
        {
            return programs;
        }
        // Read back exactly as saved: wxConfig would expand $NAME and %NAME%
        // (and drop a backslash before $ or %), which can break a path.
        const bool expanding = config->IsExpandingEnvVars();
        config->SetExpandEnvVars(false);
        for (unsigned i = 1; i <= kMaxSaved; ++i)
        {
            Program program;
            if (!config->Read(EntryKey(i, "Path"), &program.path) || program.path.empty())
            {
                continue;
            }
            config->Read(EntryKey(i, "Name"), &program.name);
            config->Read(EntryKey(i, "Options"), &program.options);
            wxString lastPlayed;
            wxLongLong_t time = 0;
            if (config->Read(EntryKey(i, "LastPlayed"), &lastPlayed) && lastPlayed.ToLongLong(&time))
            {
                program.lastPlayed = (long long)time;
            }
            if (program.name.empty())
            {
                program.name = DefaultName(program.path);
            }
            programs.push_back(program);
        }
        config->SetExpandEnvVars(expanding);
        std::stable_sort(programs.begin(), programs.end(), NewestFirst);
        return programs;
    }

    void Remember(const Program& program)
    {
        std::vector<Program> programs = Saved();
        programs.erase(std::remove_if(programs.begin(), programs.end(),
                                      [&program](const Program& p) { return SameProgram(p, program); }),
                       programs.end());
        Program played = program;
        played.lastPlayed = (long long)std::time(nullptr);
        programs.insert(programs.begin(), played);
        Save(programs);
    }

    void Replace(const Program& old, const Program& changed)
    {
        std::vector<Program> programs = Saved();
        long long lastPlayed = old.lastPlayed;
        for (size_t i = 0; i < programs.size(); ++i)
        {
            if (SameProgram(programs[i], old))
            {
                lastPlayed = programs[i].lastPlayed;
            }
        }
        // Without the old one, and without another that's now the same.
        programs.erase(std::remove_if(programs.begin(), programs.end(),
                                      [&old, &changed](const Program& p)
                                      { return SameProgram(p, old) || SameProgram(p, changed); }),
                       programs.end());
        Program updated = changed;
        updated.lastPlayed = lastPlayed;
        programs.push_back(updated);
        Save(programs);
    }

    void Forget(const Program& program)
    {
        std::vector<Program> programs = Saved();
        programs.erase(std::remove_if(programs.begin(), programs.end(),
                                      [&program](const Program& p) { return SameProgram(p, program); }),
                       programs.end());
        Save(programs);
    }

    bool IsOurs(CGameID game)
    {
        return g_appID && game.IsValid() && game.IsShortcut() && game.ModID() == g_appID;
    }

    bool Starting()
    {
        return g_appID && !g_seenRunning && !IsOurs(GetRunningGameID()) &&
               wxGetLocalTimeMillis() - g_startedAt < kStartTimeoutMs;
    }

    wxString Name()
    {
        return g_program.name;
    }

    uint64 Key()
    {
        return KeyOf(g_program);
    }

    bool Start(const Program& program, std::string& error)
    {
        if (g_appID)
        {
            if (IsOurs(GetRunningGameID()) || Starting())
            {
                error = "A program is starting already.";
                return false;
            }
            // One that stopped or never started, not cleaned up yet.
            RemoveShortcutNow();
        }
        CShortcutsByName* shortcuts = GClientContext()->Shortcuts();
        if (!shortcuts->CanAddTemporary())
        {
            error = "This Steam version doesn't let Remote Play Whatever start programs.";
            return false;
        }
        const wxString name = program.name.empty() ? DefaultName(program.path) : program.name;
        const std::string nameText = name.utf8_str().data();
        if (!ProgramExists(program.path))
        {
            error = nameText + " isn't there any more: " + std::string(program.path.utf8_str().data());
            return false;
        }

        // Steam keeps a temporary shortcut's program and folder as given, so
        // they go in without quotes (Steam couldn't start a quoted one).
        const std::string exe = program.path.utf8_str().data();
        const std::string folder = wxFileName(program.path).GetPath().utf8_str().data();
        const std::string options = program.options.utf8_str().data();
        const uint32 appID = shortcuts->AddTemporaryShortcut(nameText.c_str(), exe.c_str(), "");
        if (!appID)
        {
            error = "Steam didn't add " + nameText + ".";
            return false;
        }
        shortcuts->SetShortcutStartDir(appID, folder.c_str());
        if (!options.empty())
        {
            shortcuts->SetShortcutCommandLine(appID, options.c_str());
        }
        // Started like any game (Recent games' Play does the same), so Steam
        // tracks it as the running game.
        const uint64 gameID = ((uint64)appID << 32) | 0x02000000ull;
        const bool launched = wxLaunchDefaultBrowser(wxString::Format("steam://rungameid/%llu", (unsigned long long)gameID));
        clientvt::Log("[rp] program: shortcut %u for %s in %s, options [%s] -> steam://rungameid/%llu %s\n", appID,
                      exe.c_str(), folder.c_str(), options.c_str(), (unsigned long long)gameID,
                      launched ? "opened" : "FAILED to open");
        if (!launched)
        {
            shortcuts->RemoveShortcut(appID);
            error = "Steam didn't start " + nameText + ".";
            return false;
        }
        g_appID = appID;
        g_program = program;
        g_program.name = name;
        g_seenRunning = false;
        g_goneSince = 0;
        g_startedAt = wxGetLocalTimeMillis();
        return true;
    }

    TickResult Tick()
    {
        if (!g_appID)
        {
            return TickNothing;
        }
        const wxLongLong now = wxGetLocalTimeMillis();
        if (IsOurs(GetRunningGameID()))
        {
            if (!g_seenRunning)
            {
                clientvt::Log("[rp] program: Steam runs shortcut %u\n", g_appID);
            }
            g_seenRunning = true;
            g_goneSince = 0;
            return TickNothing;
        }
        if (g_seenRunning)
        {
            // What Steam reports can lag a moment, so it has to be gone for 3 s.
            if (g_goneSince == 0)
            {
                g_goneSince = now;
            }
            if (now - g_goneSince < 3000)
            {
                return TickNothing;
            }
            clientvt::Log("[rp] program: it stopped\n");
            RemoveShortcutNow();
            return TickEnded;
        }
        if (now - g_startedAt >= kStartTimeoutMs)
        {
            clientvt::Log("[rp] program: Steam didn't start it\n");
            RemoveShortcutNow();
            return TickNotStarted;
        }
        return TickNothing;
    }

    void Shutdown()
    {
        if (!g_appID)
        {
            return;
        }
        if (IsOurs(GetRunningGameID()) || Starting())
        {
            clientvt::Log("[rp] program: still running, shortcut %u left for Steam to forget\n", g_appID);
            return;
        }
        RemoveShortcutNow();
    }
}
