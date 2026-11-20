#include <wx/defs.h>
#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>
#endif
#include <algorithm>
#include <cmath>
#include <memory>
#include <wx/dcbuffer.h>
#include <wx/fontenum.h>
#include <wx/graphics.h>
#include <wx/image.h>
#include "UiWidgets.h"

namespace
{
    // Height of the picture across the top of a card, in DIPs.
    const int kBannerHeight = 78;

    // How much of a pixel whose centre is at the given distance from a
    // circle's centre lies inside the circle (for anti-aliased edges).
    double Coverage(double distance, double radius)
    {
        return std::min(1.0, std::max(0.0, radius - distance + 0.5));
    }

    unsigned char Mix(unsigned char from, unsigned char to, double amount)
    {
        return (unsigned char)std::lround(from + (to - from) * amount);
    }

    // Spreads the bits of an id, so neighbouring SteamIDs get different colours.
    unsigned long long Scramble(unsigned long long seed)
    {
        seed ^= seed >> 33;
        seed *= 0xff51afd7ed558ccdULL;
        seed ^= seed >> 33;
        return seed;
    }

    wxString IconFace()
    {
        static bool checked = false;
        static wxString face;
        if (!checked)
        {
            checked = true;
#ifdef __WXMSW__
            if (wxFontEnumerator::IsValidFacename("Segoe Fluent Icons"))
            {
                face = "Segoe Fluent Icons";
            }
            else if (wxFontEnumerator::IsValidFacename("Segoe MDL2 Assets"))
            {
                face = "Segoe MDL2 Assets";
            }
#endif
        }
        return face;
    }

    // Lays the corners of an image's top edge transparent, rounded like a card's.
    void RoundTopCorners(wxImage& image, int radius)
    {
        if (!image.HasAlpha())
        {
            image.InitAlpha();
        }
        const int width = image.GetWidth();
        const int height = image.GetHeight();
        unsigned char* alpha = image.GetAlpha();
        for (int y = 0; y < radius && y < height; ++y)
        {
            for (int x = 0; x < radius && x < width; ++x)
            {
                const double dx = radius - (x + 0.5);
                const double dy = radius - (y + 0.5);
                const double cover = Coverage(std::sqrt(dx * dx + dy * dy), radius);
                unsigned char& left = alpha[y * width + x];
                unsigned char& right = alpha[y * width + (width - 1 - x)];
                left = (unsigned char)std::lround(left * cover);
                right = (unsigned char)std::lround(right * cover);
            }
        }
    }
}

// ---------------------------------------------------------------------------

wxString Ui::Separator()
{
    return wxString(L" \u00B7 ");
}

wxFont Ui::SemiBold(const wxFont& font)
{
    wxFont result(font.IsOk() ? font : *wxNORMAL_FONT);
#ifdef __WXMSW__
    // Asked for weight 600, Windows draws Segoe UI in bold; its semibold
    // weight is a font of its own name.
    static int haveSemibold = -1;
    if (haveSemibold < 0)
    {
        haveSemibold = wxFontEnumerator::IsValidFacename("Segoe UI Semibold") ? 1 : 0;
    }
    if (haveSemibold && result.GetFaceName().IsSameAs("Segoe UI", false))
    {
        result.SetFaceName("Segoe UI Semibold");
        result.SetWeight(wxFONTWEIGHT_NORMAL);
        return result;
    }
#endif
    result.SetWeight(wxFONTWEIGHT_SEMIBOLD);
    return result;
}

bool Ui::HasIconFont()
{
    return !IconFace().empty();
}

wxFont Ui::IconFont(const wxFont& like)
{
    if (!HasIconFont())
    {
        return wxNullFont;
    }
    wxFont font(like.IsOk() ? like : *wxNORMAL_FONT);
    font.SetFaceName(IconFace());
    font.SetWeight(wxFONTWEIGHT_NORMAL);
    font.SetStyle(wxFONTSTYLE_NORMAL);
    return font;
}

wxBitmap Ui::RoundPicture(const wxImage& picture, int size, bool greyed)
{
    if (!picture.IsOk() || size <= 0)
    {
        return wxNullBitmap;
    }
    wxImage image = picture.Scale(size, size, wxIMAGE_QUALITY_HIGH);
    if (greyed)
    {
        image = image.ConvertToGreyscale();
        unsigned char* data = image.GetData();
        for (int i = 0, n = size * size * 3; i < n; ++i)
        {
            data[i] = Mix(data[i], 255, 0.45);
        }
    }
    if (!image.HasAlpha())
    {
        image.InitAlpha();
    }
    unsigned char* alpha = image.GetAlpha();
    const double radius = size / 2.0;
    for (int y = 0; y < size; ++y)
    {
        for (int x = 0; x < size; ++x)
        {
            const double dx = x + 0.5 - radius;
            const double dy = y + 0.5 - radius;
            unsigned char& a = alpha[y * size + x];
            a = (unsigned char)std::lround(a * Coverage(std::sqrt(dx * dx + dy * dy), radius));
        }
    }
    return wxBitmap(image);
}

wxString Ui::Initials(const wxString& name)
{
    // Letters and digits only, in runs ("Alex Smith" -> "Alex", "Smith").
    std::vector<wxString> words(1);
    for (wxString::const_iterator it = name.begin(); it != name.end(); ++it)
    {
        const wxUniChar c = *it;
        if (wxIsalnum(c))
        {
            words.back() += c;
        }
        else if (!words.back().empty())
        {
            words.push_back(wxString());
        }
    }
    if (words.back().empty())
    {
        words.pop_back();
    }
    if (words.empty())
    {
        return "?";
    }

    wxString result;
    if (words.size() >= 2)
    {
        result << words[0][0] << words[1][0];
    }
    else
    {
        result = words[0].Left(2);
    }
    // Wide letters (e.g. Chinese or Japanese) only fit one at a time.
    if (result.length() > 1 && ((wxUniChar)result[0]).GetValue() >= 0x2E80)
    {
        result.Truncate(1);
    }
    return result.Upper();
}

wxColour Ui::PlaceholderColour(unsigned long long seed)
{
    static const unsigned char colours[][3] =
    {
        { 142, 91, 209 }, { 209, 98, 91 }, { 59, 138, 196 }, { 47, 158, 143 },
        { 196, 138, 47 }, { 94, 124, 226 }, { 201, 82, 145 }, { 96, 145, 72 },
    };
    const unsigned char* c = colours[Scramble(seed) % (sizeof(colours) / sizeof(colours[0]))];
    return wxColour(c[0], c[1], c[2]);
}

void Ui::DrawAvatar(wxDC& dc, const wxRect& rect, const wxBitmap& roundPicture, const wxString& initials,
                    const wxColour& placeholder, const wxColour& dot, const wxColour& surface)
{
    if (roundPicture.IsOk())
    {
        dc.DrawBitmap(roundPicture, rect.x, rect.y, true);
    }
    else
    {
        {
            std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::CreateFromUnknownDC(dc));
            if (gc)
            {
                gc->SetPen(*wxTRANSPARENT_PEN);
                gc->SetBrush(wxBrush(placeholder.IsOk() ? placeholder : kTextMuted));
                gc->DrawEllipse(rect.x, rect.y, rect.width, rect.height);
            }
        }
        if (!initials.empty())
        {
            wxFont font = SemiBold(dc.GetFont().IsOk() ? dc.GetFont() : *wxNORMAL_FONT);
            font.SetPixelSize(wxSize(0, std::max(6, rect.height * 2 / 5)));
            dc.SetFont(font);
            dc.SetTextForeground(*wxWHITE);
            dc.SetBackgroundMode(wxBRUSHSTYLE_TRANSPARENT);
            const wxSize text = dc.GetTextExtent(initials);
            dc.DrawText(initials, rect.x + (rect.width - text.x) / 2, rect.y + (rect.height - text.y) / 2);
        }
    }

    if (dot.IsOk())
    {
        std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::CreateFromUnknownDC(dc));
        if (gc)
        {
            const double size = std::max(6.0, rect.width * 0.34);
            const double ring = std::max(1.5, rect.width / 15.0);
            const double cx = rect.x + rect.width - size / 2.0 + 1.0;
            const double cy = rect.y + rect.height - size / 2.0 + 1.0;
            gc->SetPen(*wxTRANSPARENT_PEN);
            gc->SetBrush(wxBrush(surface));
            gc->DrawEllipse(cx - size / 2.0 - ring, cy - size / 2.0 - ring, size + 2.0 * ring, size + 2.0 * ring);
            gc->SetBrush(wxBrush(dot));
            gc->DrawEllipse(cx - size / 2.0, cy - size / 2.0, size, size);
        }
    }
}

void Ui::Relayout(wxWindow* window)
{
    std::vector<wxWindow*> chain;
    for (wxWindow* w = window; w; w = w->GetParent())
    {
        chain.push_back(w);
        if (w->IsTopLevel())
        {
            break;
        }
    }
    if (window)
    {
        window->InvalidateBestSize();
    }
    for (std::vector<wxWindow*>::reverse_iterator it = chain.rbegin(); it != chain.rend(); ++it)
    {
        (*it)->Layout();
        if ((*it)->HasFlag(wxVSCROLL))
        {
            (*it)->FitInside();
        }
    }
}

// ---------------------------------------------------------------------------

FlatButton::FlatButton(wxWindow* parent, const wxString& label, Kind kind, wchar_t glyph, bool compact) :
    wxControl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE),
    m_kind(kind),
    m_glyph(glyph),
    m_compact(compact),
    m_iconOnly(false),
    m_checked(false),
    m_muted(false),
    m_hover(false),
    m_pressed(false),
    m_keyboardFocus(false)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    wxControl::SetLabel(label);
    SetInitialSize();

    Bind(wxEVT_PAINT, &FlatButton::OnPaint, this);
    Bind(wxEVT_LEFT_DOWN, &FlatButton::OnLeftDown, this);
    Bind(wxEVT_LEFT_DCLICK, &FlatButton::OnLeftDown, this);
    Bind(wxEVT_LEFT_UP, &FlatButton::OnLeftUp, this);
    Bind(wxEVT_MOTION, &FlatButton::OnMotion, this);
    Bind(wxEVT_KEY_DOWN, &FlatButton::OnKeyDown, this);
    // Other handlers (a friend row's highlight) also watch the mouse coming
    // and going, so these pass the events on.
    Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent& event)
    {
        m_hover = true;
        Refresh();
        event.Skip();
    });
    Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent& event)
    {
        if (!m_pressed)
        {
            m_hover = false;
        }
        Refresh();
        event.Skip();
    });
    Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](wxMouseCaptureLostEvent&)
    {
        m_pressed = false;
        m_hover = false;
        Refresh();
    });
    // A focus outline only when focus came from the keyboard (Tab), like
    // Windows' own buttons; not for the button that gets focus at startup.
    Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& event)
    {
#ifdef __WXMSW__
        m_keyboardFocus = wxGetKeyState(WXK_TAB);
#else
        m_keyboardFocus = false;
#endif
        Refresh();
        event.Skip();
    });
    Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& event)
    {
        Refresh();
        event.Skip();
    });
    Bind(wxEVT_SIZE, [this](wxSizeEvent& event)
    {
        Refresh();
        event.Skip();
    });
}

FlatButton::~FlatButton()
{
    // While the native window still exists; wx asserts (and GTK later
    // crashes) if a window is destroyed with the mouse still captured.
    if (HasCapture())
    {
        ReleaseMouse();
    }
}

void FlatButton::SetKind(Kind kind)
{
    if (kind != m_kind)
    {
        m_kind = kind;
        InvalidateBestSize();
        Refresh();
    }
}

void FlatButton::SetChecked(bool checked)
{
    if (checked != m_checked)
    {
        m_checked = checked;
        Refresh();
    }
}

void FlatButton::SetMuted(bool muted)
{
    if (muted != m_muted)
    {
        m_muted = muted;
        Refresh();
    }
}

void FlatButton::SetGlyph(wchar_t glyph)
{
    if (glyph != m_glyph)
    {
        m_glyph = glyph;
        InvalidateBestSize();
        Refresh();
    }
}

void FlatButton::SetIconOnly(bool iconOnly)
{
    if (iconOnly != m_iconOnly)
    {
        m_iconOnly = iconOnly;
        InvalidateBestSize();
        Refresh();
    }
}

void FlatButton::SetLabel(const wxString& label)
{
    if (label != GetLabel())
    {
        wxControl::SetLabel(label);
        InvalidateBestSize();
        Refresh();
    }
}

bool FlatButton::Enable(bool enable)
{
    // GTK doesn't deliver the button-up to a disabled widget, so a capture
    // taken by a press would never be released.
    if (!enable && HasCapture())
    {
        ReleaseMouse();
    }
    if (!wxControl::Enable(enable))
    {
        return false;
    }
    if (!enable)
    {
        m_pressed = false;
        m_hover = false;
    }
    Refresh();
    return true;
}

bool FlatButton::SetFont(const wxFont& font)
{
    if (!wxControl::SetFont(font))
    {
        return false;
    }
    InvalidateBestSize();
    Refresh();
    return true;
}

void FlatButton::Click()
{
    if (!IsEnabled())
    {
        return;
    }
    // Queued, so whatever the click does happens after this button's own
    // mouse or key handling is finished.
    wxCommandEvent* event = new wxCommandEvent(wxEVT_BUTTON, GetId());
    event->SetEventObject(this);
    event->SetInt(m_checked ? 1 : 0);
    GetEventHandler()->QueueEvent(event);
}

#ifdef __WXMSW__
WXLRESULT FlatButton::MSWWindowProc(WXUINT msg, WXWPARAM wParam, WXLPARAM lParam)
{
    if (msg == BM_CLICK)
    {
        Click();
        return 0;
    }
    return wxControl::MSWWindowProc(msg, wParam, lParam);
}
#endif

bool FlatButton::DrawsGlyph() const
{
    return m_glyph != 0 && Ui::HasIconFont();
}

bool FlatButton::DrawsLabel() const
{
    return !m_iconOnly || !DrawsGlyph();
}

wxFont FlatButton::LabelFont() const
{
    const wxFont font = GetFont();
    return m_kind == Primary || m_kind == Link ? Ui::SemiBold(font) : font;
}

wxSize FlatButton::DoGetBestClientSize() const
{
    const wxFont font = LabelFont();
    int textWidth = 0, textHeight = 0, unused = 0;
    GetTextExtent("Ag", &unused, &textHeight, nullptr, nullptr, &font);
    if (DrawsLabel())
    {
        GetTextExtent(GetLabelText(), &textWidth, &unused, nullptr, nullptr, &font);
    }
    int glyphWidth = 0, glyphHeight = 0;
    if (DrawsGlyph())
    {
        const wxFont icons = Ui::IconFont(font);
        GetTextExtent(wxString(m_glyph), &glyphWidth, &glyphHeight, nullptr, nullptr, &icons);
    }
    const int gap = glyphWidth && textWidth ? FromDIP(6) : 0;
    const int padY = FromDIP(m_compact ? 4 : 7);
    int padX = FromDIP(m_compact ? 10 : 14);
    if (m_kind == Link || m_kind == Danger)
    {
        padX = FromDIP(m_compact ? 6 : 8);
    }
    if (m_iconOnly && DrawsGlyph())
    {
        padX = padY + FromDIP(2);
    }
    return wxSize(glyphWidth + gap + textWidth + 2 * padX, std::max(textHeight, glyphHeight) + 2 * padY);
}

FlatButton::Look FlatButton::CurrentLook() const
{
    Look look;
    const bool pressed = m_pressed && m_hover;
    Kind kind = m_kind;
    if (kind == Toggle)
    {
        kind = m_checked ? Primary : Secondary;
    }

    if (!IsEnabled() || m_muted)
    {
        if (kind == Link || kind == Danger)
        {
            look.text = Ui::kTextMuted;
        }
        else if (m_kind == Toggle && m_checked)
        {
            look.fill = wxColour(178, 204, 232);
            look.border = look.fill;
            look.text = *wxWHITE;
        }
        else
        {
            look.fill = Ui::kSubtle;
            look.border = wxColour(225, 228, 232);
            look.text = wxColour(160, 165, 171);
        }
        return look;
    }

    switch (kind)
    {
    case Primary:
        look.fill = pressed ? Ui::kAccentPressed : m_hover ? Ui::kAccentHover : Ui::kAccent;
        look.border = look.fill;
        look.text = *wxWHITE;
        break;
    case Link:
        if (m_hover)
        {
            look.fill = pressed ? wxColour(219, 232, 247) : Ui::kHover;
        }
        look.text = pressed ? Ui::kAccentPressed : Ui::kAccent;
        break;
    case Danger:
        if (m_hover)
        {
            look.fill = pressed ? wxColour(248, 206, 210) : Ui::kDangerBg;
        }
        look.text = Ui::kDanger;
        break;
    default:
        look.fill = pressed ? wxColour(232, 234, 237) : m_hover ? wxColour(246, 247, 249) : Ui::kWindow;
        look.border = m_hover ? wxColour(176, 181, 189) : Ui::kBorder;
        look.text = m_kind == Toggle ? Ui::kTextSecondary : Ui::kText;
        break;
    }
    return look;
}

void FlatButton::OnPaint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(this);
    const wxSize size = GetClientSize();
    dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
    dc.Clear();
    if (size.x <= 0 || size.y <= 0)
    {
        return;
    }

    const Look look = CurrentLook();
    {
        std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::CreateFromUnknownDC(dc));
        if (gc)
        {
            const double radius = FromDIP(4);
            if (look.fill.IsOk() || look.border.IsOk())
            {
                gc->SetPen(look.border.IsOk() ? wxPen(look.border) : *wxTRANSPARENT_PEN);
                gc->SetBrush(look.fill.IsOk() ? wxBrush(look.fill) : *wxTRANSPARENT_BRUSH);
                gc->DrawRoundedRectangle(0.5, 0.5, size.x - 1.0, size.y - 1.0, radius);
            }
            if (m_keyboardFocus && HasFocus())
            {
                const bool filled = m_kind == Primary || (m_kind == Toggle && m_checked);
                gc->SetPen(wxPen(filled ? *wxWHITE : Ui::kAccent));
                gc->SetBrush(*wxTRANSPARENT_BRUSH);
                gc->DrawRoundedRectangle(2.5, 2.5, size.x - 5.0, size.y - 5.0, std::max(1.0, radius - 2.0));
            }
        }
    }

    // The text is drawn with GDI, after the graphics context is gone, so it
    // looks like the rest of the window's text (ClearType).
    dc.SetBackgroundMode(wxBRUSHSTYLE_TRANSPARENT);
    dc.SetTextForeground(look.text);
    const wxFont font = LabelFont();
    wxString label = DrawsLabel() ? GetLabelText() : wxString();
    wxSize labelSize(0, 0);
    if (!label.empty())
    {
        dc.SetFont(font);
        labelSize = dc.GetTextExtent(label);
    }
    wxFont icons;
    wxString glyph;
    wxSize glyphSize(0, 0);
    if (DrawsGlyph())
    {
        icons = Ui::IconFont(font);
        glyph = wxString(m_glyph);
        dc.SetFont(icons);
        glyphSize = dc.GetTextExtent(glyph);
    }
    const int gap = glyphSize.x && labelSize.x ? FromDIP(6) : 0;
    const int room = size.x - 2 * FromDIP(6) - glyphSize.x - gap;
    if (labelSize.x > room)
    {
        dc.SetFont(font);
        label = wxControl::Ellipsize(label, dc, wxELLIPSIZE_END, std::max(0, room));
        labelSize = dc.GetTextExtent(label);
    }
    int x = (size.x - (glyphSize.x + gap + labelSize.x)) / 2;
    if (glyphSize.x)
    {
        dc.SetFont(icons);
        dc.DrawText(glyph, x, (size.y - glyphSize.y) / 2);
        x += glyphSize.x + gap;
    }
    if (labelSize.x)
    {
        dc.SetFont(font);
        dc.DrawText(label, x, (size.y - labelSize.y) / 2);
    }
}

void FlatButton::OnLeftDown(wxMouseEvent&)
{
    if (!IsEnabled())
    {
        return;
    }
    m_pressed = true;
    m_hover = true;
    m_keyboardFocus = false;
    if (!HasCapture())
    {
        CaptureMouse();
    }
    Refresh();
}

void FlatButton::OnMotion(wxMouseEvent& event)
{
    if (m_pressed)
    {
        const bool inside = GetClientRect().Contains(event.GetPosition());
        if (inside != m_hover)
        {
            m_hover = inside;
            Refresh();
        }
    }
    event.Skip();
}

void FlatButton::OnLeftUp(wxMouseEvent& event)
{
    const bool inside = GetClientRect().Contains(event.GetPosition());
    const bool clicked = m_pressed && inside;
    m_pressed = false;
    m_hover = inside;
    if (HasCapture())
    {
        ReleaseMouse();
    }
    Refresh();
    if (clicked)
    {
        Click();
    }
}

void FlatButton::OnKeyDown(wxKeyEvent& event)
{
    if (!m_keyboardFocus)
    {
        m_keyboardFocus = true;
        Refresh();
    }
    switch (event.GetKeyCode())
    {
    case WXK_SPACE:
    case WXK_RETURN:
    case WXK_NUMPAD_ENTER:
        Click();
        break;
    default:
        event.Skip();
        break;
    }
}

// ---------------------------------------------------------------------------

Card::Card(wxWindow* parent, const wxColour& fill, const wxColour& border, int radius) :
    wxPanel(parent, wxID_ANY),
    m_content(new wxBoxSizer(wxVERTICAL)),
    m_bannerSpace(nullptr),
    m_fill(fill),
    m_border(border),
    m_radius(radius),
    m_banner(false),
    m_seed(0)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetBackgroundColour(fill);
    wxBoxSizer* outer = new wxBoxSizer(wxVERTICAL);
    m_bannerSpace = outer->Add(0, 0);
    outer->Add(m_content, 1, wxEXPAND);
    SetSizer(outer);

    Bind(wxEVT_PAINT, &Card::OnPaint, this);
    Bind(wxEVT_SIZE, [this](wxSizeEvent& event)
    {
        Refresh();
        event.Skip();
    });
}

void Card::SetColours(const wxColour& fill, const wxColour& border)
{
    if (fill == m_fill && border == m_border)
    {
        return;
    }
    m_fill = fill;
    m_border = border;
    SetBackgroundColour(fill);
    Refresh();
    // The children (text, buttons) paint their background in the card's colour.
    for (wxWindowList::compatibility_iterator node = GetChildren().GetFirst(); node; node = node->GetNext())
    {
        node->GetData()->Refresh();
    }
}

void Card::SetBanner(const wxImage& picture, const wxString& title, unsigned long long seed)
{
    m_picture = wxImage();
    if (picture.IsOk() && picture.GetWidth() > 0 && picture.GetHeight() > 0)
    {
        // Keep a smaller copy; the banner is scaled from it whenever its size changes.
        const int height = FromDIP(kBannerHeight) * 2;
        if (picture.GetHeight() > height)
        {
            const int width = std::max(1, (int)((double)picture.GetWidth() * height / picture.GetHeight()));
            m_picture = picture.Scale(width, height, wxIMAGE_QUALITY_HIGH);
        }
        else
        {
            m_picture = picture;
        }
    }
    m_title = title;
    m_seed = seed;
    m_banner = true;
    m_bannerCache = wxNullBitmap;
    m_bannerSpace->AssignSpacer(1, FromDIP(kBannerHeight));
    // The window's text, for automation (the title itself is drawn).
    wxPanel::SetLabel(title);
    Refresh();
}

void Card::SetBannerTitle(const wxString& title)
{
    if (title != m_title)
    {
        m_title = title;
        wxPanel::SetLabel(title);
        Refresh();
    }
}

void Card::ClearBanner()
{
    if (!m_banner)
    {
        return;
    }
    m_banner = false;
    m_picture = wxImage();
    m_title.clear();
    m_bannerCache = wxNullBitmap;
    m_bannerSpace->AssignSpacer(0, 0);
    wxPanel::SetLabel(wxEmptyString);
    Refresh();
}

wxBitmap Card::BannerBitmap(const wxSize& size)
{
    if (m_bannerCache.IsOk() && m_bannerCacheSize == size)
    {
        return m_bannerCache;
    }

    wxImage image;
    if (m_picture.IsOk())
    {
        // Cover the banner, cropping what sticks out on either side.
        const double scale = std::max((double)size.x / m_picture.GetWidth(), (double)size.y / m_picture.GetHeight());
        const int width = std::max(size.x, (int)std::ceil(m_picture.GetWidth() * scale));
        const int height = std::max(size.y, (int)std::ceil(m_picture.GetHeight() * scale));
        image = m_picture.Scale(width, height, wxIMAGE_QUALITY_HIGH)
                         .GetSubImage(wxRect((width - size.x) / 2, (height - size.y) / 2, size.x, size.y));
    }
    else
    {
        // No picture (e.g. a non-Steam game): a colour gradient of its own.
        static const unsigned char gradients[][6] =
        {
            { 59, 31, 74, 192, 138, 62 }, { 16, 62, 115, 40, 140, 190 }, { 20, 90, 70, 90, 170, 110 },
            { 110, 30, 40, 210, 110, 60 }, { 40, 40, 80, 120, 80, 170 }, { 25, 70, 90, 60, 150, 150 },
        };
        const unsigned char* g = gradients[Scramble(m_seed) % (sizeof(gradients) / sizeof(gradients[0]))];
        image.Create(size.x, size.y, false);
        unsigned char* data = image.GetData();
        for (int y = 0; y < size.y; ++y)
        {
            for (int x = 0; x < size.x; ++x)
            {
                const double t = std::min(1.0, 0.75 * x / std::max(1, size.x - 1) + 0.25 * y / std::max(1, size.y - 1));
                unsigned char* p = data + (y * size.x + x) * 3;
                p[0] = Mix(g[0], g[3], t);
                p[1] = Mix(g[1], g[4], t);
                p[2] = Mix(g[2], g[5], t);
            }
        }
    }

    // Darker towards the bottom, so the white title stands out on any picture.
    unsigned char* data = image.GetData();
    for (int y = 0; y < size.y; ++y)
    {
        const double t = (double)y / size.y;
        const double shade = t < 0.3 ? 0.0 : (t - 0.3) / 0.7 * 0.62;
        if (shade <= 0.0)
        {
            continue;
        }
        unsigned char* p = data + y * size.x * 3;
        for (int i = 0; i < size.x * 3; ++i)
        {
            p[i] = (unsigned char)std::lround(p[i] * (1.0 - shade));
        }
    }
    RoundTopCorners(image, FromDIP(m_radius));

    m_bannerCache = wxBitmap(image);
    m_bannerCacheSize = size;
    return m_bannerCache;
}

void Card::OnPaint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(this);
    const wxSize size = GetClientSize();
    dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
    dc.Clear();
    if (size.x <= 2 || size.y <= 2)
    {
        return;
    }

    const double radius = FromDIP(m_radius);
    {
        std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::CreateFromUnknownDC(dc));
        if (gc)
        {
            gc->SetPen(*wxTRANSPARENT_PEN);
            gc->SetBrush(wxBrush(m_fill));
            gc->DrawRoundedRectangle(0, 0, size.x, size.y, radius);
        }
    }

    const int bannerHeight = m_banner ? std::min(size.y, FromDIP(kBannerHeight)) : 0;
    if (bannerHeight > 0)
    {
        dc.DrawBitmap(BannerBitmap(wxSize(size.x, bannerHeight)), 0, 0, true);
    }

    if (m_border.IsOk())
    {
        std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::CreateFromUnknownDC(dc));
        if (gc)
        {
            gc->SetPen(wxPen(m_border));
            gc->SetBrush(*wxTRANSPARENT_BRUSH);
            gc->DrawRoundedRectangle(0.5, 0.5, size.x - 1.0, size.y - 1.0, radius);
        }
    }

    if (bannerHeight > 0 && !m_title.empty())
    {
        const int pad = FromDIP(12);
        dc.SetFont(Ui::SemiBold(GetFont()).Scaled(1.35f));
        dc.SetTextForeground(*wxWHITE);
        dc.SetBackgroundMode(wxBRUSHSTYLE_TRANSPARENT);
        const wxString title = wxControl::Ellipsize(m_title, dc, wxELLIPSIZE_END, std::max(0, size.x - 2 * pad));
        const wxSize text = dc.GetTextExtent(title);
        dc.DrawText(title, pad, bannerHeight - FromDIP(9) - text.y);
    }
}

// ---------------------------------------------------------------------------

AvatarCtrl::AvatarCtrl(wxWindow* parent, int size) :
    wxWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE),
    m_size(size),
    m_greyed(false)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetInitialSize(FromDIP(wxSize(size, size)));
    Bind(wxEVT_PAINT, &AvatarCtrl::OnPaint, this);
}

wxSize AvatarCtrl::DoGetBestClientSize() const
{
    return FromDIP(wxSize(m_size, m_size));
}

void AvatarCtrl::SetPerson(const wxImage& picture, const wxString& name, unsigned long long seed)
{
    m_picture = picture;
    m_round = wxNullBitmap;
    m_initials = Ui::Initials(name);
    m_placeholder = Ui::PlaceholderColour(seed);
    Refresh();
}

void AvatarCtrl::SetDot(const wxColour& colour)
{
    if (colour != m_dot)
    {
        m_dot = colour;
        Refresh();
    }
}

void AvatarCtrl::SetGreyed(bool greyed)
{
    if (greyed != m_greyed)
    {
        m_greyed = greyed;
        m_round = wxNullBitmap;
        Refresh();
    }
}

void AvatarCtrl::OnPaint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(this);
    const wxColour surface = GetParent()->GetBackgroundColour();
    dc.SetBackground(wxBrush(surface));
    dc.Clear();
    const wxSize size = GetClientSize();
    const int side = std::min(size.x, size.y);
    if (side <= 0)
    {
        return;
    }
    if (m_picture.IsOk() && (!m_round.IsOk() || m_round.GetWidth() != side))
    {
        m_round = Ui::RoundPicture(m_picture, side, m_greyed);
    }
    dc.SetFont(GetFont());
    Ui::DrawAvatar(dc, wxRect((size.x - side) / 2, (size.y - side) / 2, side, side),
                   m_picture.IsOk() ? m_round : wxNullBitmap, m_initials, m_placeholder, m_dot, surface);
}

// ---------------------------------------------------------------------------

StatusDot::StatusDot(wxWindow* parent, int size) :
    wxWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE),
    m_size(size),
    m_colour(Ui::kTextMuted)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetInitialSize(FromDIP(wxSize(size, size)));
    Bind(wxEVT_PAINT, &StatusDot::OnPaint, this);
}

wxSize StatusDot::DoGetBestClientSize() const
{
    return FromDIP(wxSize(m_size, m_size));
}

void StatusDot::SetColour(const wxColour& colour)
{
    if (colour != m_colour)
    {
        m_colour = colour;
        Refresh();
    }
}

void StatusDot::OnPaint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
    dc.Clear();
    const wxSize size = GetClientSize();
    const int side = std::min(size.x, size.y);
    std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::CreateFromUnknownDC(dc));
    if (gc && side > 0)
    {
        gc->SetPen(*wxTRANSPARENT_PEN);
        gc->SetBrush(wxBrush(m_colour));
        gc->DrawEllipse((size.x - side) / 2.0, (size.y - side) / 2.0, side, side);
    }
}

// ---------------------------------------------------------------------------

WrapLabel::WrapLabel(wxWindow* parent, const wxString& text, bool keycaps) :
    wxControl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE),
    m_keycaps(keycaps),
    m_spaceWidth(0),
    m_textHeight(0),
    m_keyHeight(0),
    m_laidOutWidth(0),
    m_relayoutQueued(false)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    wxControl::SetLabel(text);
    Measure();
    SetInitialSize();
    Bind(wxEVT_PAINT, &WrapLabel::OnPaint, this);
    Bind(wxEVT_SIZE, &WrapLabel::OnSize, this);
}

void WrapLabel::SetLabel(const wxString& text)
{
    if (text != GetLabel())
    {
        wxControl::SetLabel(text);
        Measure();
        InvalidateBestSize();
        Refresh();
    }
}

bool WrapLabel::SetFont(const wxFont& font)
{
    if (!wxControl::SetFont(font))
    {
        return false;
    }
    Measure();
    InvalidateBestSize();
    Refresh();
    return true;
}

wxFont WrapLabel::KeyFont() const
{
    return GetFont().Scaled(0.88f);
}

void WrapLabel::Measure()
{
    m_words.clear();
    const wxFont font = GetFont();
    const wxFont keyFont = KeyFont();
    int unused = 0;
    GetTextExtent(" ", &m_spaceWidth, &unused, nullptr, nullptr, &font);
    GetTextExtent("Ag", &unused, &m_textHeight, nullptr, nullptr, &font);
    m_keyHeight = 0;
    if (m_keycaps)
    {
        int keyText = 0;
        GetTextExtent("Ag", &unused, &keyText, nullptr, nullptr, &keyFont);
        m_keyHeight = keyText + FromDIP(3);
    }
    const int keyPad = FromDIP(4);
    const int partGap = FromDIP(2);

    const wxArrayString lines = wxSplit(GetLabel(), '\n', '\0');
    for (size_t l = 0; l < lines.size(); ++l)
    {
        const wxArrayString words = wxSplit(lines[l], ' ', '\0');
        for (size_t w = 0; w < words.size(); ++w)
        {
            if (words[w].empty())
            {
                continue;
            }
            Word word;
            word.width = 0;
            word.lineBreak = false;

            // "[Ctrl]+[Alt]+[K]" is keys and the text between them.
            const wxString& s = words[w];
            size_t pos = 0;
            while (pos < s.length())
            {
                Part part;
                part.key = false;
                size_t close = wxString::npos;
                if (m_keycaps && s[pos] == '[')
                {
                    close = s.find(']', pos + 1);
                }
                if (close != wxString::npos && close > pos + 1)
                {
                    part.key = true;
                    part.text = s.substr(pos + 1, close - pos - 1);
                    pos = close + 1;
                }
                else
                {
                    size_t next = m_keycaps ? s.find('[', pos + 1) : wxString::npos;
                    if (next == wxString::npos)
                    {
                        next = s.length();
                    }
                    part.text = s.substr(pos, next - pos);
                    pos = next;
                }
                int height = 0;
                GetTextExtent(part.text, &part.width, &height, nullptr, nullptr, part.key ? &keyFont : &font);
                if (part.key)
                {
                    part.width += 2 * keyPad;
                }
                if (!word.parts.empty() && (part.key || word.parts.back().key))
                {
                    word.width += partGap;
                }
                word.width += part.width;
                word.parts.push_back(part);
            }
            m_words.push_back(word);
        }
        if (l + 1 < lines.size())
        {
            Word lineBreak;
            lineBreak.width = 0;
            lineBreak.lineBreak = true;
            m_words.push_back(lineBreak);
        }
    }
}

int WrapLabel::LineHeight() const
{
    return m_keycaps ? std::max(m_textHeight, m_keyHeight) + FromDIP(4) : m_textHeight;
}

int WrapLabel::HeightFor(int width) const
{
    if (m_words.empty())
    {
        return 0;
    }
    int lines = 1, x = 0;
    for (size_t i = 0; i < m_words.size(); ++i)
    {
        const Word& word = m_words[i];
        if (word.lineBreak)
        {
            ++lines;
            x = 0;
        }
        else if (x > 0 && x + m_spaceWidth + word.width > width)
        {
            ++lines;
            x = word.width;
        }
        else
        {
            x += (x > 0 ? m_spaceWidth : 0) + word.width;
        }
    }
    return lines * LineHeight();
}

wxSize WrapLabel::DoGetBestClientSize() const
{
    // Narrow, so the text never makes its window wider; as tall as the text
    // is at the width it was last given (on one line until then).
    int natural = 0;
    for (size_t i = 0; i < m_words.size(); ++i)
    {
        natural += m_words[i].width + (i ? m_spaceWidth : 0);
    }
    const int width = m_laidOutWidth > 0 ? m_laidOutWidth : natural;
    return wxSize(std::min(natural, FromDIP(60)), HeightFor(std::max(1, width)));
}

int WrapLabel::DoGetBestClientHeight(int width) const
{
    return width > 0 ? HeightFor(width) : wxDefaultCoord;
}

void WrapLabel::OnSize(wxSizeEvent& event)
{
    event.Skip();
    Refresh();
    const wxSize size = GetClientSize();
    if (size.x <= 0 || size.x == m_laidOutWidth)
    {
        return;
    }
    m_laidOutWidth = size.x;
    if (HeightFor(size.x) != size.y && !m_relayoutQueued)
    {
        // A new width needs a different height: lay the window out again
        // once this size change is over.
        InvalidateBestSize();
        m_relayoutQueued = true;
        CallAfter([this]()
        {
            m_relayoutQueued = false;
            Ui::Relayout(GetParent());
        });
    }
}

void WrapLabel::OnPaint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
    dc.Clear();

    struct Placed
    {
        const Part* part;
        int x;
        int y;
    };
    std::vector<Placed> placed;
    const int width = GetClientSize().x;
    const int lineHeight = LineHeight();
    const int partGap = FromDIP(2);
    int x = 0, y = 0;
    for (size_t i = 0; i < m_words.size(); ++i)
    {
        const Word& word = m_words[i];
        if (word.lineBreak)
        {
            x = 0;
            y += lineHeight;
            continue;
        }
        if (x > 0 && x + m_spaceWidth + word.width > width)
        {
            x = 0;
            y += lineHeight;
        }
        else if (x > 0)
        {
            x += m_spaceWidth;
        }
        int px = x;
        for (size_t p = 0; p < word.parts.size(); ++p)
        {
            if (p > 0 && (word.parts[p].key || word.parts[p - 1].key))
            {
                px += partGap;
            }
            Placed item = { &word.parts[p], px, y };
            placed.push_back(item);
            px += word.parts[p].width;
        }
        x += word.width;
    }

    // Keys first (with a graphics context), then all the text with GDI.
    const int keyTop = (lineHeight - m_keyHeight) / 2;
    if (m_keycaps)
    {
        std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::CreateFromUnknownDC(dc));
        if (gc)
        {
            const double radius = FromDIP(3);
            gc->SetPen(*wxTRANSPARENT_PEN);
            for (size_t i = 0; i < placed.size(); ++i)
            {
                if (!placed[i].part->key)
                {
                    continue;
                }
                const double kx = placed[i].x, ky = placed[i].y + keyTop;
                const double kw = placed[i].part->width, kh = m_keyHeight;
                // An outline that's a little thicker at the bottom, like a key.
                gc->SetBrush(wxBrush(Ui::kBorder));
                gc->DrawRoundedRectangle(kx, ky, kw, kh, radius);
                gc->SetBrush(wxBrush(*wxWHITE));
                gc->DrawRoundedRectangle(kx + 1, ky + 1, kw - 2, kh - 3, std::max(1.0, radius - 1));
            }
        }
    }

    dc.SetBackgroundMode(wxBRUSHSTYLE_TRANSPARENT);
    const wxFont font = GetFont();
    const wxFont keyFont = KeyFont();
    for (size_t i = 0; i < placed.size(); ++i)
    {
        const Part& part = *placed[i].part;
        if (part.key)
        {
            dc.SetFont(keyFont);
            dc.SetTextForeground(Ui::kText);
            const wxSize text = dc.GetTextExtent(part.text);
            dc.DrawText(part.text, placed[i].x + (part.width - text.x) / 2,
                        placed[i].y + keyTop + (m_keyHeight - 1 - text.y) / 2);
        }
        else
        {
            dc.SetFont(font);
            dc.SetTextForeground(GetForegroundColour());
            dc.DrawText(part.text, placed[i].x, placed[i].y + (lineHeight - m_textHeight) / 2);
        }
    }
}
