#ifndef SERVEMODE_H
#define SERVEMODE_H

// `--serve`: the app's Remote Play engine with no windows, driven through
// stdin/stdout (JSON lines), for frontends such as the Steam Deck Decky plugin
// (docs/serve-protocol.md). It starts before wxWidgets' GUI does, so it also
// runs where there's no display. appID: -a/--appid, as for the window.
int RunServeMode(int argc, char** argv, unsigned appID);

#endif // SERVEMODE_H
