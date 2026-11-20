#include "wxSteamStuff.h"

wxImage SteamImageToImage(int imageID)
{
    uint32 imgHeight = 0, imgWidth = 0;
    wxImage wImage;
    if(imageID > 0 && GClientContext()->SteamUtils()->GetImageSize(imageID, &imgWidth, &imgHeight) &&
       imgWidth > 0 && imgHeight > 0)
    {
        size_t bufSize = (size_t)imgHeight * imgWidth;
        uint32_t* buf = new uint32_t[bufSize];

        if(GClientContext()->SteamUtils()->GetImageRGBA(imageID, (uint8_t*)buf, bufSize * 4))
        {
            size_t imgSize = bufSize * 3;
            uint8_t* imBuf = (uint8_t*)malloc(imgSize);
            for(size_t i = 0; i < bufSize; ++i)
            {
                size_t offset = i * 3;
                imBuf[offset]     = ((uint8_t*)&buf[i])[0];
                imBuf[offset + 1] = ((uint8_t*)&buf[i])[1];
                imBuf[offset + 2] = ((uint8_t*)&buf[i])[2];
            }
            wImage.SetData(imBuf, imgWidth, imgHeight);
        }

        delete[] buf;
    }

    return wImage;
}

wxBitmap SteamImageToBitmap(int imageID)
{
    wxImage image = SteamImageToImage(imageID);
    return image.IsOk() ? wxBitmap(image) : wxNullBitmap;
}

wxImage SteamAvatarImage(CSteamID steamID, bool* medium)
{
    ISteamFriends015* friends = GClientContext()->SteamFriends();
    wxImage image = SteamImageToImage(friends->GetMediumFriendAvatar(steamID));
    if (medium)
    {
        *medium = image.IsOk();
    }
    if (!image.IsOk())
    {
        image = SteamImageToImage(friends->GetSmallFriendAvatar(steamID));
    }
    return image;
}

wxSteamID::wxSteamID(CSteamID steamID):
    m_steamID(steamID)
{
}

CSteamID wxSteamID::GetSteamID()
{
    return m_steamID;
}
