#ifndef UIWIDGETS_H
#define UIWIDGETS_H
#include <vector>
#include <wx/wx.h>

// The app's look: light and flat, with rounded corners. These controls are
// drawn by the app itself, so they look the same on every Windows version.
namespace Ui
{
    const wxColour kWindow(255, 255, 255);
    const wxColour kText(27, 27, 27);
    const wxColour kTextSecondary(102, 108, 115);
    const wxColour kTextMuted(138, 143, 150);
    const wxColour kBorder(201, 205, 211);          // buttons, keycaps
    const wxColour kCardBorder(227, 230, 234);
    const wxColour kSubtle(244, 246, 248);          // rows inside a card
    const wxColour kHover(238, 244, 251);           // a friend row under the mouse
    const wxColour kAccent(15, 108, 189);
    const wxColour kAccentHover(17, 94, 163);
    const wxColour kAccentPressed(15, 84, 140);
    const wxColour kGood(31, 157, 74);              // "ready" and "sharing" dot
    const wxColour kGoodText(30, 107, 52);
    const wxColour kGoodBg(233, 246, 236);
    const wxColour kWarnText(138, 75, 0);
    const wxColour kWarnBg(255, 244, 229);
    const wxColour kInfoBg(238, 244, 251);
    const wxColour kInfoBorder(206, 224, 244);
    const wxColour kDanger(179, 38, 30);
    const wxColour kDangerBg(253, 231, 233);
    const wxColour kPlaying(63, 143, 42);           // "Playing <game>"
    const wxColour kDotOnline(59, 155, 224);
    const wxColour kDotInGame(87, 176, 58);
    const wxColour kDotAway(227, 162, 26);
    const wxColour kDotBusy(209, 52, 56);
    const wxColour kFooter(250, 251, 252);
    const wxColour kDivider(236, 238, 241);

    // Characters of the Windows icon font (Segoe Fluent Icons / Segoe MDL2 Assets).
    const wchar_t kGlyphKeyboard = 0xE765;
    const wchar_t kGlyphMouse = 0xE962;
    const wchar_t kGlyphController = 0xE7FC;
    const wchar_t kGlyphCopy = 0xE8C8;
    const wchar_t kGlyphLink = 0xE71B;
    const wchar_t kGlyphClose = 0xE711;
    const wchar_t kGlyphClock = 0xE823;
    const wchar_t kGlyphDownload = 0xE896;
    const wchar_t kGlyphCheck = 0xE73E;
    const wchar_t kGlyphWarning = 0xE7BA;
    const wchar_t kGlyphInfo = 0xE946;

    // " · ", between parts of a line such as "Sharing · 1 guest connected".
    wxString Separator();

    // The same font in semibold (Segoe UI Semibold on Windows).
    wxFont SemiBold(const wxFont& font);
    // The icon font at the size of the given font; an invalid font where the
    // system has none (Linux, or Windows before 10), so icons are left out.
    wxFont IconFont(const wxFont& like);
    bool HasIconFont();

    // A round, anti-aliased copy of a picture, size x size pixels. Greyed:
    // faded grey, for friends who are offline.
    wxBitmap RoundPicture(const wxImage& picture, int size, bool greyed);
    // First letters of a name ("Alex Smith" -> "AS"), for people without a picture.
    wxString Initials(const wxString& name);
    // A colour for someone's initials circle, always the same for the same seed.
    wxColour PlaceholderColour(unsigned long long seed);
    // An avatar in rect: the round picture, or the initials on a coloured
    // circle, with an optional status dot at the bottom right, ringed in
    // surface (the colour behind the avatar).
    void DrawAvatar(wxDC& dc, const wxRect& rect, const wxBitmap& roundPicture, const wxString& initials,
                    const wxColour& placeholder, const wxColour& dot, const wxColour& surface);

    // Lay out a window whose content changed, and every window it's in (from
    // the top-level window down), so a change in its height moves everything
    // below it. Laying out only the top-level window doesn't reach panels
    // whose own size stays the same.
    void Relayout(wxWindow* window);
}

// A flat button with rounded corners. Sends wxEVT_BUTTON like wxButton, but
// queued, after the button's own mouse and key handling is over. (A handler
// that destroys the button still has to do it with CallAfter.) On Windows it
// also answers BM_CLICK, so automation can press it.
class FlatButton : public wxControl
{
public:
    enum Kind
    {
        Secondary,  // white with a grey outline
        Primary,    // filled with the accent colour
        Link,       // just text, in the accent colour
        Danger,     // just text or an icon, in red (e.g. a remove "x")
        Toggle,     // checked: like Primary; unchecked: like Secondary, in grey
    };

    FlatButton(wxWindow* parent, const wxString& label, Kind kind = Secondary, wchar_t glyph = 0, bool compact = false);
    // Lets go of the mouse if it's held down on the button as it goes (the
    // windows rebuild their rows on timers, so that can happen).
    virtual ~FlatButton();

    void SetKind(Kind kind);
    Kind GetKind() const { return m_kind; }
    void SetChecked(bool checked);
    bool IsChecked() const { return m_checked; }
    // Drawn like a disabled button, but it can still be clicked and still
    // shows its tooltip (so a click can explain why it isn't available).
    void SetMuted(bool muted);
    bool IsMuted() const { return m_muted; }
    // An icon before the label (if the system has the icon font).
    void SetGlyph(wchar_t glyph);
    // Show only the icon; the label is then the tooltip-like name that
    // automation finds (and what's shown where there's no icon font).
    void SetIconOnly(bool iconOnly);

    virtual void SetLabel(const wxString& label) override;
    virtual bool Enable(bool enable = true) override;
    virtual bool SetFont(const wxFont& font) override;
    virtual bool AcceptsFocusFromKeyboard() const override { return IsEnabled(); }
    // Do what a click does.
    void Click();

protected:
    virtual wxSize DoGetBestClientSize() const override;
    virtual wxBorder GetDefaultBorder() const override { return wxBORDER_NONE; }
#ifdef __WXMSW__
    virtual WXLRESULT MSWWindowProc(WXUINT msg, WXWPARAM wParam, WXLPARAM lParam) override;
#endif

private:
    struct Look
    {
        wxColour fill;      // invalid: none
        wxColour border;    // invalid: none
        wxColour text;
    };
    Look CurrentLook() const;
    wxFont LabelFont() const;
    bool DrawsGlyph() const;
    bool DrawsLabel() const;
    void OnPaint(wxPaintEvent& event);
    void OnLeftDown(wxMouseEvent& event);
    void OnLeftUp(wxMouseEvent& event);
    void OnMotion(wxMouseEvent& event);
    void OnKeyDown(wxKeyEvent& event);

    Kind m_kind;
    wchar_t m_glyph;
    bool m_compact;
    bool m_iconOnly;
    bool m_checked;
    bool m_muted;
    bool m_hover;
    bool m_pressed;
    bool m_keyboardFocus;
};

// A panel with rounded corners and a thin border, optionally with a picture
// across its top (cover-scaled) with a title over it. Put its content in
// Content(); that's placed below the picture.
class Card : public wxPanel
{
public:
    // An invalid border colour draws no border.
    Card(wxWindow* parent, const wxColour& fill = Ui::kWindow, const wxColour& border = Ui::kCardBorder,
         int radius = 8);

    wxBoxSizer* Content() const { return m_content; }
    void SetColours(const wxColour& fill, const wxColour& border);
    // Without a picture (invalid image) a colour gradient is used, chosen by seed.
    void SetBanner(const wxImage& picture, const wxString& title, unsigned long long seed);
    void SetBannerTitle(const wxString& title);
    void ClearBanner();
    bool HasBanner() const { return m_banner; }

private:
    void OnPaint(wxPaintEvent& event);
    wxBitmap BannerBitmap(const wxSize& size);

    wxBoxSizer* m_content;
    wxSizerItem* m_bannerSpace;
    wxColour m_fill;
    wxColour m_border;
    int m_radius;
    bool m_banner;
    wxImage m_picture;
    wxString m_title;
    unsigned long long m_seed;
    wxBitmap m_bannerCache;
    wxSize m_bannerCacheSize;
};

// A round avatar picture (or initials) with an optional status dot.
class AvatarCtrl : public wxWindow
{
public:
    AvatarCtrl(wxWindow* parent, int size);

    // An invalid picture shows the initials of name instead.
    void SetPerson(const wxImage& picture, const wxString& name, unsigned long long seed);
    void SetDot(const wxColour& colour);
    void SetGreyed(bool greyed);

protected:
    virtual wxSize DoGetBestClientSize() const override;

private:
    void OnPaint(wxPaintEvent& event);

    int m_size;
    wxImage m_picture;
    wxBitmap m_round;
    wxString m_initials;
    wxColour m_placeholder;
    wxColour m_dot;
    bool m_greyed;
};

// A small filled circle, e.g. green for "sharing".
class StatusDot : public wxWindow
{
public:
    StatusDot(wxWindow* parent, int size = 9);
    void SetColour(const wxColour& colour);

protected:
    virtual wxSize DoGetBestClientSize() const override;

private:
    void OnPaint(wxPaintEvent& event);

    int m_size;
    wxColour m_colour;
};

// Text that wraps to the width it's given and asks for the height that
// needs. With keycaps on, parts in brackets ("[Ctrl]+[Alt]+[K]") are drawn
// as keyboard keys.
class WrapLabel : public wxControl
{
public:
    WrapLabel(wxWindow* parent, const wxString& text = wxEmptyString, bool keycaps = false);

    virtual void SetLabel(const wxString& text) override;
    virtual bool SetFont(const wxFont& font) override;
    virtual bool AcceptsFocus() const override { return false; }

protected:
    virtual wxSize DoGetBestClientSize() const override;
    virtual int DoGetBestClientHeight(int width) const override;
    virtual wxBorder GetDefaultBorder() const override { return wxBORDER_NONE; }

private:
    struct Part
    {
        wxString text;
        bool key;
        int width;
    };
    struct Word
    {
        std::vector<Part> parts;
        int width;
        bool lineBreak;     // a "\n": the next word starts a new line
    };

    void Measure();
    int HeightFor(int width) const;
    int LineHeight() const;
    wxFont KeyFont() const;
    void OnPaint(wxPaintEvent& event);
    void OnSize(wxSizeEvent& event);

    bool m_keycaps;
    std::vector<Word> m_words;
    int m_spaceWidth;
    int m_textHeight;
    int m_keyHeight;
    int m_laidOutWidth;
    bool m_relayoutQueued;
};

#endif // UIWIDGETS_H
