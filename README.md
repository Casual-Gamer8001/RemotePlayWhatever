# Remote Play Whatever

Start Steam **Remote Play Together** with any game: Steam games that don't support it, and non-Steam games such as emulators that you've added to Steam. Invite Steam friends, or send a **guest link** that anyone can use, even without a Steam account.

This is Casual-Gamer8001's fork of [m4dEngi's RemotePlayWhatever](https://github.com/m4dEngi/RemotePlayWhatever). It works with current Steam clients, and it adds a redesigned window, guest controls, QR codes for guest links, automatic updates on Windows, and a Linux / Steam Deck and macOS version. See [What this fork adds](#what-this-fork-adds).

<p align="center">
    <img src="assets/rpw_screen.png" alt="Remote Play Whatever, ready to share RetroArch" width="480">
</p>

## Download

Get the newest version from the [latest release](https://github.com/Casual-Gamer8001/RemotePlayWhatever/releases/latest). Older builds stay on the [releases page](https://github.com/Casual-Gamer8001/RemotePlayWhatever/releases).

| System | File | Notes |
|---|---|---|
| Windows 10 or 11 (64-bit) | `RemotePlayWhatever-win64.zip` | Everything it needs is included. Updates itself. |
| Steam Deck and other Linux PCs (x86-64) | `RemotePlayWhatever-linux-x86_64.AppImage` | One file. Use it in Desktop Mode on the Steam Deck. |
| macOS 11 or later (Intel or Apple Silicon) | `RemotePlayWhatever-macos.zip` | Experimental: not yet tested with a real Mac. |

### Windows
1. Download `RemotePlayWhatever-win64.zip` and unzip it into a folder you'll keep, for example `Documents\Remote Play Whatever`. There's no installer and it doesn't need admin rights.
2. Run `RemotePlayWhatever.exe`. The app isn't code-signed, so Windows SmartScreen may say it's from an unknown publisher: click **More info**, then **Run anyway**.
3. From then on it keeps itself up to date (see [Updates](#updates)).

### Steam Deck
1. Switch to Desktop Mode: press the Steam button, then **Power > Switch to Desktop**.
2. Download `RemotePlayWhatever-linux-x86_64.AppImage`, for example into a new `Applications` folder in your home folder.
3. Right-click the file, choose **Properties > Permissions** and tick **Is executable** (or run `chmod +x RemotePlayWhatever-linux-x86_64.AppImage` in Konsole).
4. Make sure Steam is running and signed in (it is in Desktop Mode), then double-click the AppImage.

Start the game from Steam in Desktop Mode too. Some people add the AppImage to Steam as a non-Steam game and use it from Game Mode, but closing it there can freeze Steam until the Deck restarts ([upstream issue #69](https://github.com/m4dEngi/RemotePlayWhatever/issues/69)), so Desktop Mode is the safe choice.

### Other Linux PCs
Use the same AppImage: mark it executable and run it while the Steam client is running. It needs a 64-bit PC with glibc 2.35 or newer (Ubuntu 22.04, Debian 12, Fedora 36, Arch and newer) and a desktop with X11 or XWayland. It works with the usual Steam installs (in `~/.steam` or `~/.local/share/Steam`). The Flatpak version of Steam keeps its files inside its sandbox, so it probably won't work with that.

### macOS
1. Download `RemotePlayWhatever-macos.zip`, open it, and move **Remote Play Whatever** to your Applications folder.
2. The app isn't signed by Apple, so macOS blocks it the first time. On macOS 15 or later, try to open it, then go to **System Settings > Privacy & Security** and click **Open Anyway**. On older versions, right-click the app, choose **Open**, then **Open** again.
3. On a Mac with Apple Silicon (M1 or later) it runs through Rosetta 2; macOS offers to install Rosetta the first time if it's missing.
4. Start Steam before you open it.

The macOS version is new and hasn't been tested with a real Mac yet. Please [open an issue](https://github.com/Casual-Gamer8001/RemotePlayWhatever/issues) to say whether it works for you.

## How to use
1. Start Steam and sign in.
2. Open Remote Play Whatever.
3. Start your game **through Steam**: from your Steam library, or with **Play** next to it under *Recent games* in Remote Play Whatever. A non-Steam game, such as an emulator, has to be added to Steam first (**Games > Add a Non-Steam Game to My Library**) and started from Steam.
4. Invite someone:
    - **Invite** next to a friend sends them an invite in Steam chat.
    - **Create guest link** copies a link you can paste anywhere. Anyone can join with it, even without a Steam account: they open it and play through the free Steam Link app on a phone, tablet, computer or TV. Each link lets one person in, so **New guest link** makes another one.
    - **QR code**, next to a waiting link, shows the link as a QR code for someone in the room to scan with their phone.
5. Switch back to your game and play. To choose who can use the keyboard, mouse or a controller, open the guest controls window (**Session > Show guest controls**) or use the [in-game shortcuts](#in-game-shortcuts).
6. When you're done, **End session** disconnects everyone and stops sharing. Closing the game does the same.

Only the game's window is shared for a Steam game. For a **non-Steam game**, Steam shares your **whole screen**: Remote Play Whatever asks first (tick **Don't ask again** for a game you trust), and on Windows it keeps its own windows, including the guest link, out of what your guests see.

## What this fork adds
**Works with current Steam**
- The Steam-update fix from [pull request #289](https://github.com/m4dEngi/RemotePlayWhatever/pull/289) by @okabeeer: Steam's internal functions are looked up by name, so Steam updates don't break the app, and invites create a real Remote Play Together group so guests can play, not just watch. This needs a 64-bit build.
- Every Steam call is checked before it's made. If a Steam update ever changes something the app needs, it says so when it starts and, on Windows, looks for a fixed version and offers to install it.
- When Steam won't set up a Remote Play group for a game, the app asks before sending an invite that would only let the guest watch.

**Sharing a game**
- The game you're sharing is shown at the top of the window with its picture from your Steam library, what your guests will see, and the guest link and session buttons.
- With no game running, the games you played lately (Steam games and non-Steam games) are listed with a **Play** button that starts them through Steam.
- Non-Steam games are shown by name, and sharing stops by itself when the game closes, so guests don't keep seeing your screen.
- Guest links are clickable, can be shown as a QR code, and invites nobody has used yet are listed with a **Cancel** button each. **End session** disconnects everyone and cancels every unused invite and link.
- While you're sharing, the window says for how long (for example "Sharing · 2 guests connected · 25 min") and shows "*name* joined." or "*name* left." when guests come and go.
- Short messages in the window (invite sent, link copied, why a session ended) instead of pop-ups.

**Guests**
- A guest controls window with a card for every guest, including Steam Link guests who joined without a Steam account: keyboard, mouse and controller switches, how long they've been there, and a **Remove** button.
- An **Everyone** row and [in-game shortcuts](#in-game-shortcuts) to switch everyone's keyboard, mouse or controller at once, take all control back, and give it back.
- What each friend may use is remembered for the next time they join.
- A short sound and a message when a guest joins or leaves, and on Windows a notification by the clock while you're in the game (turn them off under **Options**).

**Friends list**
- A search box that finds friends by name or by what they're playing.
- What each friend is doing: "Playing *game*", Online, Away or Busy.
- **Pinned** friends at the top (right-click a friend, then **Pin to the top**), then **Played with recently** (up to 8 friends who joined your sessions, most recent first), then everyone online.
- Friends who appear offline are listed too, in a group you can fold away, because they may just be invisible: an invite still reaches them in Steam chat.
- An **Invite** button for every friend, so clicking a friend's row doesn't send an invite by accident.

**The window**
- Light or dark to match your system (it switches when the system does), or always light or dark (**Options > Theme**).
- A menu bar: **File**, **Session**, **Options** and **Help**.
- Two columns when the window is wide, sharp text on high-DPI screens, and the window opens where you left it.
- Only one copy runs at a time: opening it again brings the open window back.
- A tray icon (on macOS, in the menu bar at the top of the screen). Minimizing hides the window there, its menu has the session buttons and options, and a green dot means a session is on. On Windows, **Start with Windows** opens the app in the tray when you sign in; it waits for Steam to start.

### Updates
On Windows the app keeps itself up to date from this fork's releases:
- It checks when it starts and every 6 hours. An update found when it starts is installed straight away: the app restarts by itself a few seconds later, never during a Remote Play session. **Options > Install updates on launch** turns that off; then an **Update** button with a line about what's new appears instead.
- **Help > Check for updates** checks right away, and **Options > Check for updates automatically** turns the checks off.
- Updates are downloaded and checked (SHA-256) before anything is replaced, and if a new version doesn't start properly, the next start goes back to the version you had and says so.

On Linux, the Steam Deck and macOS, download the new version from the releases page when you want to update.

## In-game shortcuts
These work even while the game is in front: Ctrl+Alt+L while a game is running, the others while guests are connected. On a Mac they're Control+Option+key instead (not tested yet). They aren't available on Linux or the Steam Deck.

| Shortcut | What it does |
|---|---|
| Ctrl+Alt+L | Copy a guest link (it creates one if needed) |
| Ctrl+Alt+K | Keyboard on or off for every guest |
| Ctrl+Alt+M | Mouse on or off for every guest |
| Ctrl+Alt+C | Controller on or off for every guest |
| Ctrl+Alt+D | Take all control from every guest |
| Ctrl+Alt+G | Give control back, as it was before Ctrl+Alt+D |

A shortcut is left out if your keyboard layout uses that key combination to type a character. The guest controls window lists the ones that are on.

## Command line
```
-a, --appid=<number>   Treat this Steam AppID as a non-Steam game (share the whole screen);
                       also the AppID used for non-Steam games' sessions (default 0)
-i, --invite=<ids>     Send invites to these comma-separated SteamID64s and quit
                       (use 0 to create a guest link)
-c, --cancel=<id>      Cancel the invite sent to this SteamID64 and quit (with --guestid)
-g, --guestid=<number> The guest id of the invite to cancel
    --minimized        Start hidden in the tray, waiting for Steam if it isn't running yet
                       (what Start with Windows uses)
-v, --verbose          Verbose output
-h, --help             Show these options
```

## Troubleshooting
- **"Steam isn't running" or "nobody is signed in"**: start Steam, sign in, then open the app again.
- **"Start a game through Steam first"**: the game has to be started from Steam, or with **Play** in the app, so that Steam knows it's running.
- **Your guest can only watch**: Steam didn't set up a Remote Play group for the game. Quit the game, start it again through Steam, and send a new invite.
- **Your guest sees your whole screen**: that's how Steam shares a non-Steam game. Steam games share only their own window.
- **A Steam update broke the app**: the app says so when it starts and, on Windows, offers a fixed version as soon as there is one. Otherwise check the [releases page](https://github.com/Casual-Gamer8001/RemotePlayWhatever/releases) or [open an issue](https://github.com/Casual-Gamer8001/RemotePlayWhatever/issues).
- **Sharing doesn't stop after a session ends**: restart Steam.

## How it works
Remote Play Whatever talks to your running Steam client over Steam's own local connection and asks it to start a Remote Play Together session for the game that's running. It doesn't change any files. It uses Steam's internal, undocumented functions, the same ones the Steam client uses itself.

> [!WARNING]
> - Because these internal functions aren't meant for other programs, a Steam update **can break the app** without warning. This fork looks them up by name to survive most updates, but nothing can promise that.
> - The project aims to support the latest **stable** Steam client, not the beta.
> - Use it at your own risk.

## Building from source
Clone the repository with its submodule:
```bash
git clone --recurse-submodules https://github.com/Casual-Gamer8001/RemotePlayWhatever.git
cd RemotePlayWhatever
```
Every push is built by [GitHub Actions](.github/workflows/build.yml), which is the most up-to-date recipe for each system.

### Windows
You need [Visual Studio 2022](https://visualstudio.microsoft.com/) with the **Desktop development with C++** workload, [CMake](https://cmake.org/) and [wxWidgets](https://www.wxwidgets.org/) 3.2 (64-bit). The simplest way to get wxWidgets is the prebuilt files from its [releases](https://github.com/wxWidgets/wxWidgets/releases): the headers, the `vc14x_x64_Dev` package and the `vc14x_x64_ReleaseDLL` package, unpacked into one folder. Then, in a **Developer Command Prompt for VS 2022**:
```bat
cmake -B build -A x64 -DwxWidgets_ROOT_DIR=C:\wx -DwxWidgets_LIB_DIR=C:\wx\lib\vc14x_x64_dll -DwxWidgets_CONFIGURATION=mswu
cmake --build build --config Release
```
The program is built in `build\RemotePlayWhatever\Release`; copy the wxWidgets DLLs it needs next to it. Build it 64-bit (`-A x64`): the Steam-update fix doesn't work in 32-bit builds.

### Linux
On Debian or Ubuntu:
```bash
sudo apt install build-essential cmake libwxgtk3.2-dev
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```
The program is `build/RemotePlayWhatever/remoteplaywhatever`. To make a Debian package instead, run `cmake --build build --target package`. The AppImage is made by the `linux` job of the GitHub Actions workflow: wxWidgets built in (static, GTK 3) on Ubuntu 22.04, then [linuxdeploy](https://github.com/linuxdeploy/linuxdeploy) with its GTK plugin.

### macOS
You need the Xcode command line tools (`xcode-select --install`) and [CMake](https://cmake.org/). Build wxWidgets 3.2 for Intel Macs (the app reads Steam's Intel code, so it has to be an Intel app; Apple Silicon Macs run it with Rosetta 2):
```bash
curl -LO https://github.com/wxWidgets/wxWidgets/releases/download/v3.2.12/wxWidgets-3.2.12.tar.bz2
tar xjf wxWidgets-3.2.12.tar.bz2 && mkdir wx-build && cd wx-build
../wxWidgets-3.2.12/configure --prefix="$HOME/wx" --with-osx_cocoa --disable-shared --enable-unicode \
  --with-macosx-version-min=11.0 --enable-macosx_arch=x86_64
make -j"$(sysctl -n hw.ncpu)" && make install && cd ..
```
Then build the app:
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DwxWidgets_CONFIG_EXECUTABLE="$HOME/wx/bin/wx-config" -DCMAKE_OSX_ARCHITECTURES=x86_64
cmake --build build
```
The app is `build/RemotePlayWhatever/RemotePlayWhatever.app`.

## Credits
- [m4dEngi](https://github.com/m4dEngi), who made RemotePlayWhatever, and @Jondycz, who had the original idea and tested it.
- @okabeeer for the Steam-update fix ([pull request #289](https://github.com/m4dEngi/RemotePlayWhatever/pull/289)).
- [Open Steamworks](https://github.com/m4dEngi/open-steamworks) for the Steam client interfaces, and [wxWidgets](https://www.wxwidgets.org/) for the window.
- Project Nayuki's [QR Code generator library](https://www.nayuki.io/page/qr-code-generator-library) (MIT License).

Remote Play Whatever is released under the [MIT License](LICENSE). It isn't made by or affiliated with Valve; Steam and Remote Play Together are trademarks of Valve Corporation.

## Alternatives
Steam
- [RemotePlayDetached](https://github.com/smaTc/RemotePlayDetached)
- [cooplauncher](https://github.com/MBaliver/cooplauncher)

Standalone
- [Parsec](https://parsec.app/)
