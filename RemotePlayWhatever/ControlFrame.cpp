#include <algorithm>
#include <ctime>
#include "ControlFrame.h"
#include "wxSteamStuff.h"
#include "UiHelpers.h"
#include "UiWidgets.h"
#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>
#endif

// Key used by the "Everyone" row's toggles; never a real guest key.
static const uint64 kEveryone = 0;

enum
{
    kHotKeyKeyboard = 0xB001,
    kHotKeyMouse,
    kHotKeyController,
    kHotKeyTakeControl,
    kHotKeyGiveBack,
};

static const char* const kPeripheral[3] = { "Keyboard", "Mouse", "Controller" };
static const wchar_t kPeripheralGlyph[3] = { Ui::kGlyphKeyboard, Ui::kGlyphMouse, Ui::kGlyphController };

static wxString JoinedText(long long joinedAt)
{
    const long long minutes = std::max(0LL, ((long long)std::time(nullptr) - joinedAt) / 60);
    if (minutes < 1)
    {
        return "Joined just now";
    }
    if (minutes < 60)
    {
        return wxString::Format("Joined %lld min ago", minutes);
    }
    const long long hours = minutes / 60;
    return hours == 1 ? wxString("Joined 1 hour ago") : wxString::Format("Joined %lld hours ago", hours);
}

static wxString ToggleTip(int which, bool on, bool everyone)
{
    const wxString what = wxString(kPeripheral[which]).Lower();
    if (everyone)
    {
        return on ? "Every guest can use their " + what + ". Click to take it from all of them."
                  : "Not every guest can use their " + what + ". Click to give it to all of them.";
    }
    return on ? wxString(kPeripheral[which]) + ": on. Click to take it away."
              : wxString(kPeripheral[which]) + ": off. Click to give it back.";
}

ControlFrame::ControlFrame(RemotePlayInviteHandler* handler) :
    wxFrame(NULL, wxID_ANY, "Remote Play Control", wxDefaultPosition, wxDefaultSize),
    m_handler(handler),
    m_body(nullptr),
    m_title(nullptr),
    m_everyone(nullptr),
    m_container(nullptr),
    m_sizer(nullptr),
    m_footer(nullptr),
    m_hint(nullptr),
    m_timer(this),
    m_lastRevision((uint32)-1),
    m_lastGuestCount(0),
    m_pollTicks(0),
    m_waitingForNames(false),
    m_hotkeysActive(false)
{
    for (int i = 0; i < 3; ++i)
    {
        m_everyoneToggles[i] = nullptr;
    }
    SetIcons(AppIcons());

    m_body = new wxPanel(this, wxID_ANY);
    m_body->SetBackgroundColour(Ui::kWindow);
    wxBoxSizer* bodySizer = new wxBoxSizer(wxVERTICAL);

    // "Guests (2)" and End session.
    wxBoxSizer* header = new wxBoxSizer(wxHORIZONTAL);
    m_title = new wxStaticText(m_body, wxID_ANY, "Guests");
    m_title->SetFont(Ui::SemiBold(GetFont()).Scaled(1.2f));
    m_title->SetForegroundColour(Ui::kText);
    header->Add(m_title, 1, wxALIGN_CENTER_VERTICAL);
    FlatButton* endButton = new FlatButton(m_body, "End session");
    endButton->SetToolTip("Disconnect everyone, cancel unused invites and guest links, and stop sharing your screen.");
    endButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&)
    {
        int answer = wxMessageBox(
            "Disconnect everyone and stop sharing?\n\nUnused invites and guest links will stop working too.",
            "Remote Play Control", wxYES_NO | wxICON_QUESTION, this);
        if (answer == wxYES)
        {
            m_handler->EndSession();
            Rebuild();
        }
    });
    header->Add(endButton, 0, wxALIGN_CENTER_VERTICAL);
    bodySizer->Add(header, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(14));

    // With several guests, "Everyone" turns a peripheral on or off for all of them.
    m_everyone = new wxPanel(m_body, wxID_ANY);
    m_everyone->SetBackgroundColour(Ui::kWindow);
    wxBoxSizer* everyone = new wxBoxSizer(wxHORIZONTAL);
    wxStaticText* label = new wxStaticText(m_everyone, wxID_ANY, "Everyone:");
    label->SetForegroundColour(Ui::kTextSecondary);
    everyone->Add(label, 0, wxALIGN_CENTER_VERTICAL);
    for (int i = 0; i < 3; ++i)
    {
        m_everyoneToggles[i] = AddToggle(m_everyone, everyone, i, kEveryone);
    }
    m_everyone->SetSizer(everyone);
    bodySizer->Add(m_everyone, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(14));
    m_everyone->Hide();

    // A card per guest.
    m_container = new wxScrolledWindow(m_body, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
    m_container->SetBackgroundColour(Ui::kWindow);
    m_container->SetScrollRate(0, FromDIP(10));
    m_sizer = new wxBoxSizer(wxVERTICAL);
    m_container->SetSizer(m_sizer);
    bodySizer->Add(m_container, 1, wxEXPAND | wxTOP, FromDIP(2));

    // The in-game shortcuts, with the keys drawn as keys.
    m_footer = new wxPanel(m_body, wxID_ANY);
    m_footer->SetBackgroundColour(Ui::kFooter);
    wxBoxSizer* footer = new wxBoxSizer(wxVERTICAL);
    wxWindow* divider = new wxWindow(m_footer, wxID_ANY);
    divider->SetBackgroundColour(Ui::kDivider);
    divider->SetMinSize(wxSize(-1, 1));
    footer->Add(divider, 0, wxEXPAND);
    m_hint = new WrapLabel(m_footer, wxEmptyString, true);
    m_hint->SetFont(GetFont().Scaled(0.92f));
    m_hint->SetForegroundColour(Ui::kTextSecondary);
    footer->Add(m_hint, 0, wxEXPAND | wxALL, FromDIP(10));
    m_footer->SetSizer(footer);
    bodySizer->Add(m_footer, 0, wxEXPAND);

    m_body->SetSizer(bodySizer);
    wxBoxSizer* frameSizer = new wxBoxSizer(wxVERTICAL);
    frameSizer->Add(m_body, 1, wxEXPAND);
    SetSizer(frameSizer);
    SetMinSize(FromDIP(wxSize(470, 260)));
    SetSize(FromDIP(wxSize(560, 400)));

    // The Ctrl+Alt shortcuts are registered only while guests are connected
    // (see OnPoll), so the combinations stay free for other apps otherwise.
#if wxUSE_HOTKEY
    Bind(wxEVT_HOTKEY, &ControlFrame::OnHotKey, this);
#endif
    ShowHint();

    Bind(wxEVT_TIMER, &ControlFrame::OnPoll, this);
    Bind(wxEVT_CLOSE_WINDOW, &ControlFrame::OnClose, this);
    m_timer.Start(500);

    Rebuild();
}

ControlFrame::~ControlFrame()
{
#if wxUSE_HOTKEY
    for (size_t i = 0; i < m_hotkeys.size(); ++i)
    {
        UnregisterHotKey(m_hotkeys[i]);
    }
#endif
}

void ControlFrame::SetHotKeysActive(bool on)
{
#if wxUSE_HOTKEY
    if (on == m_hotkeysActive)
    {
        return;
    }
    m_hotkeysActive = on;
    m_hintKeys.clear();
    if (on)
    {
        struct HotKey { int id; int key; const char* what; };
        static const HotKey keys[] =
        {
            { kHotKeyKeyboard,    'K', "keyboard" },
            { kHotKeyMouse,       'M', "mouse" },
            { kHotKeyController,  'C', "controller" },
            { kHotKeyTakeControl, 'D', "take all control" },
            { kHotKeyGiveBack,    'G', "give it back" },
        };

        // A combination another app already owns, or one that types a letter
        // with AltGr on this keyboard (Ctrl+Alt is AltGr), isn't offered.
        for (const HotKey& k : keys)
        {
            if (CtrlAltKeyIsFree(k.key) && RegisterHotKey(k.id, wxMOD_CONTROL | wxMOD_ALT, k.key))
            {
                m_hotkeys.push_back(k.id);
                HintKey hint;
                hint.key = (char)k.key;
                hint.what = k.what;
                m_hintKeys.push_back(hint);
            }
        }
    }
    else
    {
        for (size_t i = 0; i < m_hotkeys.size(); ++i)
        {
            UnregisterHotKey(m_hotkeys[i]);
        }
        m_hotkeys.clear();
    }
    ShowHint();
#else
    wxUnusedVar(on);
#endif
}

void ControlFrame::AddShortcutHint(char key, const wxString& what)
{
    HintKey hint;
    hint.key = key;
    hint.what = what;
    m_extraHints.push_back(hint);
    ShowHint();
}

void ControlFrame::ShowHint()
{
    // "In the game: [Ctrl]+[Alt]+[K]/[M]/[C] everyone's keyboard / mouse /
    // controller · [D] take all control · ..." (keys in brackets are drawn as keys).
    std::vector<HintKey> all = m_hintKeys;
    all.insert(all.end(), m_extraHints.begin(), m_extraHints.end());
    wxString peripheralKeys, peripheralWords;
    std::vector<std::pair<wxString, wxString>> items;
    for (size_t i = 0; i < all.size(); ++i)
    {
        const wxString key = wxString::Format("[%c]", all[i].key);
        if (all[i].key == 'K' || all[i].key == 'M' || all[i].key == 'C')
        {
            peripheralKeys += (peripheralKeys.empty() ? "" : "/") + key;
            peripheralWords += (peripheralWords.empty() ? "" : " / ") + all[i].what;
        }
        else
        {
            items.push_back(std::make_pair(key, all[i].what));
        }
    }
    if (!peripheralKeys.empty())
    {
        items.insert(items.begin(), std::make_pair(peripheralKeys, "everyone's " + peripheralWords));
    }

    wxString hint;
    for (size_t i = 0; i < items.size(); ++i)
    {
        if (i)
        {
            hint += Ui::Separator();
        }
        hint += (i == 0 ? "[Ctrl]+[Alt]+" : "") + items[i].first + " " + items[i].second;
    }
    if (!hint.empty())
    {
        hint = "In the game: " + hint;
    }
    m_hint->SetLabel(hint);
    m_footer->Show(!hint.empty());
    Ui::Relayout(m_footer);
}

void ControlFrame::OnHotKey(wxKeyEvent& evt)
{
    if (m_handler->Guests().empty())
    {
        return;
    }

    bool gave = true;
    switch (evt.GetId())
    {
    case kHotKeyKeyboard:    gave = !m_handler->AllGuestsHave(0); m_handler->SetAllGuestsInput(0, gave, false); break;
    case kHotKeyMouse:       gave = !m_handler->AllGuestsHave(1); m_handler->SetAllGuestsInput(1, gave, false); break;
    case kHotKeyController:  gave = !m_handler->AllGuestsHave(2); m_handler->SetAllGuestsInput(2, gave, false); break;
    case kHotKeyTakeControl: gave = false; m_handler->TakeAllControl(); break;
    case kHotKeyGiveBack:    gave = true;  m_handler->GiveBackControl(); break;
    default:                 return;
    }

#ifdef __WXMSW__
    // The window is usually hidden behind the game, so confirm with a sound.
    ::MessageBeep(gave ? MB_OK : MB_ICONEXCLAMATION);
#endif
    if (RowsMatchGuests())
    {
        RefreshRows();
    }
    else
    {
        Rebuild();
    }
}

void ControlFrame::OnClose(wxCloseEvent& evt)
{
    // Closing only hides the window; it comes back when the next guest joins.
    // (Quitting the app destroys it directly from the main window.)
    if (evt.CanVeto())
    {
        evt.Veto();
        Hide();
        return;
    }
    evt.Skip();
}

bool ControlFrame::RowsMatchGuests() const
{
    const std::vector<ConnectedGuest>& guests = m_handler->Guests();
    if (guests.size() != m_rows.size())
    {
        return false;
    }
    for (size_t i = 0; i < guests.size(); ++i)
    {
        if (guests[i].key != m_rows[i].key)
        {
            return false;
        }
    }
    return true;
}

void ControlFrame::OnPoll(wxTimerEvent&)
{
    SetHotKeysActive(!m_handler->Guests().empty());

    if (!RowsMatchGuests())
    {
        Rebuild();
    }
    // Control changes, names of guests who joined through a link (Steam sends
    // them a moment after they connect), and "Joined N min ago" every 10 s.
    else if (m_handler->RosterRevision() != m_lastRevision || m_waitingForNames || ++m_pollTicks >= 20)
    {
        RefreshRows();
    }

    // Pop up when a new guest joins, without taking focus away from the game
    // (which can minimize a fullscreen game). Flash the taskbar button instead.
    size_t count = m_handler->Guests().size();
    if (count > m_lastGuestCount && !IsShown())
    {
        ShowWithoutActivating();
        RequestUserAttention(wxUSER_ATTENTION_INFO);
    }
    m_lastGuestCount = count;
}

FlatButton* ControlFrame::AddToggle(wxWindow* parent, wxSizer* sizer, int which, uint64 guestKey)
{
    FlatButton* button = new FlatButton(parent, kPeripheral[which], FlatButton::Toggle, kPeripheralGlyph[which], true);
    button->SetFont(GetFont().Scaled(0.92f));
    button->SetMinSize(wxSize(FromDIP(90), -1));
    sizer->Add(button, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(6));
    button->Bind(wxEVT_BUTTON, [this, guestKey, which](wxCommandEvent&)
    {
        Toggle(guestKey, which);
    });
    return button;
}

// Flips one peripheral for one guest, or for everyone on the "Everyone" row.
void ControlFrame::Toggle(uint64 guestKey, int which)
{
    if (guestKey == kEveryone)
    {
        m_handler->SetAllGuestsInput(which, !m_handler->AllGuestsHave(which), true);
    }
    else
    {
        const std::vector<ConnectedGuest>& guests = m_handler->Guests();
        for (size_t i = 0; i < guests.size(); ++i)
        {
            if (guests[i].key == guestKey)
            {
                bool k = guests[i].wantKbd;
                bool m = guests[i].wantMouse;
                bool c = guests[i].wantCtrl;
                if (which == 0)      k = !k;
                else if (which == 1) m = !m;
                else                 c = !c;
                m_handler->SetGuestInput(guestKey, k, m, c);
                break;
            }
        }
    }
    if (RowsMatchGuests())
    {
        RefreshRows();
    }
    else
    {
        // Rebuilding destroys the clicked button, so wait until its click is handled.
        CallAfter([this]() { Rebuild(); });
    }
}

wxString ControlFrame::GuestName(const ConnectedGuest& g, bool& placeholder)
{
    placeholder = false;
    if (!g.steamID)
    {
        return wxString::Format("Steam Link guest %u", g.player.m_unGuestID);
    }

    const char* persona = GClientContext()->SteamFriends()->GetFriendPersonaName(g.player.m_playerID);
    wxString name = wxString::FromUTF8(persona ? persona : "");
    if (name.IsEmpty() || name == "[unknown]")
    {
        placeholder = true;
        return "Guest (joined with a link)";
    }
    return name;
}

void ControlFrame::RefreshRows()
{
    m_lastRevision = m_handler->RosterRevision();
    m_pollTicks = 0;
    m_waitingForNames = false;

    const std::vector<ConnectedGuest>& guests = m_handler->Guests();
    for (size_t i = 0; i < guests.size() && i < m_rows.size(); ++i)
    {
        const ConnectedGuest& g = guests[i];
        GuestRow& row = m_rows[i];

        bool placeholder = false;
        const wxString name = GuestName(g, placeholder);
        m_waitingForNames = m_waitingForNames || placeholder;
        if (name != row.shownName || !row.hasPicture)
        {
            // Link guests' names and pictures arrive a little after they join.
            const wxImage picture = g.steamID ? SteamAvatarImage(g.player.m_playerID) : wxImage();
            if (name != row.shownName || picture.IsOk())
            {
                row.shownName = name;
                row.name->SetLabelText(name);
                row.name->SetToolTip(name);
                row.remove->SetToolTip("Remove " + name + " from the session");
                row.avatar->SetPerson(picture, g.steamID ? name : wxString::Format("G%u", g.player.m_unGuestID), g.key);
                row.hasPicture = picture.IsOk();
            }
        }
        const wxString joined = (g.steamID ? wxString() : "No Steam account" + Ui::Separator()) + JoinedText(g.joinedAt);
        if (row.joined->GetLabelText() != joined)
        {
            row.joined->SetLabelText(joined);
        }
        const bool on[3] = { g.wantKbd, g.wantMouse, g.wantCtrl };
        for (int t = 0; t < 3; ++t)
        {
            row.toggles[t]->SetChecked(on[t]);
            const wxString tip = ToggleTip(t, on[t], false);
            if (row.toggles[t]->GetToolTipText() != tip)
            {
                row.toggles[t]->SetToolTip(tip);
            }
        }
    }
    for (int t = 0; t < 3; ++t)
    {
        const bool on = m_handler->AllGuestsHave(t);
        m_everyoneToggles[t]->SetChecked(on);
        const wxString tip = ToggleTip(t, on, true);
        if (m_everyoneToggles[t]->GetToolTipText() != tip)
        {
            m_everyoneToggles[t]->SetToolTip(tip);
        }
    }
}

void ControlFrame::Rebuild()
{
    m_lastRevision = m_handler->RosterRevision();
    m_waitingForNames = false;

    Freeze();
    m_sizer->Clear(true);   // destroy the old cards
    m_rows.clear();

    const std::vector<ConnectedGuest>& guests = m_handler->Guests();
    m_title->SetLabel(guests.empty() ? wxString("Guests") : wxString::Format("Guests (%u)", (unsigned)guests.size()));
    m_everyone->Show(guests.size() >= 2);

    if (guests.empty())
    {
        wxStaticText* title = new wxStaticText(m_container, wxID_ANY, "No guests yet");
        title->SetFont(Ui::SemiBold(GetFont()).Scaled(1.1f));
        title->SetForegroundColour(Ui::kText);
        m_sizer->Add(title, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(16));
        WrapLabel* text = new WrapLabel(m_container, "Invite someone from the main window. They show up here when "
                                                    "they join, and this window opens by itself.");
        text->SetForegroundColour(Ui::kTextSecondary);
        m_sizer->AddSpacer(FromDIP(4));
        m_sizer->Add(text, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(16));
    }
    else
    {
        for (size_t i = 0; i < guests.size(); ++i)
        {
            const ConnectedGuest& g = guests[i];
            GuestRow row;
            row.key = g.key;
            row.hasPicture = false;

            Card* card = new Card(m_container);
            wxBoxSizer* line = new wxBoxSizer(wxHORIZONTAL);
            row.avatar = new AvatarCtrl(card, 36);
            line->Add(row.avatar, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(10));

            wxBoxSizer* text = new wxBoxSizer(wxVERTICAL);
            row.name = new wxStaticText(card, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                        wxST_NO_AUTORESIZE | wxST_ELLIPSIZE_END);
            row.name->SetFont(Ui::SemiBold(GetFont()));
            row.name->SetForegroundColour(Ui::kText);
            row.name->SetMinSize(wxSize(FromDIP(60), row.name->GetCharHeight()));
            text->Add(row.name, 0, wxEXPAND);
            row.joined = new wxStaticText(card, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                          wxST_NO_AUTORESIZE | wxST_ELLIPSIZE_END);
            row.joined->SetFont(GetFont().Scaled(0.92f));
            row.joined->SetForegroundColour(Ui::kTextSecondary);
            row.joined->SetMinSize(wxSize(FromDIP(60), row.joined->GetCharHeight()));
            text->Add(row.joined, 0, wxEXPAND | wxTOP, FromDIP(1));
            line->Add(text, 1, wxALIGN_CENTER_VERTICAL);

            for (int t = 0; t < 3; ++t)
            {
                row.toggles[t] = AddToggle(card, line, t, g.key);
            }

            row.remove = new FlatButton(card, "Remove", FlatButton::Danger, Ui::kGlyphClose, true);
            row.remove->SetIconOnly(true);
            const uint64 key = g.key;
            row.remove->Bind(wxEVT_BUTTON, [this, key](wxCommandEvent&)
            {
                // Ask after the click is handled: removing rebuilds the window,
                // which destroys this button.
                CallAfter([this, key]()
                {
                    wxString name = "this guest";
                    const std::vector<ConnectedGuest>& current = m_handler->Guests();
                    for (size_t n = 0; n < current.size(); ++n)
                    {
                        if (current[n].key == key)
                        {
                            bool placeholder = false;
                            name = GuestName(current[n], placeholder);
                        }
                    }
                    int answer = wxMessageBox("Remove " + name + " from the session?",
                                              "Remote Play Control", wxYES_NO | wxICON_QUESTION, this);
                    if (answer == wxYES)
                    {
                        m_handler->RemoveGuest(key);
                        Rebuild();
                    }
                });
            });
            line->Add(row.remove, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(6));

            card->Content()->Add(line, 0, wxEXPAND | wxALL, FromDIP(10));
            m_sizer->Add(card, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(12));
            m_rows.push_back(row);
        }
        m_sizer->AddSpacer(FromDIP(12));
    }

    RefreshRows();
    m_body->Layout();
    m_container->FitInside();
    m_container->Layout();
    Thaw();
}
