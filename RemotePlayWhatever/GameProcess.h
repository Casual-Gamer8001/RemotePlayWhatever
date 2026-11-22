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

#endif // GAMEPROCESS_H
