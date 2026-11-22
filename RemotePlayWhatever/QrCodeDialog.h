#ifndef QRCODEDIALOG_H
#define QRCODEDIALOG_H
#include <functional>
#include <wx/dialog.h>
#include <wx/timer.h>

class FlatButton;

// A guest link as a QR code, for someone in the same room to scan with their
// phone's camera (it opens the link, which leads them to Steam Link). Not
// modal, so the main window can still be used; create it with new and Show()
// it: it destroys itself when closed (Close, Esc, its window's X), and it
// closes by itself once the link stops working (someone came in with it, it
// was cancelled, or the session ended): stillValid is asked twice a second.
class QrCodeDialog : public wxDialog
{
public:
    QrCodeDialog(wxWindow* parent, const wxString& link, std::function<bool()> stillValid);

    const wxString& GetLink() const { return m_link; }
    // Closed already, only waiting to be deleted.
    bool IsClosing() const { return m_closing; }

private:
    void OnTimer(wxTimerEvent& event);

    wxString m_link;
    std::function<bool()> m_stillValid;
    FlatButton* m_copyButton;
    int m_copiedTicks;      // "Copied!" for this many more timer ticks
    bool m_closing;
    wxTimer m_timer;
};

#endif // QRCODEDIALOG_H
