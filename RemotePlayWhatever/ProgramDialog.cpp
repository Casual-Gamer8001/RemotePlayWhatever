#include <wx/wx.h>
#include <wx/dcbuffer.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/graphics.h>
#include <wx/stdpaths.h>
#include <memory>
// Windows headers come first: Steamworks.h (via ProgramLauncher.h) includes
// windows.h itself, and wx's copy must come before it.
#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>
#endif
#include "UiWidgets.h"
#include "ProgramDialog.h"

namespace
{
    // How wide the explanation is (DIP); the fields are as wide.
    const int kTextWidth = 400;

    // A text box drawn like the friends list's search box: rounded, with the
    // accent colour around it while it has the focus.
    class TextField : public wxPanel
    {
    public:
        TextField(wxWindow* parent, const wxString& value, const wxString& hint) :
            wxPanel(parent, wxID_ANY),
            m_text(nullptr),
            m_focused(false)
        {
            SetBackgroundStyle(wxBG_STYLE_PAINT);
            Ui::SetColours(this, &Ui::kCard);
            m_text = new wxTextCtrl(this, wxID_ANY, value, wxDefaultPosition, wxDefaultSize,
                                    wxBORDER_NONE | wxTE_PROCESS_ENTER);
            m_text->SetHint(hint);
            Ui::SetColours(m_text, &Ui::kCard, &Ui::kText);
            wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
            row->Add(m_text, 1, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(7));
            SetSizer(row);

            m_text->Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& event)
            {
                m_focused = true;
                Refresh();
                event.Skip();
            });
            m_text->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& event)
            {
                m_focused = false;
                Refresh();
                event.Skip();
            });
            Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) { m_text->SetFocus(); });
            Bind(wxEVT_PAINT, &TextField::OnPaint, this);
            Bind(wxEVT_SIZE, [this](wxSizeEvent& event)
            {
                Refresh();
                event.Skip();
            });
        }

        wxTextCtrl* Text() const { return m_text; }

    private:
        void OnPaint(wxPaintEvent&)
        {
            wxAutoBufferedPaintDC dc(this);
            dc.SetBackground(wxBrush(GetParent()->GetBackgroundColour()));
            dc.Clear();
            const wxSize size = GetClientSize();
            std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::CreateFromUnknownDC(dc));
            if (gc && size.x > 2 && size.y > 2)
            {
                gc->SetBrush(wxBrush(Ui::kCard));
                gc->SetPen(wxPen(m_focused ? Ui::kAccent : Ui::kBorder));
                gc->DrawRoundedRectangle(0.5, 0.5, size.x - 1.0, size.y - 1.0, FromDIP(5));
            }
        }

        wxTextCtrl* m_text;
        bool m_focused;
    };

    // A path as typed or pasted: without the quotes Explorer's "Copy as path" adds.
    wxString CleanPath(wxString path)
    {
        path.Trim().Trim(false);
        if (path.length() >= 2 && path.StartsWith("\"") && path.EndsWith("\""))
        {
            path = path.Mid(1, path.length() - 2);
        }
        return path;
    }

    // Remote Play Whatever itself (as Steam would start it, on Linux the AppImage).
    bool IsThisApp(const wxString& path)
    {
        const wxFileName picked(path);
        if (picked.SameAs(wxFileName(wxStandardPaths::Get().GetExecutablePath())))
        {
            return true;
        }
        wxString appImage;
        return wxGetEnv("APPIMAGE", &appImage) && !appImage.empty() && picked.SameAs(wxFileName(appImage));
    }

    class ProgramDialog : public wxDialog
    {
    public:
        ProgramDialog(wxWindow* parent, const ProgramLauncher::Program& program, bool editing) :
            wxDialog(parent, wxID_ANY, editing ? wxString("Change a program") : wxString("Play a program that isn't in Steam")),
            m_program(program),
            m_path(nullptr),
            m_name(nullptr),
            m_options(nullptr)
        {
            Ui::SetColours(this, &Ui::kWindow, &Ui::kText);
            const int pad = FromDIP(20);
            wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

            wxStaticText* title = new wxStaticText(this, wxID_ANY, wxEmptyString);
            title->SetLabelText(editing ? "Change " + program.name : wxString("Play a program that isn't in Steam"));
            title->SetFont(Ui::SemiBold(GetFont()).Scaled(1.2f));
            Ui::SetColours(title, nullptr, &Ui::kText);
            sizer->Add(title, 0, wxLEFT | wxRIGHT | wxTOP, pad);
            sizer->AddSpacer(FromDIP(6));

            wxStaticText* text = new wxStaticText(this, wxID_ANY,
                "Steam starts it like a non-Steam game, without adding it to your library, so your guests hear it "
                "and see its window while it's in front. A game that starts through another launcher may still "
                "need Share my screen.");
            Ui::SetColours(text, nullptr, &Ui::kTextSecondary);
            text->Wrap(FromDIP(kTextWidth));
            sizer->Add(text, 0, wxLEFT | wxRIGHT, pad);
            sizer->AddSpacer(FromDIP(16));

            AddLabel(sizer, "Program");
            wxBoxSizer* pathRow = new wxBoxSizer(wxHORIZONTAL);
            m_path = new TextField(this, program.path, "The game's or emulator's program file");
            pathRow->Add(m_path, 1, wxEXPAND);
            FlatButton* browse = new FlatButton(this, "Browse...");
            browse->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Browse(); });
            pathRow->Add(browse, 0, wxEXPAND | wxLEFT, FromDIP(8));
            sizer->Add(pathRow, 0, wxEXPAND | wxLEFT | wxRIGHT, pad);
            sizer->AddSpacer(FromDIP(12));

            AddLabel(sizer, "Name");
            m_name = new TextField(this, program.name, "How it's shown in Remote Play Whatever");
            sizer->Add(m_name, 0, wxEXPAND | wxLEFT | wxRIGHT, pad);
            sizer->AddSpacer(FromDIP(12));

            AddLabel(sizer, "Launch options (optional)");
            m_options = new TextField(this, program.options, "For example, a game file for an emulator");
            sizer->Add(m_options, 0, wxEXPAND | wxLEFT | wxRIGHT, pad);

            wxBoxSizer* buttons = new wxBoxSizer(wxHORIZONTAL);
            buttons->AddStretchSpacer();
            FlatButton* ok = new FlatButton(this, editing ? "Save" : "Play", FlatButton::Primary);
            ok->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Finish(); });
            buttons->Add(ok, 0, wxRIGHT, FromDIP(8));
            FlatButton* cancel = new FlatButton(this, "Cancel");
            cancel->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_CANCEL); });
            buttons->Add(cancel, 0);
            sizer->AddSpacer(FromDIP(20));
            sizer->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, pad);

            SetSizerAndFit(sizer);
            CentreOnParent();
            Ui::ThemeNativeParts(this);

            // The name follows the file until it's changed by hand.
            m_autoName = !program.name.empty() && program.name == ProgramLauncher::DefaultName(program.path)
                         ? program.name : wxString();
            m_path->Text()->Bind(wxEVT_TEXT, [this](wxCommandEvent& event)
            {
                FollowPath();
                event.Skip();
            });
            // Enter in any field is Play (or Save); Escape cancels.
            for (TextField* field : { m_path, m_name, m_options })
            {
                field->Text()->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { Finish(); });
            }
            Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& event)
            {
                if (event.GetKeyCode() == WXK_ESCAPE)
                {
                    EndModal(wxID_CANCEL);
                    return;
                }
                event.Skip();
            });
            m_path->Text()->SetFocus();
        }

        ProgramLauncher::Program Result() const { return m_program; }

    private:
        void AddLabel(wxSizer* sizer, const wxString& text)
        {
            wxStaticText* label = new wxStaticText(this, wxID_ANY, text);
            Ui::SetColours(label, nullptr, &Ui::kText);
            sizer->Add(label, 0, wxLEFT | wxRIGHT, FromDIP(20));
            sizer->AddSpacer(FromDIP(4));
        }

        void FollowPath()
        {
            const wxString name = m_name->Text()->GetValue();
            if (!name.empty() && name != m_autoName)
            {
                return;
            }
            const wxString path = CleanPath(m_path->Text()->GetValue());
            m_autoName = path.empty() ? wxString() : ProgramLauncher::DefaultName(path);
            m_name->Text()->ChangeValue(m_autoName);
        }

        void Browse()
        {
            wxString folder, file;
            const wxString current = CleanPath(m_path->Text()->GetValue());
            if (!current.empty())
            {
                const wxFileName name(current);
                folder = name.GetPath();
                file = name.GetFullName();
            }
#ifdef __WXMSW__
            const wxString wildcard = "Programs (*.exe)|*.exe|All files (*.*)|*.*";
#else
            const wxString wildcard = wxFileSelectorDefaultWildcardStr;
#endif
            wxFileDialog dialog(this, "Pick the game's or emulator's program", folder, file, wildcard,
                                wxFD_OPEN | wxFD_FILE_MUST_EXIST);
            if (dialog.ShowModal() == wxID_OK)
            {
                // Its wxEVT_TEXT names the program too (FollowPath).
                m_path->Text()->SetValue(dialog.GetPath());
                m_path->Text()->SetInsertionPointEnd();
            }
        }

        void Problem(const wxString& text, TextField* field)
        {
            wxMessageBox(text, "Remote Play Whatever", wxOK | wxICON_WARNING, this);
            field->Text()->SetFocus();
        }

        void Finish()
        {
            const wxString path = CleanPath(m_path->Text()->GetValue());
            if (path.empty())
            {
                Problem("Pick the program's file first: click Browse.", m_path);
                return;
            }
            if (!ProgramLauncher::ProgramExists(path))
            {
                Problem("Remote Play Whatever can't find that file. Pick it with Browse.", m_path);
                return;
            }
            if (IsThisApp(path))
            {
                Problem("That's Remote Play Whatever itself. Pick your game's or emulator's program.", m_path);
                return;
            }
            wxString name = m_name->Text()->GetValue();
            name.Trim().Trim(false);
            wxString options = m_options->Text()->GetValue();
            options.Trim().Trim(false);
            m_program.path = path;
            m_program.name = name.empty() ? ProgramLauncher::DefaultName(path) : name;
            m_program.options = options;
            EndModal(wxID_OK);
        }

        ProgramLauncher::Program m_program;
        TextField* m_path;
        TextField* m_name;
        TextField* m_options;
        wxString m_autoName;    // the name taken from the file, while it's unchanged
    };
}

bool AskForProgram(wxWindow* parent, ProgramLauncher::Program& program, bool editing)
{
    ProgramDialog dialog(parent, program, editing);
    if (dialog.ShowModal() != wxID_OK)
    {
        return false;
    }
    program = dialog.Result();
    return true;
}
