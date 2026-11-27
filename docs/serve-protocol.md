# RemotePlayWhatever `--serve` protocol (v1)

`RemotePlayWhatever --serve [-a <appid>]` runs the app's Remote Play engine with no windows, controlled through
stdin/stdout. It is meant for frontends such as the Steam Deck Decky plugin, which start it as a plain child
process (not through Steam, so Game Mode never tracks it as a second "game").

- Encoding: UTF-8, one message per line (`\n`). stdout carries only protocol lines (JSON objects); anything else
  the app prints goes to stderr.
- Commands (stdin) are plain words separated by single spaces. Unknown commands get an `error` event.
- Every JSON object has an `"event"` field. SteamID64s and game IDs are JSON **strings** (they don't fit in a
  double). Booleans are JSON booleans.
- stdin reaching EOF behaves like `quit`.

## Startup
After Steam is reached:
`{"event":"ready","protocol":1,"build":91,"steamid":"7656119...","name":"<persona name>"}`

If it can't start, one line and exit code 1:
`{"event":"error","error":"steam-not-running"|"not-signed-in"|"methods-missing","message":"<human sentence>"}`

## Commands
| Command | Effect / reply |
|---|---|
| `status` | `status` event (below). |
| `link` | Creates a guest link for the running game. The result comes later as an `invite` event. If no game is running: `{"event":"error","command":"link","error":"no-game","message":"Start a game through Steam first."}` |
| `invite <steamid64>` | Invites a friend (Steam's own invite in their chat). Result comes as an `invite` event. Same `no-game` error. |
| `cancel <steamid64> <guestid>` | Cancels a waiting invite (`steamid64` is `0` for a guest link). Then a `status` event. |
| `input <key> <kbd> <mouse> <ctrl>` | Sets a connected guest's keyboard/mouse/controller (`0`/`1`). `key` is the guest's `key` from `status`. Then a `status` event. |
| `remove <key>` | Disconnects one guest (and cancels their invite/link). Then a `status` event. |
| `friends` | `{"event":"friends","friends":[{"steamid":"...","name":"...","state":"online","game":"Half-Life 2"}, ...]}`; `state` is one of `online`, `away`, `snooze`, `busy`, `trade`, `play`, `offline`; `game` is `""` when not in a game. Offline friends are included (they may be Invisible). |
| `end` | Ends the session (disconnects everyone, cancels unused invites and links, stops sharing the screen). Then `{"event":"ended"}`. |
| `quit` | Exits **without** ending the session (Steam keeps it). Replies `{"event":"bye"}` first. |

## `status` event
```json
{"event":"status",
 "game":{"id":"<CGameID as string>","name":"RetroArch","steam":true} ,      // or null when no game runs
 "screen":false,                 // true: the whole screen is shared (non-Steam game)
 "session":true,                 // invites out, guests connected, or the screen shared
 "link":"https://s.team/...",    // newest unused guest link, or ""
 "guests":[{"key":"<string>","steamid":"<string, \"0\" for Steam Link guests>","name":"...","keyboard":true,"mouse":true,"controller":true,"joined":1700000000}],
 "waiting":[{"steamid":"<string, \"0\" for a guest link>","guest":1,"name":"<friend name or \"\">","link":"<url or \"\">"}]}
```

## Unsolicited events
- `{"event":"invite","steamid":"<string>","guest":<n>,"result":"ok","url":"https://s.team/..."}` (a friend's invite for a game Steam doesn't run is a guest link the app sends them in Steam chat: its `steamid` is the friend's, with a non-zero `guest`)
- `{"event":"invite","steamid":"<string>","guest":<n>,"result":"failed","code":<ERemoteClientLaunchResult>,"message":"<why>"}`
- `{"event":"joined","key":"...","steamid":"...","name":"..."}` / `{"event":"left","key":"...","steamid":"...","name":"..."}`
- `{"event":"notice","message":"..."}` (e.g. "The game closed, so the session ended.")
- `{"event":"group-failed","message":"..."}` (Steam made no Remote Play group; the invite is sent anyway, guests may only watch)
