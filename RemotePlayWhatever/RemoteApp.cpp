#include <wx/cmdline.h>
#include <wx/image.h>
#include <wx/snglinst.h>
#include <wx/taskbar.h>
#include <wx/time.h>
#include <wx/utils.h>
#include "Updater.h"
#include "UpdateNeededDialog.h"
#include "RemoteApp.h"
#include "UiHelpers.h"
#include "wxSteamStuff.h"
#include "FriendsListFrame.h"
#include "ControlFrame.h"
#include "TrayIcon.h"
#include "ScreenShare.h"
#ifdef __WXMSW__
#include <cwchar>
#include <wx/msw/wrapwin.h>
#endif

static const wxCmdLineEntryDesc cmdLineDesc[] =
{
    { wxCMD_LINE_SWITCH, "h", "help",    "show this help message" },
    { wxCMD_LINE_SWITCH, "v", "verbose", "verbose output" },
    { wxCMD_LINE_OPTION, "a", "appid",   "AppID for non-steam games", wxCMD_LINE_VAL_NUMBER },
    { wxCMD_LINE_OPTION, "i", "invite",  "Send remote play invites to provided SteamID64s ( use 0 for guest link )", wxCMD_LINE_VAL_STRING },
    { wxCMD_LINE_OPTION, "g", "guestid", "Guest client id", wxCMD_LINE_VAL_NUMBER },
    { wxCMD_LINE_OPTION, "c", "cancel",  "Cancel invite for provided SteamID64 and GuestID", wxCMD_LINE_VAL_STRING },
    { wxCMD_LINE_OPTION, nullptr, "updated-from", "process id of the copy that just updated this one",
      wxCMD_LINE_VAL_NUMBER, wxCMD_LINE_HIDDEN },
    { wxCMD_LINE_SWITCH, nullptr, "minimized", "start hidden in the tray, waiting for Steam if it isn't running yet "
      "(how it starts with Windows)" },
    { wxCMD_LINE_NONE }
};

RemoteApp::RemoteApp():
    m_friendsList(nullptr),
    m_inviteHandler(nullptr),
    m_controlFrame(nullptr),
    m_singleInstance(nullptr),
    m_oneshot(false),
    m_guestID(1),
    m_nonSteamID(0),
    m_inviteToCancel(-1),
    m_updatedFrom(0),
    m_startMinimized(false)
{
}

#ifdef __WXMSW__
// The main window of the copy that's already running, even while it's hidden
// in the tray. Its message boxes can have the same title, so skip dialogs.
static HWND FindRunningMainWindow()
{
    HWND wnd = nullptr;
    while ((wnd = ::FindWindowExW(nullptr, wnd, nullptr, L"Remote Play Whatever")) != nullptr)
    {
        wchar_t cls[64] = {};
        ::GetClassNameW(wnd, cls, 64);
        if (wcscmp(cls, L"#32770") != 0)
        {
            return wnd;
        }
    }
    return nullptr;
}
#endif

bool RemoteApp::OnInit()
{
    // Settings (sounds, what each friend may control) live under this name,
    // e.g. HKEY_CURRENT_USER\Software\RemotePlayWhatever on Windows.
    SetAppName("RemotePlayWhatever");

    if (!wxApp::OnInit())
        return false;

    // Game pictures in the main window are JPEG and PNG files.
    wxInitAllImageHandlers();

    // Started by an update: let the old copy finish closing first, or the
    // single-instance check below would find it and give up.
    if (!m_oneshot && m_updatedFrom)
    {
        AppUpdater::WaitForOldCopy(m_updatedFrom);
    }

    // Only one window at a time; a second launch brings the open one back
    // (from the tray, if it's hidden there). Command-line invites (one-shot
    // mode) can still run alongside it.
    // What a version started by the updater needs to start the same way.
    wxString arguments;
    if (m_startMinimized)
    {
        arguments += " --minimized";
    }
    if (m_nonSteamID)
    {
        arguments += wxString::Format(" -a %u", (unsigned)m_nonSteamID);
    }
    if (!m_oneshot)
    {
        m_singleInstance = new wxSingleInstanceChecker();
        if (m_singleInstance->Create("RemotePlayWhatever-" + wxGetUserId()) &&
            m_singleInstance->IsAnotherRunning())
        {
            bool activated = false;
#ifdef __WXMSW__
            HWND other = FindRunningMainWindow();
            if (other)
            {
                // Let the other copy bring itself to the front; Windows only
                // allows that because this (just launched) copy says so.
                DWORD pid = 0;
                ::GetWindowThreadProcessId(other, &pid);
                ::AllowSetForegroundWindow(pid);
                activated = ::PostMessageW(other, ::RegisterWindowMessageW(L"RemotePlayWhatever-Show"), 0, 0) != FALSE;
            }
#endif
            if (!activated)
            {
                wxMessageBox("Remote Play Whatever is already open.", "Remote Play Whatever", wxOK | wxICON_INFORMATION);
            }
            delete m_singleInstance;
            m_singleInstance = nullptr;
            return false;
        }

        // The only copy running: remove what an earlier update left behind.
        AppUpdater::CleanUp();
        RefreshStartWithWindows();

        // The last update's new version (this one, then) was tried and never
        // started properly: put the version before it back and start that.
        if (AppUpdater::RollBackFailedUpdate(arguments))
        {
            delete m_singleInstance;
            m_singleInstance = nullptr;
            return false;
        }
    }

    bool steamReady = GClientContext()->Init();
    // Started with Windows: Steam may still be starting or signing in, so wait
    // for it quietly (up to 10 minutes) instead of showing an error at sign-in.
    if (!steamReady && !m_oneshot && m_startMinimized &&
        GClientContext()->InitError() != ClientContext::InitMethodsMissing)
    {
        // Meanwhile, a copy opened by hand may take over (it gets the window),
        // and this one may be stopped (Windows shutting down): neither says
        // anything about whether an update works, so it isn't being tried.
        AppUpdater::UpdateTryInconclusive();
        delete m_singleInstance;
        m_singleInstance = nullptr;
        const wxLongLong giveUp = wxGetLocalTimeMillis() + 10 * 60 * 1000;
        while (!steamReady && GClientContext()->InitError() != ClientContext::InitMethodsMissing &&
               wxGetLocalTimeMillis() < giveUp)
        {
            wxMilliSleep(5000);
            steamReady = GClientContext()->Init();
        }
        m_singleInstance = new wxSingleInstanceChecker();
        if (!m_singleInstance->Create("RemotePlayWhatever-" + wxGetUserId()) || m_singleInstance->IsAnotherRunning())
        {
            return false;
        }
        if (!steamReady && GClientContext()->InitError() != ClientContext::InitMethodsMissing)
        {
            return false;   // no Steam this time; nothing to complain about
        }
        // Steam is here (or its functions changed): from now on this start is
        // the update's try again, unless another copy found it failed meanwhile.
        if (AppUpdater::RollBackFailedUpdate(arguments))
        {
            delete m_singleInstance;
            m_singleInstance = nullptr;
            return false;
        }
    }

    if (!steamReady)
    {
        // Steam not running or nobody signed in says nothing about whether a
        // new version works (one that can't use this Steam's functions does).
        if (!m_oneshot && GClientContext()->InitError() != ClientContext::InitMethodsMissing)
        {
            AppUpdater::UpdateTryInconclusive();
        }
        wxString why;
        switch (GClientContext()->InitError())
        {
        case ClientContext::InitNotSignedIn:
            why = "Steam is running, but nobody is signed in. Sign in to Steam, then open Remote Play Whatever again.";
            break;
        case ClientContext::InitMethodsMissing:
            why = "A Steam update changed the internal Remote Play functions this app uses, "
                  "so Remote Play Whatever needs an update before it can work with this Steam version.";
            break;
        default:
            why = "Steam isn't running. Start Steam and sign in, then open Remote Play Whatever again.";
            break;
        }

        // The fix for a Steam change is a newer version, and the main window's
        // Update button is out of reach, so look for one right away.
        if (!m_oneshot && GClientContext()->InitError() == ClientContext::InitMethodsMissing &&
            AppUpdater::Supported())
        {
            std::cout << "Error: Could not initialize steam client library! " << why.utf8_str().data() << std::endl;
            UpdateNeededDialog dialog(why);
            dialog.ShowModal();
            return false;
        }

#ifndef _WIN32
        if(!m_oneshot)
#endif
            wxMessageBox
            (
                why,
                "Remote Play Whatever",
                wxOK | wxICON_ERROR
            );

        std::cout << "Error: Could not initialize steam client library! " << why.utf8_str().data() << std::endl;

        return false;
    }

    return true;
}

int RemoteApp::OnRun()
{
    m_inviteHandler = new RemotePlayInviteHandler();
    m_inviteHandler->SetGuestID(m_guestID);
    m_inviteHandler->SetNonSteamAppID(m_nonSteamID);

    if(!m_oneshot)
    {
        m_callbackRunner.Start(200);

        // Light or dark from the start (the windows read the palette as they're built).
        ApplySavedTheme();
        m_friendsList = new FriendsListFrame(m_inviteHandler);

        // Hidden until a guest connects, then it pops itself up.
        m_controlFrame = new ControlFrame(m_inviteHandler);
        if (m_friendsList->GuestLinkHotKeyUsable())
        {
            m_controlFrame->AddShortcutHint('L', "copy a guest link");
        }

        // Icon next to the clock; minimizing the main window hides it there.
        const bool tray = wxTaskBarIcon::IsAvailable();
        if (tray)
        {
            m_friendsList->SetTray(new RpwTrayIcon(m_friendsList, m_controlFrame, m_inviteHandler));
        }
        // Title bars, scroll bars and tooltips too, now that the windows exist.
        ApplySavedTheme();
        // This version starts properly: an update to it is done (see
        // AppUpdater::RollBackFailedUpdate).
        AppUpdater::MarkStartedOK();
        // Started with Windows: stay in the tray until the host needs it.
        m_friendsList->Show(!(m_startMinimized && tray));

        const long rolledBackFrom = AppUpdater::TakeRolledBackFrom();
        if (rolledBackFrom > 0)
        {
            m_friendsList->ShowNotice(wxString::Format("Build %ld didn't start properly, so Remote Play Whatever went back "
                                                       "to build %ld.", rolledBackFrom, AppUpdater::CurrentBuild()),
                                      FriendsListFrame::NoticeWarning);
        }
        else if (m_updatedFrom)
        {
            m_friendsList->ShowNotice(wxString::Format("Updated to build %ld.", AppUpdater::CurrentBuild()));
        }

        return wxApp::OnRun();
    }

    if(!m_inviteQue.empty() &&
        GetRunningGameID().IsValid() &&
        GClientContext()->SteamUser()->BLoggedOn()
    )
    {
        // No one to ask on the command line: if Steam won't create a group,
        // send anyway like stock RPW did. And the main window may be running a
        // session in the group this finds, so it's never disbanded here.
        m_inviteHandler->SetAutoFallback(true);
        m_inviteHandler->SetKeepExistingGroups(true);
        QueueInviter qinviter(m_inviteHandler, &m_inviteQue);
        qinviter.SendInvites();
        while(qinviter.Running())
        {
            // look at me... i'm the main event loop now
            GClientContext()->RunCallbacks();
            m_inviteHandler->GroupTick();   // fire the async (group-gated) invite

            wxMilliSleep(200);
        }
    }

    if(m_inviteToCancel != -1)
    {
        m_inviteHandler->CancelInvite(m_inviteToCancel, m_guestID);
    }

    return 0;
}

int RemoteApp::OnExit()
{
    if (m_callbackRunner.IsRunning())
    {
        m_callbackRunner.Stop();
    }

    // A screen share's placeholder quits with this copy anyway; its temporary
    // shortcut goes too.
    if (!m_oneshot)
    {
        ScreenShare::Stop(true);
    }

    if(m_inviteHandler)
    {
        delete m_inviteHandler;
    }

    GClientContext()->Shutdown();

    delete m_singleInstance;
    m_singleInstance = nullptr;

    return 0;
}

bool RemoteApp::OnCmdLineParsed(wxCmdLineParser &parser)
{
    if(parser.FoundSwitch("h"))
    {
        parser.Usage();

        wxTheApp->Exit();
    }

    long appID;
    if(parser.Found("a", &appID))
    {
        m_nonSteamID = appID;
    }

    long guestID;
    if(parser.Found("g", &guestID))
    {
        m_guestID = guestID;
    }

    wxString inviteeStr64;
    if(parser.Found("i", &inviteeStr64))
    {
        m_oneshot = true;
        size_t first = 0, last = 0;
        while(last != wxString::npos)
        {
            last = inviteeStr64.find(",", first);
            uint64 invitee = 0;
            if(inviteeStr64.substr(first, last - first).ToULongLong(&invitee))
            {
                m_inviteQue.push(invitee);
            }
            first = last + 1;
        }
    }
    else if (parser.Found("c", &inviteeStr64))
    {
        m_oneshot = true;
        inviteeStr64.ToULongLong(&m_inviteToCancel);
    }

    long updatedFrom = 0;
    if (parser.Found("updated-from", &updatedFrom) && updatedFrom > 0)
    {
        m_updatedFrom = (unsigned long)updatedFrom;
    }
    m_startMinimized = parser.FoundSwitch("minimized") == wxCMD_SWITCH_ON;

    return wxApp::OnCmdLineParsed(parser);
}

void RemoteApp::OnInitCmdLine(wxCmdLineParser &parser)
{
    parser.SetDesc(cmdLineDesc);
}

// --

void RemoteAppCallbackRunner::Notify()
{
    GClientContext()->RunCallbacks();

    RemoteApp* app = static_cast<RemoteApp*>(wxTheApp);
    if (app && app->GetInviteHandler())
    {
        // Advance async group creation / flush queued invites every tick.
        app->GetInviteHandler()->GroupTick();
        // End the session if the shared game has closed.
        app->GetInviteHandler()->SessionTick();

        // Re-assert guest control roughly every 2s (10 ticks @ 200ms) so Steam
        // can't silently reset the per-user input we granted.
        static int s_tick = 0;
        if (++s_tick >= 10)
        {
            s_tick = 0;
            app->GetInviteHandler()->GrantControlTick();
        }
    }
}


