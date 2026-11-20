#ifndef UPDATENEEDEDDIALOG_H
#define UPDATENEEDEDDIALOG_H
#include <wx/dialog.h>

class AppUpdater;
class wxButton;
class wxStaticText;

// Shown instead of the main window when this version can't work with the
// installed Steam (a Steam update changed the functions it uses). The fix is a
// newer version, and the main window's Update button is out of reach, so this
// looks for one straight away and offers to install it.
class UpdateNeededDialog : public wxDialog
{
public:
    explicit UpdateNeededDialog(const wxString& why);
    ~UpdateNeededDialog();

private:
    void OnUpdaterChange();
    void OnAction();
    void Finish(int result);

    AppUpdater* m_updater;
    wxStaticText* m_status;
    wxButton* m_actionButton;
};

#endif // UPDATENEEDEDDIALOG_H
