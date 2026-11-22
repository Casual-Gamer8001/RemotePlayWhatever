#ifndef SCREENSHARE_H
#define SCREENSHARE_H

#include <string>
#include <Steamworks.h>

// "Share my screen", for games Steam doesn't run (the Xbox app, Epic, ...).
// Remote Play Together needs a game running through Steam, so this makes one:
// a temporary non-Steam shortcut (Steam forgets it when it restarts) to this
// program with --share-screen, which Steam starts and which only waits until
// it's told to stop (or this copy of the app is gone). While it "runs", the
// whole screen is shared like for any non-Steam game, and the host starts the
// real game however they like.
namespace ScreenShare
{
    // This Steam lets the app make the shortcut.
    bool Available();
    // Make the shortcut and have Steam start it. False (with a reason for the
    // host) if that couldn't be done.
    bool Start(std::string& error);
    // Asked for, but Steam doesn't report it running yet (up to 30 s).
    bool Starting();
    // It's the game Steam reports running now.
    bool Active();
    // That game is the screen share.
    bool IsOurs(CGameID game);
    // Tell the placeholder to quit; the shortcut is removed once Steam has
    // seen it stop (Tick), or straight away when the app is closing.
    void Stop(bool closing = false);

    // What Tick noticed about a screen share nobody here stopped.
    enum TickResult
    {
        TickNothing,
        TickEnded,          // it stopped by itself (Steam's Stop or Exit game, a crash)
        TickNotStarted      // Steam didn't start it within 30 s
    };
    // From the app's timer: removes the shortcut of a stopped screen share,
    // and cleans up one that ended by itself or never started.
    TickResult Tick();

    // The placeholder (`--share-screen <pid>-<n>`, see RunPlaceholderArg):
    // waits until the copy of the app with that process ID tells it to stop
    // (its n-th screen share), or exits. No windows, no Steam. Returns the
    // exit code.
    int RunPlaceholder(unsigned long ownerPid, unsigned long start);
    // The same with the argument as given; a malformed one exits at once.
    int RunPlaceholderArg(const char* arg);
}

#endif // SCREENSHARE_H
