#ifndef UPDATER_H
#define UPDATER_H
#include <functional>
#include <wx/defs.h>
#include <wx/arrstr.h>
#include <wx/event.h>
#include <wx/string.h>
#include <wx/timer.h>

// Release builds for Windows keep themselves up to date from the GitHub
// releases they were built for: each release has an update.txt next to the
// package saying which build it is, where the package is and its SHA-256.
#if defined(__WXMSW__) && wxUSE_WEBREQUEST
#define RPW_HAS_UPDATER 1
#include <wx/webrequest.h>
#else
#define RPW_HAS_UPDATER 0
#endif

class AppUpdater : public wxEvtHandler
{
public:
    enum State
    {
        Idle,
        Checking,
        UpToDate,
        Available,
        Downloading,
        Installing,
        Restarting,     // the new version has been started; close this one
        Postponed,      // downloaded, but not installed: restarting isn't allowed right now
        Failed,
    };

    // onChange is called (on the GUI thread) whenever the state changes.
    // canRestart is asked right before the files are swapped (restarting
    // would end a Remote Play session, for example).
    AppUpdater(std::function<void()> onChange, std::function<bool()> canRestart);

    // A release build on Windows (it knows its build number and its releases).
    static bool Supported();
    static long CurrentBuild();
    // The releases page, for downloading by hand.
    static wxString ReleasesPage();
    // The project on GitHub this build comes from (the original project for
    // local builds, which don't know).
    static wxString ProjectPage();

    // "Check for updates automatically" (saved setting, on by default).
    static bool AutoChecksEnabled();
    static void SetAutoChecksEnabled(bool enabled);
    // "Install updates on launch" (saved setting, on by default): an update
    // the check right after startup finds is installed straight away.
    static bool InstallsOnLaunch();
    static void SetInstallsOnLaunch(bool enabled);

    // Check right after startup and then every few hours, while the setting
    // above is on.
    void StartAutomaticChecks();
    // Check now. manual = the user asked, so the outcome is worth telling them.
    void Check(bool manual);
    // Download the available update, verify it, put it in place and start it.
    // Progress and the outcome come through the change callback. automatic:
    // nobody clicked, so a failure needn't be reported (LastCheckWasManual).
    void Install(bool automatic = false);
    // The newly found update should install itself: it was found by the check
    // right after startup, the setting is on, and it isn't a build that was
    // installed before but didn't start (so a bad release can't make the app
    // restart over and over).
    bool ShouldInstallNow() const;
    // Extra command-line arguments for the new version (asked right before
    // it's started), e.g. " --minimized" to stay in the tray.
    void SetRestartArguments(std::function<wxString()> arguments) { m_restartArguments = arguments; }
    // Stop talking to the window (it's closing) and cancel any download.
    void Shutdown();

    State GetState() const { return m_state; }
    bool LastCheckWasManual() const { return m_manual; }
    // The newer build found by the last successful check, or 0.
    long AvailableBuild() const { return m_availableBuild; }
    // What's new in it (the release's change titles, oldest first), possibly none.
    const wxArrayString& Notes() const { return m_notes; }
    // The notes on one line ("a; b; c"), cut to about maxChars.
    wxString NotesSummary(size_t maxChars) const;
    // The notes as a list, one "- note" per line.
    wxString NotesList() const;
    bool Busy() const { return m_state == Checking || m_state == Downloading || m_state == Installing || m_state == Restarting; }
    // "Check for updates" can be clicked now (also during an automatic check,
    // whose answer is then shown).
    bool CanCheckNow() const { return m_state == Checking || !Busy(); }
    const wxString& Error() const { return m_error; }

    // In the copy started after an update: wait until the old copy (process
    // oldProcessId) has exited, so the single-instance check doesn't find it.
    static void WaitForOldCopy(unsigned long oldProcessId);
    // Remove what an update leaves behind (harmless when there's nothing).
    static void CleanUp();

    // A safety net for updates that don't start: the files an update replaced
    // stay (as X.old) until the new version has started properly. Called early
    // at every start, before Steam is touched: if the last update's new version
    // was already tried and never got that far, its files are put back, the old
    // version is started (with arguments) and this returns true: this copy
    // should quit. Otherwise this start becomes the try.
    static bool RollBackFailedUpdate(const wxString& arguments);
    // This start couldn't show whether the new version works (Steam wasn't
    // running, say), so the next start tries again.
    static void UpdateTryInconclusive();
    // The new version works: remove the files it replaced.
    static void MarkStartedOK();
    // The build a rollback came back from (once; 0 if there was none).
    static long TakeRolledBackFrom();

private:
    enum Stage { StageInfo, StagePackage };

    void StartCheck(bool manual, bool atLaunch);
    void SetState(State state);
    void Fail(const wxString& error);
    void ReadInfo(const wxString& text);
    void FinishInstall();
    void OnTimer(wxTimerEvent& event);

    std::function<void()> m_onChange;
    std::function<bool()> m_canRestart;
    std::function<wxString()> m_restartArguments;
    State m_state;
    bool m_manual;
    bool m_atLaunch;        // the current or last check is the one right after startup
    bool m_checkedOnce;     // the automatic checks' timer has fired before
    bool m_shutdown;
    wxString m_error;
    long m_availableBuild;
    wxArrayString m_notes;
    wxString m_tag;
    wxString m_file;
    wxString m_sha256;
    wxTimer m_timer;

#if RPW_HAS_UPDATER
    bool StartRequest(const wxString& url, Stage stage);
    void OnRequestState(wxWebRequestEvent& event);

    wxWebRequest m_request;
    Stage m_stage;
    int m_requestId;
#endif
};

#endif // UPDATER_H
