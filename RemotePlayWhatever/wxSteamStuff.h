#ifndef WXSTEAMSTUFF_H
#define WXSTEAMSTUFF_H
#include <wx/wx.h>
#include "SteamStuff.h"

class wxSteamID : public wxObject
{
public:
    wxSteamID(CSteamID steamID);
    CSteamID GetSteamID();

private:
    CSteamID m_steamID;
};

wxBitmap SteamImageToBitmap(int imageID);
wxImage SteamImageToImage(int imageID);

// Someone's avatar: the 64x64 one if Steam has it (sharp on high-DPI
// screens), else the 32x32 one. medium (optional) says which it was; Steam
// starts loading a missing one, so asking again later may get the bigger one.
wxImage SteamAvatarImage(CSteamID steamID, bool* medium = nullptr);

#endif // WXSTEAMSTUFF_H
