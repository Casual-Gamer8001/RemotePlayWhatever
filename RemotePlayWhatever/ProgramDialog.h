#ifndef PROGRAMDIALOG_H
#define PROGRAMDIALOG_H

#include <wx/window.h>
#include "ProgramLauncher.h"

// Asks which program to play through Steam (Play a program that isn't in
// Steam): its file, a name for it and launch options. With editing it
// changes a remembered one instead (Save rather than Play). False if
// cancelled; otherwise program holds the answer.
bool AskForProgram(wxWindow* parent, ProgramLauncher::Program& program, bool editing);

#endif // PROGRAMDIALOG_H
