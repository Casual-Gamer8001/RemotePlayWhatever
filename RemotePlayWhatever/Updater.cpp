#include <wx/defs.h>
#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>
#include <bcrypt.h>
#endif
#include <wx/config.h>
#include <wx/dir.h>
#include <wx/ffile.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/stdpaths.h>
#include <wx/tokenzr.h>
#include <wx/utils.h>
#include <wx/wfstream.h>
#include <wx/zipstrm.h>
#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "BuildInfo.h"
#include "Updater.h"

namespace
{
const char* const kInfoFile = "update.txt";
const char* const kProgramName = "RemotePlayWhatever.exe";
// The first check runs a second after the app starts (just after the window
// appears; the request doesn't hold anything up), then every 6 hours.
const int kFirstCheckMs = 1000;
const int kCheckEveryMs = 6 * 60 * 60 * 1000;

wxString Repo()
{
    return wxString::FromUTF8(RPW_UPDATE_REPO);
}

wxString Join(const wxString& dir, const wxString& name)
{
    return dir + wxFILE_SEP_PATH + name;
}

wxString ExePath()
{
    return wxStandardPaths::Get().GetExecutablePath();
}

wxString AppDir()
{
    return wxFileName(ExePath()).GetPath();
}

// Where a downloaded package is unpacked, next to the program so the files
// can be moved into place (a move can't cross drives).
wxString StagingDir()
{
    return Join(AppDir(), "update-staging");
}

wxString PackagePath()
{
    return Join(wxFileName::GetTempDir(), "RemotePlayWhatever-update.zip");
}

// update.txt of the newest release. RPW_UPDATE_INFO_URL can point at another
// release of the same repository instead, to test a branch's test build.
wxString InfoUrl()
{
    const wxString releases = "https://github.com/" + Repo() + "/releases/";
    wxString custom;
    if (wxGetEnv("RPW_UPDATE_INFO_URL", &custom) && custom.StartsWith(releases) && !custom.Contains(".."))
    {
        return custom;
    }
    return releases + "latest/download/" + kInfoFile;
}

// A plain file name (no folders), as in our releases and packages.
bool IsSafeName(const wxString& name)
{
    if (name.empty() || name.length() > 100 || name[0] == '.')
    {
        return false;
    }
    for (wxString::const_iterator it = name.begin(); it != name.end(); ++it)
    {
        wxUniChar c = *it;
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                  c == '.' || c == '_' || c == '-';
        if (!ok)
        {
            return false;
        }
    }
    return true;
}

bool IsSha256(const wxString& text)
{
    if (text.length() != 64)
    {
        return false;
    }
    for (wxString::const_iterator it = text.begin(); it != text.end(); ++it)
    {
        wxUniChar c = *it;
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
        {
            return false;
        }
    }
    return true;
}

void RemoveTree(const wxString& dir)
{
    wxLogNull quiet;
    if (wxDirExists(dir))
    {
        wxFileName::Rmdir(dir, wxPATH_RMDIR_RECURSIVE);
    }
}

#if RPW_HAS_UPDATER
const char* const kBrokenInstall =
    "The update failed part-way, and Windows wouldn't let all of Remote Play Whatever's old files be put back, "
    "so it may not start next time. Download it again from the releases page.";

bool MoveWithRetry(const wxString& from, const wxString& to, int attempts = 10)
{
    // A virus scanner may be looking at a file that was just written.
    for (int attempt = 0; attempt < attempts; ++attempt)
    {
        if (::MoveFileExW(from.wc_str(), to.wc_str(), MOVEFILE_REPLACE_EXISTING))
        {
            return true;
        }
        ::Sleep(200);
    }
    return false;
}

// Put target.old back as target. This one matters most, so it tries for
// longer and falls back to copying.
bool RestoreOld(const wxString& target)
{
    const wxString old = target + ".old";
    return MoveWithRetry(old, target, 50) || ::CopyFileW(old.wc_str(), target.wc_str(), FALSE);
}

bool Sha256File(const wxString& path, wxString& hex)
{
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    bool ok = false;
    if (BCRYPT_SUCCESS(::BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0)) &&
        BCRYPT_SUCCESS(::BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0)))
    {
        wxFFile file(path, "rb");
        if (file.IsOpened())
        {
            std::vector<unsigned char> buffer(64 * 1024);
            ok = true;
            for (;;)
            {
                size_t got = file.Read(buffer.data(), buffer.size());
                if (got == 0)
                {
                    break;
                }
                if (!BCRYPT_SUCCESS(::BCryptHashData(hash, buffer.data(), (ULONG)got, 0)))
                {
                    ok = false;
                    break;
                }
            }
            unsigned char digest[32];
            if (ok && !file.Error() && BCRYPT_SUCCESS(::BCryptFinishHash(hash, digest, sizeof(digest), 0)))
            {
                hex.clear();
                for (size_t i = 0; i < sizeof(digest); ++i)
                {
                    hex += wxString::Format("%02x", digest[i]);
                }
            }
            else
            {
                ok = false;
            }
        }
    }
    if (hash)
    {
        ::BCryptDestroyHash(hash);
    }
    if (alg)
    {
        ::BCryptCloseAlgorithmProvider(alg, 0);
    }
    return ok;
}

bool Unpack(const wxString& package, const wxString& dir, std::vector<wxString>& names, wxString& error)
{
    wxFFileInputStream in(package);
    if (!in.IsOk())
    {
        error = "Couldn't read the downloaded file.";
        return false;
    }
    wxZipInputStream zip(in);
    std::unique_ptr<wxZipEntry> entry;
    for (entry.reset(zip.GetNextEntry()); entry; entry.reset(zip.GetNextEntry()))
    {
        if (entry->IsDir())
        {
            continue;
        }
        const wxString name = entry->GetInternalName();
        if (!IsSafeName(name))
        {
            error = "The download contains an unexpected file (" + name + "), so nothing was changed.";
            return false;
        }
        wxFFileOutputStream out(Join(dir, name));
        if (!out.IsOk())
        {
            error = "Couldn't unpack the download into " + dir + ".";
            return false;
        }
        // Reads the whole entry; at its end the zip stream checks the CRC.
        zip.Read(out);
        if (zip.GetLastError() != wxSTREAM_EOF || !out.Close())
        {
            error = "The download couldn't be unpacked (it may be damaged), so nothing was changed.";
            return false;
        }
        names.push_back(name);
    }
    if (zip.GetLastError() == wxSTREAM_READ_ERROR || names.empty())
    {
        error = "The download couldn't be unpacked (it may be damaged), so nothing was changed.";
        return false;
    }
    return true;
}

struct SwappedFile
{
    wxString target;
    bool hadOld;
};

// Put the old files back after a failed swap. False if some couldn't be.
bool Undo(const std::vector<SwappedFile>& done)
{
    bool restored = true;
    for (std::vector<SwappedFile>::const_reverse_iterator it = done.rbegin(); it != done.rend(); ++it)
    {
        if (it->hadOld)
        {
            // Replaces the new file.
            restored = RestoreOld(it->target) && restored;
        }
        else
        {
            // A file the old version didn't have; it's in the way at worst.
            ::DeleteFileW(it->target.wc_str());
        }
    }
    return restored;
}

// Start the new version. It waits for this process to exit before it opens
// (see AppUpdater::WaitForOldCopy), because only one copy may run at a time.
bool StartNewCopy(const wxString& exe, const wxString& dir, const wxString& arguments)
{
    wxString command = wxString::Format("\"%s\" --updated-from %lu", exe, (unsigned long)::GetCurrentProcessId()) + arguments;
    std::wstring line = command.ToStdWstring();
    std::vector<wchar_t> buffer(line.begin(), line.end());
    buffer.push_back(L'\0');

    STARTUPINFOW startup;
    ZeroMemory(&startup, sizeof(startup));
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process;
    ZeroMemory(&process, sizeof(process));
    if (!::CreateProcessW(exe.wc_str(), buffer.data(), nullptr, nullptr, FALSE, 0, nullptr, dir.wc_str(),
                          &startup, &process))
    {
        return false;
    }
    ::AllowSetForegroundWindow(process.dwProcessId);
    ::CloseHandle(process.hThread);
    ::CloseHandle(process.hProcess);
    return true;
}

bool InstallPackage(const wxString& package, const wxString& sha256, const wxString& arguments, wxString& error)
{
    wxLogNull quiet;

    wxString actual;
    if (!Sha256File(package, actual))
    {
        error = "Couldn't read the downloaded file.";
        return false;
    }
    if (actual.CmpNoCase(sha256) != 0)
    {
        error = "The download was damaged (its checksum didn't match), so nothing was changed.";
        return false;
    }

    const wxString dir = AppDir();
    const wxString staging = StagingDir();
    RemoveTree(staging);
    if (!::CreateDirectoryW(staging.wc_str(), nullptr))
    {
        error = "Couldn't write to the folder Remote Play Whatever is in (" + dir + "). Move that folder somewhere "
                "you can change, like Documents, or download the new version by hand.";
        return false;
    }

    std::vector<wxString> names;
    if (!Unpack(package, staging, names, error))
    {
        RemoveTree(staging);
        return false;
    }
    bool hasProgram = std::find_if(names.begin(), names.end(), [](const wxString& n)
    {
        return n.CmpNoCase(kProgramName) == 0;
    }) != names.end();
    if (!hasProgram)
    {
        RemoveTree(staging);
        error = "The download doesn't contain Remote Play Whatever, so nothing was changed.";
        return false;
    }

    // Windows won't overwrite a running program or a DLL it has loaded, but it
    // lets them be renamed: the current files become *.old (removed when the
    // new version starts) and the new ones take their places.
    const wxString exeName = wxFileName(ExePath()).GetFullName();
    std::vector<SwappedFile> done;
    for (size_t i = 0; i < names.size(); ++i)
    {
        SwappedFile file;
        file.target = Join(dir, names[i].CmpNoCase(kProgramName) == 0 ? exeName : names[i]);
        file.hadOld = wxFileExists(file.target);
        if (file.hadOld && !MoveWithRetry(file.target, file.target + ".old"))
        {
            // This file wasn't touched; put back the ones before it.
            const bool undone = Undo(done);
            RemoveTree(staging);
            error = undone ? "Couldn't replace " + wxFileName(file.target).GetFullName() +
                             " (is something else using it?), so nothing was changed."
                           : wxString(kBrokenInstall);
            return false;
        }
        if (!MoveWithRetry(Join(staging, names[i]), file.target))
        {
            const bool restored = !file.hadOld || RestoreOld(file.target);
            const bool undone = Undo(done);
            RemoveTree(staging);
            error = restored && undone ? "Couldn't put the new " + wxFileName(file.target).GetFullName() +
                                         " in place, so nothing was changed."
                                       : wxString(kBrokenInstall);
            return false;
        }
        done.push_back(file);
    }
    RemoveTree(staging);

    if (!StartNewCopy(Join(dir, exeName), dir, arguments))
    {
        error = Undo(done) ? wxString("Couldn't start the new version, so the old one was put back.")
                           : wxString(kBrokenInstall);
        return false;
    }
    return true;
}
#endif // RPW_HAS_UPDATER
} // namespace

AppUpdater::AppUpdater(std::function<void()> onChange, std::function<bool()> canRestart) :
    m_onChange(onChange),
    m_canRestart(canRestart),
    m_state(Idle),
    m_manual(false),
    m_atLaunch(false),
    m_checkedOnce(false),
    m_shutdown(false),
    m_availableBuild(0),
    m_timer(this)
#if RPW_HAS_UPDATER
    , m_stage(StageInfo),
    m_requestId(0)
#endif
{
    Bind(wxEVT_TIMER, &AppUpdater::OnTimer, this);
#if RPW_HAS_UPDATER
    Bind(wxEVT_WEBREQUEST_STATE, &AppUpdater::OnRequestState, this);
#endif
}

bool AppUpdater::Supported()
{
    return RPW_HAS_UPDATER && CurrentBuild() > 0 && !Repo().empty();
}

long AppUpdater::CurrentBuild()
{
    return RPW_BUILD_NUMBER;
}

wxString AppUpdater::ReleasesPage()
{
    return ProjectPage() + "/releases/latest";
}

wxString AppUpdater::ProjectPage()
{
    return "https://github.com/" + (Repo().empty() ? wxString("m4dEngi/RemotePlayWhatever") : Repo());
}

bool AppUpdater::AutoChecksEnabled()
{
    wxConfigBase* config = wxConfigBase::Get();
    return !config || config->ReadBool("/Options/CheckForUpdates", true);
}

void AppUpdater::SetAutoChecksEnabled(bool enabled)
{
    wxConfigBase* config = wxConfigBase::Get();
    if (config)
    {
        config->Write("/Options/CheckForUpdates", enabled);
        config->Flush();
    }
}

bool AppUpdater::InstallsOnLaunch()
{
    wxConfigBase* config = wxConfigBase::Get();
    return !config || config->ReadBool("/Options/InstallUpdatesOnLaunch", true);
}

void AppUpdater::SetInstallsOnLaunch(bool enabled)
{
    wxConfigBase* config = wxConfigBase::Get();
    if (config)
    {
        config->Write("/Options/InstallUpdatesOnLaunch", enabled);
        config->Flush();
    }
}

bool AppUpdater::ShouldInstallNow() const
{
    if (m_state != Available || !m_atLaunch || m_manual || !InstallsOnLaunch() || m_availableBuild <= CurrentBuild())
    {
        return false;
    }
    // Installed already, yet this isn't that build: its release must be wrong.
    wxConfigBase* config = wxConfigBase::Get();
    return !config || config->ReadLong("/Options/InstalledBuild", 0) != m_availableBuild;
}

void AppUpdater::StartAutomaticChecks()
{
    if (Supported() && !m_shutdown)
    {
        m_timer.StartOnce(kFirstCheckMs);
    }
}

void AppUpdater::OnTimer(wxTimerEvent&)
{
    if (m_shutdown)
    {
        return;
    }
    if (AutoChecksEnabled() && !Busy())
    {
        StartCheck(false, !m_checkedOnce);
    }
    m_checkedOnce = true;
    m_timer.StartOnce(kCheckEveryMs);
}

void AppUpdater::SetState(State state)
{
    m_state = state;
    if (!m_shutdown && m_onChange)
    {
        m_onChange();
    }
}

void AppUpdater::Fail(const wxString& error)
{
    m_error = error;
    SetState(Failed);
}

void AppUpdater::Shutdown()
{
    m_shutdown = true;
    m_onChange = nullptr;
    m_canRestart = nullptr;
    m_restartArguments = nullptr;
    m_timer.Stop();
#if RPW_HAS_UPDATER
    if (m_request.IsOk() && m_request.GetState() == wxWebRequest::State_Active)
    {
        m_request.Cancel();
    }
#endif
}

void AppUpdater::Check(bool manual)
{
    StartCheck(manual, false);
}

void AppUpdater::StartCheck(bool manual, bool atLaunch)
{
#if RPW_HAS_UPDATER
    if (!Supported() || m_shutdown)
    {
        return;
    }
    // Asked for while an automatic check (the one right after startup, say)
    // is still running: report that check's answer instead.
    if (manual && m_state == Checking)
    {
        m_manual = true;
        return;
    }
    if (Busy())
    {
        return;
    }
    m_manual = manual;
    m_atLaunch = atLaunch;
    m_error.clear();
    if (StartRequest(InfoUrl(), StageInfo))
    {
        SetState(Checking);
    }
#else
    wxUnusedVar(manual);
    wxUnusedVar(atLaunch);
#endif
}

void AppUpdater::Install(bool automatic)
{
#if RPW_HAS_UPDATER
    if (!Supported() || m_shutdown || Busy() || m_availableBuild <= CurrentBuild())
    {
        return;
    }
    // Clicked: whatever happens next, the user is waiting to hear about it.
    m_manual = !automatic;
    m_error.clear();
    {
        wxLogNull quiet;
        if (wxFileExists(PackagePath()))
        {
            wxRemoveFile(PackagePath());
        }
    }
    const wxString url = "https://github.com/" + Repo() + "/releases/download/" + m_tag + "/" + m_file;
    if (StartRequest(url, StagePackage))
    {
        SetState(Downloading);
    }
#endif
}

void AppUpdater::ReadInfo(const wxString& text)
{
    std::map<wxString, wxString> info;
    wxArrayString notes;
    wxStringTokenizer lines(text, "\r\n", wxTOKEN_STRTOK);
    while (lines.HasMoreTokens())
    {
        wxString line = lines.GetNextToken();
        if (line.Contains("="))
        {
            const wxString key = line.BeforeFirst('=').Trim().Trim(false);
            const wxString value = line.AfterFirst('=').Trim().Trim(false);
            // Any number of "note=" lines: what's new, oldest change first.
            if (key == "note")
            {
                if (!value.empty() && notes.size() < 10)
                {
                    notes.Add(value.Left(120));
                }
            }
            else
            {
                info[key] = value;
            }
        }
    }

    long build = 0;
    if (!info["build"].ToLong(&build) || build <= 0 || !IsSafeName(info["tag"]) ||
        !IsSafeName(info["file"]) || !info["file"].EndsWith(".zip") || !IsSha256(info["sha256"]))
    {
        Fail("GitHub's update information didn't make sense.");
        return;
    }
    if (build <= CurrentBuild())
    {
        SetState(UpToDate);
        return;
    }
    m_availableBuild = build;
    m_notes = notes;
    m_tag = info["tag"];
    m_file = info["file"];
    m_sha256 = info["sha256"].Lower();
    SetState(Available);
}

wxString AppUpdater::NotesSummary(size_t maxChars) const
{
    wxString summary;
    for (size_t i = 0; i < m_notes.size(); ++i)
    {
        summary += (i ? "; " : "") + m_notes[i];
    }
    if (summary.length() > maxChars && maxChars > 3)
    {
        summary = summary.Left(maxChars - 3).Trim() + "...";
    }
    return summary;
}

wxString AppUpdater::NotesList() const
{
    wxString list;
    for (size_t i = 0; i < m_notes.size(); ++i)
    {
        list += (i ? "\n- " : "- ") + m_notes[i];
    }
    return list;
}

void AppUpdater::FinishInstall()
{
#if RPW_HAS_UPDATER
    if (m_shutdown)
    {
        return;
    }
    // Asked again now, right before the restart: a Remote Play session may
    // have started while the update was downloading.
    if (m_canRestart && !m_canRestart())
    {
        wxLogNull quiet;
        wxRemoveFile(PackagePath());
        SetState(Postponed);
        return;
    }
    SetState(Installing);
    wxString error;
    bool ok = InstallPackage(PackagePath(), m_sha256, m_restartArguments ? m_restartArguments() : wxString(), error);
    {
        wxLogNull quiet;
        wxRemoveFile(PackagePath());
    }
    if (!ok)
    {
        Fail(error);
        return;
    }
    // The new version is in place and starting (see ShouldInstallNow).
    wxConfigBase* config = wxConfigBase::Get();
    if (config)
    {
        config->Write("/Options/InstalledBuild", m_availableBuild);
        config->Flush();
    }
    SetState(Restarting);
#endif
}

#if RPW_HAS_UPDATER
bool AppUpdater::StartRequest(const wxString& url, Stage stage)
{
    // Each request gets its own id, so events from an earlier one are ignored.
    m_requestId = m_requestId >= 30000 ? 1 : m_requestId + 1;
    wxWebRequest request = wxWebSession::GetDefault().CreateRequest(this, url, m_requestId);
    if (!request.IsOk())
    {
        Fail("Windows couldn't start a download.");
        return false;
    }
    request.SetHeader("User-Agent", wxString::Format("RemotePlayWhatever/%ld", CurrentBuild()));
    request.SetStorage(stage == StageInfo ? wxWebRequest::Storage_Memory : wxWebRequest::Storage_File);
    m_stage = stage;
    m_request = request;
    request.Start();
    return true;
}

void AppUpdater::OnRequestState(wxWebRequestEvent& event)
{
    if (event.GetId() != m_requestId || m_shutdown)
    {
        return;
    }

    switch (event.GetState())
    {
    case wxWebRequest::State_Completed:
    {
        m_request = wxWebRequest();
        const int status = event.GetResponse().GetStatus();
        if (status != 200)
        {
            Fail(wxString::Format("GitHub answered with an unexpected status (%d).", status));
        }
        else if (m_stage == StageInfo)
        {
            ReadInfo(event.GetResponse().AsString());
        }
        else
        {
            // wx deletes this temporary file as soon as the handler returns.
            wxLogNull quiet;
            const wxString& file = event.GetDataFile();
            if (file.empty() || !(wxRenameFile(file, PackagePath(), true) || wxCopyFile(file, PackagePath(), true)))
            {
                Fail("Couldn't save the download.");
            }
            else
            {
                CallAfter(&AppUpdater::FinishInstall);
            }
        }
        break;
    }
    case wxWebRequest::State_Failed:
    {
        m_request = wxWebRequest();
        const wxWebResponse& response = event.GetResponse();
        if (m_stage == StageInfo && response.IsOk() && response.GetStatus() == 404)
        {
            // The newest release doesn't say which build it is (it's from
            // before the updater existed), so there's nothing newer to offer.
            SetState(UpToDate);
        }
        else
        {
            const wxString why = event.GetErrorDescription();
            Fail(why.empty() ? wxString("Couldn't reach GitHub.") : "Couldn't reach GitHub (" + why + ").");
        }
        break;
    }
    case wxWebRequest::State_Unauthorized:
        Fail("The connection to GitHub asked for a sign-in (a proxy?), so the update couldn't be downloaded.");
        if (m_request.IsOk())
        {
            m_request.Cancel();
        }
        break;
    case wxWebRequest::State_Cancelled:
        m_request = wxWebRequest();
        if (m_state == Checking || m_state == Downloading)
        {
            Fail("The download was cancelled.");
        }
        break;
    default:
        break;
    }
}
#endif // RPW_HAS_UPDATER

void AppUpdater::WaitForOldCopy(unsigned long oldProcessId)
{
#ifdef __WXMSW__
    if (!oldProcessId)
    {
        return;
    }
    HANDLE process = ::OpenProcess(SYNCHRONIZE, FALSE, (DWORD)oldProcessId);
    if (process)
    {
        ::WaitForSingleObject(process, 30000);
        ::CloseHandle(process);
    }
#else
    wxUnusedVar(oldProcessId);
#endif
}

void AppUpdater::CleanUp()
{
#if RPW_HAS_UPDATER
    wxLogNull quiet;
    const wxString dir = AppDir();
    wxArrayString olds;
    if (wxDirExists(dir))
    {
        wxDir::GetAllFiles(dir, &olds, "*.old", wxDIR_FILES);
    }
    for (size_t i = 0; i < olds.size(); ++i)
    {
        // Only what an update renamed: X.old next to a new X.
        if (wxFileExists(olds[i].Left(olds[i].length() - 4)))
        {
            ::DeleteFileW(olds[i].wc_str());
        }
    }
    RemoveTree(StagingDir());
    if (wxFileExists(PackagePath()))
    {
        wxRemoveFile(PackagePath());
    }
#endif
}
