>[!NOTE]
> **About this fork.** This fork works with current Steam clients. **To install on Windows,** download `RemotePlayWhatever-win64.zip` from the [latest release](https://github.com/Casual-Gamer8001/RemotePlayWhatever/releases/latest), unzip it anywhere and run `RemotePlayWhatever.exe` (everything it needs is included). Every push is built by GitHub Actions.
>
> It is upstream RemotePlayWhatever plus:
> - the Steam-update fix from [pull request #289](https://github.com/m4dEngi/RemotePlayWhatever/pull/289) by @okabeeer: Steam's internal functions are looked up by name, so Steam updates don't break it, and invites create a real Remote Play Together group so guests can play, not just watch. **The fix only works in 64-bit builds** (`cmake -B build -A x64`), not the 32-bit build described below.
> - non-Steam games (emulators and other shortcuts) are shown by name, and sharing stops by itself when the game closes, so guests don't keep seeing your desktop
> - End session (disconnect everyone, cancel unused invites and guest links, stop sharing), Copy guest link, and a Remove button for each guest
> - clickable guest links (Steam leaves out `https://`)
> - a separate control row for every guest, including Steam Link guests who join without a Steam account
> - a fresh Remote Play group check before every invite, and a question instead of a silent watch-only fallback when Steam won't create one
> - the game that will be shared, and who's connected, shown at the top of the main window
> - an "Everyone" row and in-game hotkeys: Ctrl+Alt+K / M / C turn keyboard / mouse / controller on or off for everyone, Ctrl+Alt+D takes all control, Ctrl+Alt+G gives it back
> - a friend search box, and one window at a time
> - a tray icon: minimizing hides the window by the clock, its right-click menu has the session buttons, and opening the app again brings the window back
> - short sounds when a guest joins or leaves (turn them off from the tray menu), and what each friend may use (keyboard, mouse, controller) is remembered for the next time they join
> - fixes: Steam calls are checked before they're made (so a Steam update can't make it call the wrong function), clearer startup errors, and the control window no longer steals focus from a fullscreen game or keeps the app running after the main window is closed
>
> The upstream notice below describes the official 0.2.14 release.

>[!CAUTION]
> This application does not function as intended due to breaking changes in the latest Steam Client releases! ( see: https://github.com/m4dEngi/RemotePlayWhatever/issues/285 https://github.com/m4dEngi/RemotePlayWhatever/issues/279 )
> Consider using an [alternative](#alternatives)!


# RemotePlayWhatever
Tiny application that lets you force remote play together any game you have in your steam library including non-steam ones.

<p align="center">
    <img src="assets/rpw_screen.png" alt="RemotePlayWhatever screenshot">
</p>

## How it works
RemotePlayWhatever communicates with your running Steam client and instructs it to initiate a Remote Play Together session for the currently running game launched through Steam. It does this by using undocumented and potentially **unstable** internal Steam client APIs.

>[!WARNING]
> - This tool relies on internal Steam client APIs. Future Steam client updates **may break compatibility** without warning.
> - This project aims to support the latest **stable** version of the Steam client. If you encounter issues after a Steam update, please check for a new version of RemotePlayWhatever or report an issue.
> - **Use at Your Own Risk:** This tool communicates locally with Steam via its standard IPC channel and doesn't modify files. However, it relies on unstable, internal APIs used by the Steam client itself, not the stable APIs meant for games. These internal APIs can change with Steam updates, potentially breaking this tool without warning.

## Getting Started
Download the latest RemotePlayWhatever [Release](https://github.com/m4dEngi/RemotePlayWhatever/releases/latest) for your system:
- Linux: 
    - **Arch**: A third-party [AUR package](https://aur.archlinux.org/packages/remoteplaywhatever) is available ( maintained by *Rojikku* ).
    - **Ubuntu**: Download and install the *.**deb** package.
    - **Any distro**: Download the *.**AppImage** and make it executable by running `chmod +x RemotePlayWhatever-*.AppImage`. \
    *Note*: Some systems might require `fuse2` ( or alternative ) package to be installed for *AppImages* to work.
- Windows:
    - Download and extract the RemotePlayWhatever-*.**zip** somewhere; no installation required.

... or [build the app from source](#building-from-source)

## Usage
1. Ensure the Steam client is running.
2. Launch RemotePlayWhatever.
3. Launch the game you want to play *through the Steam client*.
4. Alt+Tab (or switch) back to the RemotePlayWhatever window.
5. Invite your friend(s):
    - **Direct Invite:** Select a friend from the list. They will receive the invite link via a Steam Chat message.
    - **Guest Link:** Click the button to generate a guest invite link. The link will be automatically copied to your clipboard, ready for you to paste and share manually.
6. Alt+Tab (or switch) back to your game.

## Command line arguments
```
-a, --appid=<num>  	AppID to use for non-steam games (Default is 480)
-i, --invite=<str> 	Send remote play invites to provided comma separated 
                        SteamID64s list ( use 0 for guest link ) 
-g, --guestid=<num>	Guest client id
-c, --cancel=<str> 	Cancel invite for provided SteamID64 and GuestID
```

## Building from source
### Linux ( Debian )
1. Install the dependencies:
    ```bash
    sudo apt update
    sudo apt install build-essential git cmake libwxgtk3.2-dev
    ```
2. Clone the repository:
    ```bash
    git clone --recurse-submodules https://github.com/m4dEngi/RemotePlayWhatever.git
    cd RemotePlayWhatever
    ```
3. Configure and build:
    ```bash
    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build
    ```
    The executable will be placed in the `build/RemotePlayWhatever` directory.
4. **( optional )** Create and install deb package:
    ```bash
    cmake --build build --target package
    sudo dpkg -i build/RemotePlayWhatever-*-Linux.deb
    ```

### Windows
These instructions assume a 32-bit build using Visual Studio. \
All commands should be executed in a `Developer Command Prompt for VS`.

1. Download and install [Visual Studio](https://visualstudio.microsoft.com/) (Community Edition is free) with the "Desktop development with C++" workload installed and "Git For Windows" and "MSVC v142 - VS 2019 C++ x64/x86 build tools" components.
2. Download and install [wxWidgets](https://wxwidgets.org/) ( follow the docs ).
3. Clone the repository:
    ```bash
    git clone --recurse-submodules https://github.com/m4dEngi/RemotePlayWhatever.git && 
    cd RemotePlayWhatever
    ```
4. Configure and build:
    ```bash
    cmake -B build -A Win32
    cmake --build build --config Release
    ```
    If build is successful, the executable will be created in the `build\RemotePlayWhatever\Release` folder.

## Contributing
Contributions are welcome! If you find a bug or have a feature request, please open an issue.

## Acknowledgements
- Original idea and initial testing by @Jondycz

## Alternatives
Steam
- [RemotePlayDetached](https://github.com/smaTc/RemotePlayDetached)
- [cooplauncher](https://github.com/MBaliver/cooplauncher)

Standalone
- [Parsec](https://parsec.app/)

