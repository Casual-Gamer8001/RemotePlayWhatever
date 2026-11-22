# Remote Play Whatever for Decky Loader

A [Decky Loader](https://decky.xyz) plugin that brings [Remote Play Whatever](https://github.com/Casual-Gamer8001/RemotePlayWhatever) to the Steam Deck's Game Mode. It shares the game you're playing over Steam Remote Play Together, from the Quick Access Menu. The game can be a Steam game, a non-Steam game or an emulator.

## Why a plugin

Until now, people added the RemotePlayWhatever AppImage to Steam as a non-Steam game and ran it alongside their game. Game Mode copes badly with two non-Steam games at once: closing Remote Play Whatever could freeze Steam until the Deck restarted (upstream issues [#58](https://github.com/m4dEngi/RemotePlayWhatever/issues/58), [#69](https://github.com/m4dEngi/RemotePlayWhatever/issues/69) and [#132](https://github.com/m4dEngi/RemotePlayWhatever/issues/132)).

The plugin instead runs Remote Play Whatever in the background, as an ordinary program with no window (`RemotePlayWhatever --serve`), not as a game. Steam sees only the game you start from your library, as usual, and you control Remote Play from the Quick Access Menu while you play.

## Install

1. Install [Decky Loader](https://decky.xyz): in Desktop Mode, download its installer, run it, then return to Game Mode.
2. Turn on Decky's developer mode. Press **···** (Quick Access), open Decky (the plug icon) and select the gear. Under **General**, turn on **Developer mode**.
3. In Decky's settings, open **Developer** and do one of these:
   - Under **Install Plugin from URL**, enter `https://github.com/Casual-Gamer8001/RemotePlayWhatever/releases/latest/download/RemotePlayWhatever-decky.zip` and select **Install**.
   - Download that zip (in Desktop Mode, for example) and use **Install Plugin from ZIP File**.
4. Open **Remote Play Whatever** in the Quick Access Menu. If it says Remote Play Whatever isn't installed, select **Download**; if it says it's too old, select **Update**. Either fetches the newest AppImage from this fork's releases.

The plugin isn't in Decky's plugin store. To update it, install it again the same way.

The plugin uses the first AppImage it finds:

1. the one saved in its settings (**Download** saves the AppImage it fetched);
2. the newest `RemotePlayWhatever*.AppImage` in `~/Applications`, then `~/Downloads`, then `~/Desktop`;
3. one in the plugin's own `bin` folder.

The plugin needs a version of Remote Play Whatever with Game Mode support, released along with the plugin. If you already have it in one of those folders, there's nothing to download. If the AppImage it finds is older, the panel says it's too old and offers **Update**. A too-old AppImage is passed over once the plugin has tried it, so an older one in `~/Applications` doesn't hide a newer one in `~/Downloads`. You don't need the AppImage as a non-Steam game in your library anymore. Don't start it from there while the plugin is running it.

## Use

1. Start your game from your Steam library, as usual.
2. Press **···** and open **Remote Play Whatever**. It starts Remote Play Whatever in the background, then shows **Ready to share** and the game's name.
3. Bring people in:
   - **Invite a friend** lists your Steam friends, online ones first. Pick one, and the invite goes to their Steam chat.
   - **Create guest link** makes a link that anyone can use with the free Steam Link app, with no Steam account needed:
     - someone in the room can scan its QR code with their phone;
     - **Copy link** copies it, so you can paste it into a message;
     - a link is good for one guest, and **New guest link** makes another.

   Invites and links that nobody has used yet are listed under **Waiting**, each with **Cancel**.
4. Under **Guests**, each guest has **Keyboard**, **Mouse** and **Controller** switches. These decide which of their inputs reach your game. **Remove** sends that guest away.
5. **End session** disconnects everyone.

You can close the Quick Access Menu and play: the session continues. Pop-up notifications tell you when someone joins or leaves.

## Settings

These are at the bottom of the panel:

- **AppImage** shows the one in use. **Download** (or **Update**) fetches the newest release into `~/homebrew/data/RemotePlayWhatever`. If a session is running, the new version is used the next time Remote Play Whatever starts.
- **Start when the panel opens** is on at first. It starts Remote Play Whatever in the background whenever you open the panel. If you turn it off, start it yourself with **Start**.
- **Stop Remote Play Whatever** stops the background program. This doesn't end a session; **End session** does that.

The settings file is `~/homebrew/settings/RemotePlayWhatever/settings.json`. To try a test build, set `download_url` in it to the test build's address and select **Update**. For example, `https://github.com/Casual-Gamer8001/RemotePlayWhatever/releases/download/branch-test/RemotePlayWhatever-linux-x86_64.AppImage`. Set `download_url` back to `""` to return to the releases.

If Remote Play Whatever keeps stopping, the panel says why. The plugin's log is in `~/homebrew/logs/RemotePlayWhatever/`.

## Limits

- For a non-Steam game, Steam shares your whole screen, not just the game. Guests see everything on it, including the Quick Access Menu and notifications.
- Remote Play Whatever relies on Steam client internals that Valve doesn't document, so a Steam update can break it until a new version catches up. **Update** then fetches the newest AppImage.

## Development

The plugin follows [decky-plugin-template](https://github.com/SteamDeckHomebrew/decky-plugin-template):

- `main.py` is the backend. Decky runs it as the Deck's user, and it uses only the Python standard library. It starts `RemotePlayWhatever --serve`, exchanges JSON lines with it over stdin and stdout, and restarts it if it stops.
- `src/index.tsx` is the panel, built with `@decky/ui` and `@decky/api`.

```sh
npm install
npm run build          # makes dist/index.js
python3 test_main.py   # tests the backend against a fake Remote Play Whatever
```

For each release, GitHub Actions builds `RemotePlayWhatever-decky.zip`. The zip holds a `RemotePlayWhatever` folder with `plugin.json`, `package.json`, `main.py`, `dist/index.js`, `LICENSE` and `README.md`. You can zip such a folder by hand to test a build with **Install Plugin from ZIP File**.
