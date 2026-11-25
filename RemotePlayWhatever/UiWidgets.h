#ifndef UIWIDGETS_H
#define UIWIDGETS_H
#include <vector>
#include <wx/wx.h>

// The app's look: flat, with rounded corners, light or dark (following
// Windows unless the user picks one). These controls are drawn by the app
// itself, so they look the same on every Windows version.
namespace Ui
{
    // The palette. The values change with the theme (ApplyTheme), so read
    // them when painting; windows that keep a colour (SetBackgroundColour
    // and the like) should get it through SetColours below instead.
    extern wxColour kWindow;            // window background
    extern wxColour kCard;              // cards on it
    extern wxColour kText;
    extern wxColour kTextSecondary;
    extern wxColour kTextMuted;
    extern wxColour kBorder;            // buttons, keycaps, the search box
    extern wxColour kBorderHover;
    extern wxColour kCardBorder;
    extern wxColour kControl;           // button face
    extern wxColour kControlHover;
    extern wxColour kControlPressed;
    extern wxColour kSubtle;            // rows inside a card
    extern wxColour kHover;             // a friend row under the mouse
    extern wxColour kAccent;            // primary buttons, links
    extern wxColour kAccentHover;
    extern wxColour kAccentPressed;
    extern wxColour kOnAccent;          // text on the accent colour
    extern wxColour kLinkPressed;
    extern wxColour kDisabledFill;
    extern wxColour kDisabledBorder;
    extern wxColour kDisabledText;
    extern wxColour kCheckedDisabled;   // a toggle that's on but can't be clicked
    extern wxColour kGood;              // "ready" and "sharing" dot
    extern wxColour kGoodText;
    extern wxColour kGoodBg;
    extern wxColour kWarnText;
    extern wxColour kWarnBg;
    extern wxColour kInfoText;
    extern wxColour kInfoBg;
    extern wxColour kInfoBorder;
    extern wxColour kDanger;
    extern wxColour kDangerBg;
    extern wxColour kDangerPressed;
    extern wxColour kPlaying;           // "Playing <game>"
    extern wxColour kDotOnline;
    extern wxColour kDotInGame;
    extern wxColour kDotAway;
    extern wxColour kDotBusy;
    extern wxColour kFooter;
    extern wxColour kDivider;
    extern wxColour kKey;               // keycap face

    // Switch the palette to light or dark: windows given colours through
    // SetColours get the new ones, and every window is repainted. On Windows
    // the title bars, scroll bars, tooltips and menus follow too (pop-up
    // questions can't: Windows draws those itself, always light).
    void ApplyTheme(bool dark);
    bool IsDark();
    // Whether Windows is set to dark mode for apps (Settings > Personalization
    // > Colors > "Choose your mode"; the "app mode" part of a custom mix).
    bool SystemPrefersDark();
    // Give a window colours from the palette (&Ui::kWindow and so on), now and
    // again whenever the theme changes. Either may be null (left alone).
    void SetColours(wxWindow* window, const wxColour* background, const wxColour* foreground = nullptr);
    // Title bar, scroll bars and tooltips of one top-level window in the
    // current theme (ApplyTheme does this for every window that exists).
    void ThemeNativeParts(wxWindow* topLevel);
#ifdef __WXMSW__
    // A frame's menu bar in dark mode (Windows only draws menu bars light):
    // the frame's window procedure passes its messages here first (true =
    // handled, *result set), and calls MenuBarLine after WM_NCPAINT and
    // WM_NCACTIVATE, for the line Windows draws under the bar. In light mode
    // both leave the menu bar to Windows.
    bool MenuBarMessage(WXHWND hwnd, WXUINT msg, WXWPARAM wParam, WXLPARAM lParam, WXLRESULT* result);
    void MenuBarLine(WXHWND hwnd);
#endif

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
    const wchar_t kGlyphSearch = 0xE721;
    const wchar_t kGlyphUp = 0xE74A;
    const wchar_t kGlyphDown = 0xE74B;

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
    // A dot in the bottom right corner of an icon, with a transparent ring cut
    // out around it, so it stands out on any background (like the badges
    // Windows puts on taskbar icons).
    void AddCornerDot(wxImage& icon, const wxColour& colour);

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
    // Colours are palette entries (&Ui::kCard...), so they follow the theme;
    // a null border draws none.
    Card(wxWindow* parent, const wxColour* fill = &Ui::kCard, const wxColour* border = &Ui::kCardBorder,
         int radius = 8);

    wxBoxSizer* Content() const { return m_content; }
    void SetColours(const wxColour* fill, const wxColour* border);
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
    const wxColour* m_fill;
    const wxColour* m_border;
    int m_radius;
    bool m_banner;
    wxImage m_picture;
    wxString m_title;
    unsigned long long m_seed;
    wxBitmap m_bannerCache;
    wxSize m_bannerCacheSize;
};

// A rounded search field: a magnifier, the text, and a clear button while
// there's text (Escape clears too). The text control sends wxEVT_TEXT, which
// reaches the search box's parents like any command event.
class SearchBox : public wxPanel
{
public:
    SearchBox(wxWindow* parent, const wxString& hint);

    wxString GetValue() const;
    void Clear();
    wxTextCtrl* Text() const { return m_text; }

private:
    void OnPaint(wxPaintEvent& event);
    void ShowClearButton();

    wxTextCtrl* m_text;
    FlatButton* m_clear;
    bool m_focused;
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
