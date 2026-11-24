#ifndef GAMEPROCESS_H
#define GAMEPROCESS_H

#include <cstdint>

// The process ID of the running game that has Steam's in-game overlay loaded,
// or 0 if there's none (or on platforms where this isn't implemented).
//
// Steam ties a game's Remote Play Together group to this "overlay PID": it's
// the group the overlay's Remote Play menu (Shift+Tab) shows, and the one
// Steam's own invites use. If several processes have the overlay, the one
// started most recently wins. This walks every process's module list (about a
// quarter of a second), so the invite handler looks once per game rather than
// per invite or on a timer. (From the Rammy-3 fork.)
uint32_t FindGameOverlayPID();

// Whether this process is still running (false where that can't be told).
bool ProcessStillRunning(uint32_t pid);

// Steam shows Remote Play Together guests only a Steam game's own window, and
// only while it's in front; with any other window in front they see "Host is
// busy". These are for that (Windows only; elsewhere: FrontNone and false).
//
// Whose window is in front: the game's (a process with Steam's overlay that
// isn't this app or one of Steam's own programs), another program's (this app
// included), or nobody's (for a moment while Windows switches windows).
enum FrontWindow { FrontNone, FrontGame, FrontOther };
FrontWindow WindowInFront();
// Brings the game's frontmost window to the front, restoring it if it's
// minimized. Windows only lets the program in front do that. False if there's
// no game window or Windows refused. (Windows takes a moment to finish: the
// game has to answer first, and meanwhile no window is in front.)
bool BringGameWindowToFront();

#endif // GAMEPROCESS_H
