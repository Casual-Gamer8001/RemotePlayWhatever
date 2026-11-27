#include <algorithm>
#include <cstring>
#include <ctime>
#include "ControlFrame.h"
#include <wx/display.h>
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
    m_endButton(nullptr),
    m_container(nullptr),
    m_sizer(nullptr),
    m_footer(nullptr),
    m_hint(nullptr),
    m_timer(this),
    m_lastRevision((uint32)-1),
    m_lastGuestCount(0),
    m_pollTicks(0),
    m_waitingForNames(false),
    m_hotkeysActive(false),
    m_place(nullptr),
    m_controllerRevision(0),
    m_controllerTicks(0)
{
    for (int i = 0; i < 3; ++i)
    {
        m_everyoneToggles[i] = nullptr;
    }
    SetIcons(AppIcons());

    m_body = new wxPanel(this, wxID_ANY);
    Ui::SetColours(m_body, &Ui::kWindow);
    wxBoxSizer* bodySizer = new wxBoxSizer(wxVERTICAL);

    // "Guests (2)" and End session.
    wxBoxSizer* header = new wxBoxSizer(wxHORIZONTAL);
    m_title = new wxStaticText(m_body, wxID_ANY, "Guests");
    m_title->SetFont(Ui::SemiBold(GetFont()).Scaled(1.2f));
    Ui::SetColours(m_title, nullptr, &Ui::kText);
    header->Add(m_title, 1, wxALIGN_CENTER_VERTICAL);
    // Only while there's a session to end (OnPoll).
    m_endButton = new FlatButton(m_body, "End session");
    m_endButton->SetToolTip("Disconnect everyone, cancel unused invites and guest links, and stop sharing your screen.");
    m_endButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent&)
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
    header->Add(m_endButton, 0, wxALIGN_CENTER_VERTICAL);
    m_endButton->Show(m_handler->IsSessionActive());
    bodySizer->Add(header, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(14));
    // Keeps the header the same height with or without the button.
    header->SetMinSize(wxSize(-1, m_endButton->GetBestSize().y));

    // A card per guest (and an "Everyone" row above them when there are several).
    m_container = new wxScrolledWindow(m_body, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
    Ui::SetColours(m_container, &Ui::kWindow);
    m_container->SetScrollRate(0, FromDIP(10));
    m_sizer = new wxBoxSizer(wxVERTICAL);
    m_container->SetSizer(m_sizer);
    bodySizer->Add(m_container, 1, wxEXPAND | wxTOP, FromDIP(2));

    // The in-game shortcuts, with the keys drawn as keys.
    m_footer = new wxPanel(m_body, wxID_ANY);
    Ui::SetColours(m_footer, &Ui::kFooter);
    wxBoxSizer* footer = new wxBoxSizer(wxVERTICAL);
    wxWindow* divider = new wxWindow(m_footer, wxID_ANY);
    Ui::SetColours(divider, &Ui::kDivider);
    divider->SetMinSize(wxSize(-1, 1));
    footer->Add(divider, 0, wxEXPAND);
    m_hint = new WrapLabel(m_footer, wxEmptyString, true);
    m_hint->SetFont(GetFont().Scaled(0.92f));
    Ui::SetColours(m_hint, nullptr, &Ui::kTextSecondary);
    footer->Add(m_hint, 0, wxEXPAND | wxALL, FromDIP(10));
    m_footer->SetSizer(footer);
    bodySizer->Add(m_footer, 0, wxEXPAND);

    m_body->SetSizer(bodySizer);
    wxBoxSizer* frameSizer = new wxBoxSizer(wxVERTICAL);
    frameSizer->Add(m_body, 1, wxEXPAND);
    SetSizer(frameSizer);
    SetMinSize(FromDIP(wxSize(470, 260)));
    SetSize(FromDIP(wxSize(560, 400)));
    // Where it was last time and as wide (its height fits the guests).
    m_place = new WindowPlace(this, "GuestControls", false);

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
    // Saves where the window was if that changed in the last second.
    delete m_place;
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
        // (RAW_CONTROL is Ctrl everywhere; plain CONTROL means Command on a Mac.)
        for (const HotKey& k : keys)
        {
            if (CtrlAltKeyIsFree(k.key) && RegisterHotKey(k.id, wxMOD_RAW_CONTROL | wxMOD_ALT, k.key))
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
        // No-break spaces inside an item, so lines only break between items
        // (never "give it" / "back", nor a key away from its words).
        wxString words = items[i].second;
        words.Replace(" ", wxString(L"\u00A0"));
        hint += (i == 0 ? "[Ctrl]+[Alt]+" : "") + items[i].first + wxString(L"\u00A0") + words;
    }
    if (!hint.empty())
    {
        hint = "In the game: " + hint;
    }
    m_hint->SetLabel(hint);
    m_footer->Show(!hint.empty());
    Ui::Relayout(m_footer);
    // The hint can grow to two lines (or shrink), which moves the list: fit again.
    FitHeight();
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

    const bool session = m_handler->IsSessionActive();
    if (m_endButton->IsShown() != session)
    {
        m_endButton->Show(session);
        m_body->Layout();
    }

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
    // The controller order when Steam sends a new list, and every 2 s (a
    // controller can come or go without one).
    if (m_controllerOrder.Revision() != m_controllerRevision || ++m_controllerTicks >= 4)
    {
        m_controllerRevision = m_controllerOrder.Revision();
        m_controllerTicks = 0;
        if (!RefreshControllers(ControllerViews()))
        {
            Rebuild();
        }
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
                // Guests who came in through Steam Link without an account say so in the tooltip.
                row.name->SetToolTip(g.steamID ? name : name + " (no Steam account)");
                row.remove->SetToolTip("Remove " + name + " from the session");
                row.avatar->SetPerson(picture, g.steamID ? name : wxString::Format("G%u", g.player.m_unGuestID), g.key);
                row.hasPicture = picture.IsOk();
            }
        }
        const wxString joined = JoinedText(g.joinedAt);
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
        if (!m_everyoneToggles[t])
        {
            continue;
        }
        const bool on = m_handler->AllGuestsHave(t);
        m_everyoneToggles[t]->SetChecked(on);
        const wxString tip = ToggleTip(t, on, true);
        if (m_everyoneToggles[t]->GetToolTipText() != tip)
        {
            m_everyoneToggles[t]->SetToolTip(tip);
        }
    }
}

wxString ControlFrame::ControllerOwner(const RemotePlayPlayer_t& player) const
{
    // Steam lists the host's own controllers under the host, with guest ID 0.
    if (player.m_unGuestID == 0 && player.m_playerID == GClientContext()->SteamUser()->GetSteamID())
    {
        return "You";
    }
    const uint64 key = RemotePlayInviteHandler::GuestKey(player);
    const std::vector<ConnectedGuest>& guests = m_handler->Guests();
    for (size_t i = 0; i < guests.size(); ++i)
    {
        if (guests[i].key == key)
        {
            bool placeholder = false;
            return GuestName(guests[i], placeholder);
        }
    }
    if (player.m_playerID.GetAccountID() != 0)
    {
        const char* persona = GClientContext()->SteamFriends()->GetFriendPersonaName(player.m_playerID);
        if (persona && *persona && std::strcmp(persona, "[unknown]") != 0)
        {
            return wxString::FromUTF8(persona);
        }
    }
    return wxString::Format("Guest %u", player.m_unGuestID);
}

std::vector<ControlFrame::ControllerView> ControlFrame::ControllerViews() const
{
    std::vector<ControllerView> views;
    if (m_handler->Guests().empty() || !m_controllerOrder.CanMove())
    {
        return views;
    }
    // The list Steam keeps for this session's group (the last one it sent if
    // it hasn't sent one for that group).
    uint32 group = m_handler->GroupID();
    if (!m_controllerOrder.HasList(group))
    {
        group = m_controllerOrder.LastGroup();
    }
    const std::vector<ControllerSlot> listed = m_controllerOrder.Slots(group);
    std::vector<const ControllerSlot*> known(ControllerOrder::kSlots, nullptr);
    std::vector<bool> inUse(ControllerOrder::kSlots, false);
    int last = ControllerOrder::kPlayerSlots - 1;
    for (size_t i = 0; i < listed.size(); ++i)
    {
        known[listed[i].slot] = &listed[i];
        inUse[listed[i].slot] = true;
        last = std::max(last, listed[i].slot);
    }
    // Steam is asked about the slots it didn't list: players 1-4, and the few
    // after them (moving player 4 down puts a controller there), so every
    // controller there has a row even without a list.
    static const int kAskedSlots = ControllerOrder::kPlayerSlots + 4;
    for (int slot = 0; slot < kAskedSlots; ++slot)
    {
        if (!inUse[slot] && m_controllerOrder.SlotInUse(slot))
        {
            inUse[slot] = true;
            last = std::max(last, slot);
        }
    }
    for (int slot = 0; slot <= last; ++slot)
    {
        ControllerView view;
        view.slot = slot;
        view.inUse = inUse[slot];
        if (known[slot])
        {
            view.text = ControllerOwner(known[slot]->player) + Ui::Separator() +
                        wxString::FromUTF8(ControllerOrder::TypeName(known[slot]->type).c_str());
        }
        else
        {
            view.text = view.inUse ? "A controller" : "No controller";
        }
        views.push_back(view);
    }
    return views;
}

void ControlFrame::AddControllerSection()
{
    m_controllerRows.clear();
    m_controllerRevision = m_controllerOrder.Revision();
    m_controllerTicks = 0;
    const std::vector<ControllerView> views = ControllerViews();
    if (views.empty())
    {
        return;
    }

    wxStaticText* title = new wxStaticText(m_container, wxID_ANY, "Controller order");
    title->SetFont(Ui::SemiBold(GetFont()));
    Ui::SetColours(title, nullptr, &Ui::kText);
    m_sizer->Add(title, 0, wxLEFT | wxRIGHT, FromDIP(14));
    m_sizer->AddSpacer(FromDIP(2));
    WrapLabel* about = new WrapLabel(m_container, "Games give out players in this order (many only use players 1 to 4). "
                                                  "Move a controller up or down to change who's who. Identify "
                                                  "rumbles a controller, so its owner knows which it is. Plugged in "
                                                  "another controller? Click the game's window and it shows up here.");
    about->SetFont(GetFont().Scaled(0.92f));
    Ui::SetColours(about, nullptr, &Ui::kTextSecondary);
    m_sizer->Add(about, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(14));

    Card* card = new Card(m_container);
    card->Content()->AddSpacer(FromDIP(8));
    for (size_t i = 0; i < views.size(); ++i)
    {
        ControllerRow row;
        row.slot = views[i].slot;
        const int slot = row.slot;
        wxBoxSizer* line = new wxBoxSizer(wxHORIZONTAL);

        wxStaticText* player = new wxStaticText(card, wxID_ANY, wxString::Format("Player %d", slot + 1));
        player->SetFont(Ui::SemiBold(GetFont()));
        Ui::SetColours(player, nullptr, &Ui::kText);
        player->SetMinSize(wxSize(std::max(FromDIP(64), player->GetBestSize().x), -1));
        line->Add(player, 0, wxALIGN_CENTER_VERTICAL);

        row.text = new wxStaticText(card, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                    wxST_NO_AUTORESIZE | wxST_ELLIPSIZE_END);
        row.text->SetMinSize(wxSize(FromDIP(60), row.text->GetCharHeight()));
        line->Add(row.text, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));

        row.identify = new FlatButton(card, "Identify", FlatButton::Secondary, Ui::kGlyphController, true);
        row.identify->SetFont(GetFont().Scaled(0.92f));
        row.identify->SetToolTip(wxString::Format("Rumble player %d's controller, so its owner knows which it is.", slot + 1));
        row.identify->Bind(wxEVT_BUTTON, [this, slot](wxCommandEvent&) { m_controllerOrder.Identify(slot); });
        line->Add(row.identify, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(6));

        row.up = new FlatButton(card, "Move up", FlatButton::Secondary, Ui::kGlyphUp, true);
        row.up->SetIconOnly(true);
        row.up->Bind(wxEVT_BUTTON, [this, slot](wxCommandEvent&) { MoveController(slot, slot - 1); });
        line->Add(row.up, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(6));

        row.down = new FlatButton(card, "Move down", FlatButton::Secondary, Ui::kGlyphDown, true);
        row.down->SetIconOnly(true);
        row.down->Bind(wxEVT_BUTTON, [this, slot](wxCommandEvent&) { MoveController(slot, slot + 1); });
        line->Add(row.down, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(4));

        card->Content()->Add(line, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(10));
        card->Content()->AddSpacer(FromDIP(i + 1 < views.size() ? 6 : 8));
        m_controllerRows.push_back(row);
    }
    m_sizer->Add(card, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(12));
    m_sizer->AddSpacer(FromDIP(12));
    RefreshControllers(views);
}

bool ControlFrame::RefreshControllers(const std::vector<ControllerView>& views)
{
    if (views.size() != m_controllerRows.size())
    {
        return false;
    }
    for (size_t i = 0; i < views.size(); ++i)
    {
        if (views[i].slot != m_controllerRows[i].slot)
        {
            return false;
        }
    }
    for (size_t i = 0; i < views.size(); ++i)
    {
        const ControllerView& view = views[i];
        ControllerRow& row = m_controllerRows[i];
        if (row.text->GetLabelText() != view.text)
        {
            row.text->SetLabelText(view.text);
            row.text->SetToolTip(view.text);
        }
        const wxColour& colour = view.inUse ? Ui::kText : Ui::kTextMuted;
        if (row.text->GetForegroundColour() != colour)
        {
            Ui::SetColours(row.text, nullptr, view.inUse ? &Ui::kText : &Ui::kTextMuted);
            row.text->Refresh();
        }
        // A controller moves up or down by swapping with the next slot (which
        // may be empty). Below player 4 it can go down to the first slot
        // games don't use, out of the players' way.
        const bool canUp = view.inUse && view.slot > 0;
        const bool canDown = view.inUse && view.slot + 1 < ControllerOrder::kSlots &&
                             (i + 1 < views.size() || view.slot < ControllerOrder::kPlayerSlots);
        if (row.identify->IsEnabled() != view.inUse)
        {
            row.identify->Enable(view.inUse);
        }
        if (row.up->IsEnabled() != canUp)
        {
            row.up->Enable(canUp);
        }
        if (row.down->IsEnabled() != canDown)
        {
            row.down->Enable(canDown);
        }
        const wxString upTip = wxString::Format("Make this controller player %d", view.slot);
        const wxString downTip = wxString::Format("Make this controller player %d", view.slot + 2);
        if (row.up->GetToolTipText() != upTip)
        {
            row.up->SetToolTip(upTip);
        }
        if (row.down->GetToolTipText() != downTip)
        {
            row.down->SetToolTip(downTip);
        }
    }
    return true;
}

void ControlFrame::MoveController(int slot, int to)
{
    m_controllerOrder.Swap(slot, to);
    // Steam lists the new order a moment later (OnPoll shows it); show what's
    // known now, after the click is handled (a rebuild destroys the button).
    CallAfter([this]()
    {
        m_controllerRevision = m_controllerOrder.Revision();
        if (!RefreshControllers(ControllerViews()))
        {
            Rebuild();
        }
    });
}

void ControlFrame::Rebuild()
{
    m_lastRevision = m_handler->RosterRevision();
    m_waitingForNames = false;

    Freeze();
    m_sizer->Clear(true);   // destroy the old cards
    m_rows.clear();
    m_controllerRows.clear();
    for (int t = 0; t < 3; ++t)
    {
        m_everyoneToggles[t] = nullptr;
    }

    const std::vector<ConnectedGuest>& guests = m_handler->Guests();
    m_title->SetLabel(guests.empty() ? wxString("Guests") : wxString::Format("Guests (%u)", (unsigned)guests.size()));

    if (guests.empty())
    {
        wxStaticText* title = new wxStaticText(m_container, wxID_ANY, "No guests yet");
        title->SetFont(Ui::SemiBold(GetFont()).Scaled(1.1f));
        Ui::SetColours(title, nullptr, &Ui::kText);
        m_sizer->Add(title, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(16));
        WrapLabel* text = new WrapLabel(m_container, "Invite someone from the main window. They show up here when "
                                                    "they join, and this window opens by itself.");
        Ui::SetColours(text, nullptr, &Ui::kTextSecondary);
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
            Ui::SetColours(row.name, nullptr, &Ui::kText);
            row.name->SetMinSize(wxSize(FromDIP(60), row.name->GetCharHeight()));
            text->Add(row.name, 0, wxEXPAND);
            row.joined = new wxStaticText(card, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                          wxST_NO_AUTORESIZE | wxST_ELLIPSIZE_END);
            row.joined->SetFont(GetFont().Scaled(0.92f));
            Ui::SetColours(row.joined, nullptr, &Ui::kTextSecondary);
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

        // With several guests, "Everyone" turns a peripheral on or off for all
        // of them. It's laid out like a guest card (without the border), so its
        // toggles sit right above theirs.
        if (guests.size() >= 2)
        {
            Card* everyone = new Card(m_container, &Ui::kWindow, nullptr);
            wxBoxSizer* line = new wxBoxSizer(wxHORIZONTAL);
            line->AddSpacer(FromDIP(36 + 10));
            wxStaticText* label = new wxStaticText(everyone, wxID_ANY, "Everyone");
            label->SetFont(Ui::SemiBold(GetFont()));
            Ui::SetColours(label, nullptr, &Ui::kTextSecondary);
            line->Add(label, 1, wxALIGN_CENTER_VERTICAL);
            for (int t = 0; t < 3; ++t)
            {
                m_everyoneToggles[t] = AddToggle(everyone, line, t, kEveryone);
            }
            line->AddSpacer(FromDIP(6) + m_rows[0].remove->GetBestSize().x);
            everyone->Content()->AddSpacer(FromDIP(2));
            everyone->Content()->Add(line, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(10));
            everyone->Content()->AddSpacer(FromDIP(2));
            m_sizer->Insert(0, everyone, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(12));
            // First for Tab (and screen readers) too, not just on screen.
            everyone->MoveBeforeInTabOrder(m_rows[0].remove->GetParent());
        }

        AddControllerSection();
    }

    RefreshRows();
    m_body->Layout();
    m_container->FitInside();
    m_container->Layout();
    FitHeight();
    Thaw();
}

void ControlFrame::FitHeight()
{
    // Everything outside the list (header, hint) plus what the cards need,
    // up to three quarters of the screen.
    const int outside = GetClientSize().y - m_container->GetSize().y;
    const int wanted = outside + m_sizer->GetMinSize().y;
    const int index = wxDisplay::GetFromWindow(this);
    const wxDisplay display(index == wxNOT_FOUND ? 0u : (unsigned)index);
    const int most = display.GetClientArea().height * 3 / 4;
    const int height = std::max(FromDIP(200), std::min(wanted, most));
    if (height != GetClientSize().y)
    {
        SetClientSize(GetClientSize().x, height);
        m_body->Layout();
    }
}
