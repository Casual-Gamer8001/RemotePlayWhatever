#include <wx/defs.h>
#include <wx/app.h>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "RemoteApp.h"
#include "ServeMode.h"
#include "ScreenShare.h"
#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>
#include <shellapi.h>
#endif

wxIMPLEMENT_APP_NO_MAIN(RemoteApp);

// Modes that need no window are picked before wxWidgets' GUI starts: the
// screen share's placeholder (started by Steam) and --serve (no display may
// exist). -1: neither, so the app starts normally.
static int RunWithoutWindows(const std::vector<std::string>& args, int argc, char** argv)
{
    bool serve = false;
    unsigned appID = 0;
    for (size_t i = 1; i < args.size(); ++i)
    {
        const std::string& a = args[i];
        if (a == "--share-screen")
        {
            return ScreenShare::RunPlaceholderArg(i + 1 < args.size() ? args[i + 1].c_str() : nullptr);
        }
        if (a.compare(0, 15, "--share-screen=") == 0)
        {
            return ScreenShare::RunPlaceholderArg(a.c_str() + 15);
        }
        if (a == "--serve")
        {
            serve = true;
        }
        else if ((a == "-a" || a == "--appid") && i + 1 < args.size())
        {
            appID = (unsigned)std::strtoul(args[i + 1].c_str(), nullptr, 10);
        }
        else if (a.compare(0, 8, "--appid=") == 0)
        {
            appID = (unsigned)std::strtoul(a.c_str() + 8, nullptr, 10);
        }
    }
    return serve ? RunServeMode(argc, argv, appID) : -1;
}

#ifdef __WXMSW__
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR, int nCmdShow)
{
    wxDISABLE_DEBUG_SUPPORT();
    int argc = 0;
    std::vector<std::string> args;
    LPWSTR* wide = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    for (int i = 0; wide && i < argc; ++i)
    {
        const int size = ::WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, nullptr, 0, nullptr, nullptr);
        std::string arg(size > 0 ? size - 1 : 0, '\0');
        if (size > 1)
        {
            ::WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, &arg[0], size, nullptr, nullptr);
        }
        args.push_back(arg);
    }
    if (wide)
    {
        ::LocalFree(wide);
    }
    std::vector<char*> argv;
    for (size_t i = 0; i < args.size(); ++i)
    {
        argv.push_back(&args[i][0]);
    }
    argv.push_back(nullptr);
    const int done = RunWithoutWindows(args, (int)args.size(), argv.data());
    if (done >= 0)
    {
        return done;
    }
    return wxEntry(hInstance, hPrevInstance, NULL, nCmdShow);
}
#else
int main(int argc, char** argv)
{
    wxDISABLE_DEBUG_SUPPORT();
    std::vector<std::string> args(argv, argv + argc);
    const int done = RunWithoutWindows(args, argc, argv);
    if (done >= 0)
    {
        return done;
    }
    return wxEntry(argc, argv);
}
#endif
