#include <wx/wx.h>
#include <wx/artprov.h>
// Windows headers come first: Steamworks.h (via UiHelpers.h) includes
// windows.h itself, and wx's copy must come before it.
#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>
#endif
#include "Updater.h"
#include "UiHelpers.h"
#include "UpdateNeededDialog.h"

static const int kTextWidth = 360;

UpdateNeededDialog::UpdateNeededDialog(const wxString& why) :
    wxDialog(nullptr, wxID_ANY, "Remote Play Whatever"),
    m_updater(nullptr),
    m_status(nullptr),
    m_actionButton(nullptr)
{
    SetIcons(AppIcons());

    wxBoxSizer* texts = new wxBoxSizer(wxVERTICAL);
    wxStaticText* text = new wxStaticText(this, wxID_ANY, why);
    text->Wrap(FromDIP(kTextWidth));
    texts->Add(text, 0, wxBOTTOM, FromDIP(10));
    m_status = new wxStaticText(this, wxID_ANY, wxEmptyString);
    m_status->SetFont(m_status->GetFont().Bold());
    texts->Add(m_status, 0);

    wxBoxSizer* body = new wxBoxSizer(wxHORIZONTAL);
    body->Add(new wxStaticBitmap(this, wxID_ANY, wxArtProvider::GetBitmap(wxART_WARNING, wxART_MESSAGE_BOX)),
              0, wxRIGHT, FromDIP(12));
    body->Add(texts, 1);

    m_actionButton = new wxButton(this, wxID_ANY, "Check again");
    wxButton* pageButton = new wxButton(this, wxID_ANY, "Open download page");
    wxButton* closeButton = new wxButton(this, wxID_CANCEL, "Close");
    wxBoxSizer* buttons = new wxBoxSizer(wxHORIZONTAL);
    buttons->Add(m_actionButton, 0, wxRIGHT, FromDIP(6));
    buttons->Add(pageButton, 0, wxRIGHT, FromDIP(6));
    buttons->Add(closeButton, 0);

    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(body, 1, wxALL | wxEXPAND, FromDIP(14));
    sizer->Add(buttons, 0, wxALIGN_RIGHT | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(14));
    SetSizer(sizer);

    m_actionButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OnAction(); });
    pageButton->Bind(wxEVT_BUTTON, [](wxCommandEvent&) { wxLaunchDefaultBrowser(AppUpdater::ReleasesPage()); });
    closeButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Finish(wxID_CANCEL); });
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) { Finish(wxID_CANCEL); });

    // Never deleted, like the main window's: a download Windows is still
    // finishing may report back after this dialog is gone (see Finish).
    // Nothing is being shared yet, so restarting is always fine.
    m_updater = new AppUpdater([this]() { OnUpdaterChange(); }, []() { return true; });
    m_updater->Check(true);
    OnUpdaterChange();
    CentreOnScreen();
}

UpdateNeededDialog::~UpdateNeededDialog()
{
    if (m_updater)
    {
        m_updater->Shutdown();
    }
}

void UpdateNeededDialog::OnUpdaterChange()
{
    const long build = m_updater->AvailableBuild();
    wxString status;
    bool busy = false;
    switch (m_updater->GetState())
    {
    case AppUpdater::Checking:
        status = "Checking for a newer version...";
        busy = true;
        break;
    case AppUpdater::Available:
        status = wxString::Format("A newer version is available: build %ld (you have build %ld).",
                                  build, AppUpdater::CurrentBuild());
        if (!m_updater->Notes().empty())
        {
            status += "\nNew: " + m_updater->NotesSummary(160);
        }
        break;
    case AppUpdater::UpToDate:
        status = wxString::Format("You have the newest version (build %ld). A fix for this Steam version "
                                  "hasn't been published yet, so check again later.", AppUpdater::CurrentBuild());
        break;
    case AppUpdater::Downloading:
        status = wxString::Format("Downloading build %ld...", build);
        busy = true;
        break;
    case AppUpdater::Installing:
        status = wxString::Format("Installing build %ld...", build);
        busy = true;
        break;
    case AppUpdater::Restarting:
        // The new version has started and waits for this copy to exit.
        CallAfter([this]() { Finish(wxID_OK); });
        return;
    case AppUpdater::Failed:
        status = m_updater->Error();
        break;
    default:
        break;
    }

    m_status->SetLabel(status);
    m_status->Wrap(FromDIP(kTextWidth));
    m_actionButton->SetLabel(build > 0 ? wxString::Format("Update to build %ld", build) : wxString("Check again"));
    m_actionButton->Show(!busy);
    if (!busy && build > 0)
    {
        m_actionButton->SetDefault();
        m_actionButton->SetFocus();
    }
    GetSizer()->Layout();
    Fit();
}

void UpdateNeededDialog::OnAction()
{
    if (m_updater->AvailableBuild() > 0)
    {
        m_updater->Install();
    }
    else
    {
        m_updater->Check(true);
    }
}

void UpdateNeededDialog::Finish(int result)
{
    // Cancels a check or download that's still running and stops the updater
    // from calling back into this dialog.
    m_updater->Shutdown();
    if (IsModal())
    {
        EndModal(result);
    }
}
