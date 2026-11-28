#ifndef PROGRAMLAUNCHER_H
#define PROGRAMLAUNCHER_H

#include <string>
#include <vector>
#include <wx/string.h>
#include <Steamworks.h>

// "Play a program that isn't in Steam": Steam starts a game's or emulator's
// program as a temporary non-Steam game (a shortcut Steam forgets when it
// restarts, made like Share my screen's), so Remote Play Together shares it
// like a non-Steam game added to the library: guests hear it and see only its
// window while it's in front. Steam doesn't keep these programs, so the app
// remembers the ones played this way.
namespace ProgramLauncher
{
    struct Program
    {
        wxString name;
        wxString path;          // the program's file
        wxString options;       // launch options, passed to it as Steam passes a game's
        long long lastPlayed;   // Unix time, 0 = never
        Program() : lastPlayed(0) {}
    };

    // This Steam lets the app make the shortcut.
    bool Available();

    // The program's file is there (on a Mac, an app bundle counts too).
    bool ProgramExists(const wxString& path);
    // A name for a program from its file name ("Dolphin" for Dolphin.exe).
    wxString DefaultName(const wxString& path);
    // Stays the same for the same file and launch options, and is never a
    // real game ID (its type byte is 0xFF), so it can stand for the program
    // where a game's ID would be used.
    uint64 KeyOf(const Program& program);

    // The remembered programs, the most recently played first (at most 5).
    std::vector<Program> Saved();
    // Remember a program as just played (one with the same file and launch
    // options is replaced).
    void Remember(const Program& program);
    // Change a remembered program; it keeps its place in the list.
    void Replace(const Program& old, const Program& changed);
    void Forget(const Program& program);

    // Make the shortcut and have Steam start the program. False (with a
    // reason for the host) if that couldn't be done.
    bool Start(const Program& program, std::string& error);
    // Asked for, but Steam doesn't report it running yet (up to 45 s).
    bool Starting();
    // The game Steam reports running is the program started here.
    bool IsOurs(CGameID game);
    // The program started last (still known after it stops), and its key.
    wxString Name();
    uint64 Key();

    // What Tick noticed.
    enum TickResult
    {
        TickNothing,
        TickEnded,          // it stopped; its shortcut is gone
        TickNotStarted      // Steam didn't start it within 45 s; its shortcut is gone
    };
    // From the app's timer: removes the shortcut once the program has stopped
    // (or if Steam never started it).
    TickResult Tick();
    // The app is closing: the shortcut goes now, unless Steam is still
    // running (or starting) the program; Steam forgets it when it restarts.
    void Shutdown();
}

#endif // PROGRAMLAUNCHER_H
