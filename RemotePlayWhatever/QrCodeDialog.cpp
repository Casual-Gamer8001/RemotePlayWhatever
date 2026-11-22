#include <wx/wx.h>
#include <wx/clipbrd.h>
#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include <algorithm>
#include <exception>
#include <memory>
#include <vector>
#include "qrcodegen.hpp"
#include "QrCodeDialog.h"
#include "UiWidgets.h"

namespace
{
// The code: black squares on a white tile with the margin around them that
// scanners need (4 squares wide), each square a whole number of pixels so
// the edges stay sharp. White in dark mode too: phones read dark on light.
class QrCodePanel : public wxWindow
{
public:
    QrCodePanel(wxWindow* parent, const wxString& text) :
        wxWindow(parent, wxID_ANY),
        m_count(0),
        m_square(std::max(2, FromDIP(6)))
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        try
        {
            const wxScopedCharBuffer utf8 = text.utf8_str();
            const qrcodegen::QrCode code = qrcodegen::QrCode::encodeText(utf8.data(), qrcodegen::QrCode::Ecc::MEDIUM);
            m_count = code.getSize();
            m_squares.assign((size_t)m_count * m_count, 0);
            for (int y = 0; y < m_count; ++y)
            {
                for (int x = 0; x < m_count; ++x)
                {
                    m_squares[(size_t)y * m_count + x] = code.getModule(x, y) ? 1 : 0;
                }
            }
        }
        catch (const std::exception&)
        {
            m_count = 0;    // too long for a QR code; the dialog still shows the link
        }
        const int side = (std::max(m_count, 21) + 8) * m_square;
        SetMinSize(wxSize(side, side));
        Bind(wxEVT_PAINT, &QrCodePanel::OnPaint, this);
    }

private:
    void OnPaint(wxPaintEvent&)
    {
        wxAutoBufferedPaintDC dc(this);
        dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
        dc.Clear();
        const wxSize size = GetClientSize();
        const int side = (std::max(m_count, 21) + 8) * m_square;
        const int left = (size.x - side) / 2;
        const int top = (size.y - side) / 2;
        {
            std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
            if (gc)
            {
                gc->SetPen(*wxTRANSPARENT_PEN);
                gc->SetBrush(*wxWHITE_BRUSH);
                gc->DrawRoundedRectangle(left, top, side, side, FromDIP(8));
            }
            else
            {
                dc.SetPen(*wxTRANSPARENT_PEN);
                dc.SetBrush(*wxWHITE_BRUSH);
                dc.DrawRectangle(left, top, side, side);
            }
        }
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.SetBrush(*wxBLACK_BRUSH);
        const int first = 4 * m_square;
        for (int y = 0; y < m_count; ++y)
        {
            // Runs of black squares along the row as one rectangle each.
            int x = 0;
            while (x < m_count)
            {
                if (!m_squares[(size_t)y * m_count + x])
                {
                    ++x;
                    continue;
                }
                int end = x;
                while (end < m_count && m_squares[(size_t)y * m_count + end])
                {
                    ++end;
                }
                dc.DrawRectangle(left + first + x * m_square, top + first + y * m_square, (end - x) * m_square, m_square);
                x = end;
            }
        }
    }

    int m_count;                        // squares along each side (0: no code)
    int m_square;                       // pixels per square
    std::vector<unsigned char> m_squares;
};
} // namespace

QrCodeDialog::QrCodeDialog(wxWindow* parent, const wxString& link, std::function<bool()> stillValid) :
    wxDialog(parent, wxID_ANY, "Guest link QR code"),
    m_link(link),
    m_stillValid(stillValid),
    m_copyButton(nullptr),
    m_copiedTicks(0),
    m_closing(false),
    m_timer(this)
{
    Ui::SetColours(this, &Ui::kWindow, &Ui::kText);
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    const int pad = FromDIP(20);

    QrCodePanel* code = new QrCodePanel(this, link);
    const int width = std::max(code->GetMinSize().x, FromDIP(280));

    wxStaticText* title = new wxStaticText(this, wxID_ANY, "Scan to join");
    title->SetFont(Ui::SemiBold(GetFont()).Scaled(1.2f));
    Ui::SetColours(title, nullptr, &Ui::kText);
    sizer->Add(title, 0, wxLEFT | wxRIGHT | wxTOP, pad);
    sizer->AddSpacer(FromDIP(6));

    wxStaticText* text = new wxStaticText(this, wxID_ANY,
        "Point a phone's camera at the code. Anyone can join with it, even without a Steam account, "
        "using the free Steam Link app. One person per link.");
    Ui::SetColours(text, nullptr, &Ui::kTextSecondary);
    text->Wrap(width);
    sizer->Add(text, 0, wxLEFT | wxRIGHT, pad);
    sizer->AddSpacer(FromDIP(16));

    sizer->Add(code, 0, wxALIGN_CENTER_HORIZONTAL | wxLEFT | wxRIGHT, pad);
    sizer->AddSpacer(FromDIP(10));

    wxStaticText* shown = new wxStaticText(this, wxID_ANY, link);
    Ui::SetColours(shown, nullptr, &Ui::kTextMuted);
    sizer->Add(shown, 0, wxALIGN_CENTER_HORIZONTAL | wxLEFT | wxRIGHT, pad);

    wxBoxSizer* buttons = new wxBoxSizer(wxHORIZONTAL);
    buttons->AddStretchSpacer();
    m_copyButton = new FlatButton(this, "Copy link", FlatButton::Secondary, Ui::kGlyphCopy);
    m_copyButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&)
    {
        if (wxTheClipboard->Open())
        {
            wxTheClipboard->SetData(new wxTextDataObject(m_link));
            wxTheClipboard->Flush();
            wxTheClipboard->Close();
            m_copyButton->SetLabel("Copied!");
            m_copyButton->SetGlyph(Ui::kGlyphCheck);
            m_copiedTicks = 4;
            Layout();
        }
    });
    buttons->Add(m_copyButton, 0, wxRIGHT, FromDIP(8));
    FlatButton* close = new FlatButton(this, "Close", FlatButton::Primary);
    close->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Close(); });
    buttons->Add(close, 0);
    sizer->AddSpacer(FromDIP(18));
    sizer->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, pad);

    SetSizerAndFit(sizer);
    CentreOnParent();
    close->SetFocus();
    Ui::ThemeNativeParts(this);

    Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& event)
    {
        if (event.GetKeyCode() == WXK_ESCAPE)
        {
            Close();
            return;
        }
        event.Skip();
    });
    // Closing destroys it (once its events are done; a dialog would only hide).
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&)
    {
        m_closing = true;
        m_timer.Stop();
        Destroy();
    });
    Bind(wxEVT_TIMER, &QrCodeDialog::OnTimer, this);
    m_timer.Start(500);
}

void QrCodeDialog::OnTimer(wxTimerEvent&)
{
    if (m_closing)
    {
        return;
    }
    if (m_copiedTicks > 0 && --m_copiedTicks == 0)
    {
        m_copyButton->SetLabel("Copy link");
        m_copyButton->SetGlyph(Ui::kGlyphCopy);
        Layout();
    }
    // Used, cancelled or the session's over: the code would lead nowhere.
    if (m_stillValid && !m_stillValid())
    {
        Close();
    }
}
