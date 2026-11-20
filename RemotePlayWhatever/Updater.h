#ifndef UPDATER_H
#define UPDATER_H
#include <functional>
#include <wx/defs.h>
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
        Failed,
    };

    // onChange is called (on the GUI thread) whenever the state changes.
    explicit AppUpdater(std::function<void()> onChange);

    // A release build on Windows (it knows its build number and its releases).
    static bool Supported();
    static long CurrentBuild();
    // The releases page, for downloading by hand.
    static wxString ReleasesPage();

    // "Check for updates automatically" (saved setting, on by default).
    static bool AutoChecksEnabled();
    static void SetAutoChecksEnabled(bool enabled);

    // Check a little after startup and then every few hours, while the
    // setting above is on.
    void StartAutomaticChecks();
    // Check now. manual = the user asked, so the outcome is worth telling them.
    void Check(bool manual);
    // Download the available update, verify it, put it in place and start it.
    // Progress and the outcome come through the change callback.
    void Install();
    // Stop talking to the window (it's closing) and cancel any download.
    void Shutdown();

    State GetState() const { return m_state; }
    bool LastCheckWasManual() const { return m_manual; }
    // The newer build found by the last successful check, or 0.
    long AvailableBuild() const { return m_availableBuild; }
    bool Busy() const { return m_state == Checking || m_state == Downloading || m_state == Installing || m_state == Restarting; }
    const wxString& Error() const { return m_error; }

    // In the copy started after an update: wait until the old copy (process
    // oldProcessId) has exited, so the single-instance check doesn't find it.
    static void WaitForOldCopy(unsigned long oldProcessId);
    // Remove what an update leaves behind (harmless when there's nothing).
    static void CleanUp();

private:
    enum Stage { StageInfo, StagePackage };

    void SetState(State state);
    void Fail(const wxString& error);
    void ReadInfo(const wxString& text);
    void FinishInstall();
    void OnTimer(wxTimerEvent& event);

    std::function<void()> m_onChange;
    State m_state;
    bool m_manual;
    bool m_shutdown;
    wxString m_error;
    long m_availableBuild;
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
