#!/usr/bin/env python3
"""Tests for main.py, the plugin's backend, against a fake RemotePlayWhatever.

    python decky/test_main.py

Standard library only. FAKE_RPW below speaks the --serve protocol and logs what
it's sent, and OLD_RPW answers --serve like a build from before it; scratch files
go in decky/.test-work/, which is deleted afterwards (set RPW_KEEP_TEST_WORK=1 to
keep it, RPW_TEST_VERBOSE=1 to see the backend's log).
"""

from __future__ import annotations

import asyncio
import functools
import http.server
import io
import json
import logging
import os
import shutil
import sys
import threading
import time
import types
import unittest
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
WORK = os.path.join(HERE, ".test-work")

ME = "76561198000000001"
ALICE = "76561198000000002"
BOB = "76561198000000003"

# The download tests talk to 127.0.0.1, which mustn't go through a proxy.
for _name in ("http_proxy", "https_proxy", "all_proxy", "HTTP_PROXY", "HTTPS_PROXY", "ALL_PROXY"):
    os.environ.pop(_name, None)
os.environ["no_proxy"] = "127.0.0.1,localhost"

# A stand-in for the decky module that Decky Loader gives its plugins. The
# paths are set for each test, in Case.asyncSetUp.
emitted: list = []


async def _emit(event, *args):
    emitted.append((event, args))


decky = types.ModuleType("decky")
decky.logger = logging.getLogger("decky-test")
decky.emit = _emit
decky.DECKY_USER = "deck"
decky.DECKY_PLUGIN_NAME = "Remote Play Whatever"
decky.DECKY_PLUGIN_VERSION = "0.1.0-test"
sys.modules["decky"] = decky

if os.environ.get("RPW_TEST_VERBOSE"):
    logging.basicConfig(level=logging.DEBUG)
else:
    decky.logger.addHandler(logging.NullHandler())
    decky.logger.propagate = False

sys.path.insert(0, HERE)
import main  # noqa: E402

FAKE_RPW = r'''
"""A fake RemotePlayWhatever --serve, for test_main.py."""
import json
import os
import sys
import time

LOG = os.environ.get("FAKE_RPW_LOG", "")
MODE = os.environ.get("FAKE_RPW_MODE", "")
ALICE, BOB = "76561198000000002", "76561198000000003"
FRIENDS = [
    {"steamid": "76561198000000004", "name": "Carol", "state": "offline", "game": ""},
    {"steamid": BOB, "name": "Bob", "state": "play", "game": "Celeste"},
    {"steamid": "76561198000000005", "name": "dave", "state": "away", "game": ""},
    {"steamid": ALICE, "name": "Alice", "state": "online", "game": ""},
]


def log(line):
    if LOG:
        with open(LOG, "a", encoding="utf-8") as f:
            f.write(line + "\n")


def out(**event):
    sys.stdout.write(json.dumps(event) + "\n")
    sys.stdout.flush()


runs = 0
if LOG and os.path.exists(LOG):
    with open(LOG, encoding="utf-8") as f:
        runs = sum(1 for line in f if line.startswith("argv "))
log("argv " + json.dumps(sys.argv[1:]))
log("env " + json.dumps({key: os.environ.get(key) for key in (
    "HOME", "USER", "LOGNAME", "XDG_RUNTIME_DIR", "DBUS_SESSION_BUS_ADDRESS",
    "APPIMAGE_EXTRACT_AND_RUN", "DISPLAY", "LANG", "LD_LIBRARY_PATH")}))

if sys.argv[1:] != ["--serve"]:
    sys.stderr.write("usage: fake --serve\n")
    sys.exit(2)
if MODE.startswith("fail:"):
    code = MODE[len("fail:"):]
    sys.stderr.write("fake: can't start\n")
    out(event="error", error=code, message={
        "steam-not-running": "Steam isn't running.",
        "methods-missing": "This Steam lacks what Remote Play Whatever needs.",
    }.get(code, code))
    sys.exit(1)
if MODE == "crash":
    sys.stderr.write("Segmentation fault (core dumped)\n")
    sys.stderr.flush()
    sys.exit(139)

print("this line isn't JSON", flush=True)
out(note="nor is this an event")
out(event="ready", protocol=1, build=91, steamid="76561198000000001", name="Deck")

if MODE == "die-once" and runs == 0:
    time.sleep(0.2)
    sys.stderr.write("fake: lost the connection to Steam\n")
    sys.stderr.flush()
    sys.exit(3)

game = None if MODE == "no-game" else {"id": "9876543210", "name": "RetroArch", "steam": False}
session = False
guests = []
waiting = []
made = 0


def status():
    links = [w["link"] for w in waiting if w["steamid"] == "0"]
    out(event="status", game=game, screen=bool(session and game and not game["steam"]),
        session=session, link=links[-1] if links else "", guests=guests, waiting=waiting)


def no_game(command):
    out(event="error", command=command, error="no-game", message="Start a game through Steam first.")


while True:
    line = sys.stdin.readline()
    if not line:  # EOF is like quit
        out(event="bye")
        break
    line = line.strip()
    if not line:
        continue
    log("cmd " + line)
    word, *args = line.split()
    if word == "status":
        status()
    elif word in ("link", "invite"):
        if game is None:
            no_game(word)
            continue
        sid = args[0] if word == "invite" else "0"
        if sid == "666":
            out(event="invite", steamid=sid, result="failed", code=2, message="Steam refused the invite.")
            continue
        made += 1
        url = "https://s.team/p/FAKE-%d" % made
        session = True
        out(event="invite", steamid=sid, guest=made, result="ok", url=url)
        if sid == ALICE:  # she accepts at once
            guests.append({"key": "g-alice", "steamid": sid, "name": "Alice", "keyboard": False,
                           "mouse": False, "controller": True, "joined": int(time.time()) - 125})
            out(event="joined", key="g-alice", steamid=sid, name="Alice")
            continue
        name = next((f["name"] for f in FRIENDS if f["steamid"] == sid), "")
        waiting.append({"steamid": sid, "guest": made, "name": name, "link": url})
        if sid == BOB:
            out(event="group-failed", message="Steam made no Remote Play group.")
            out(event="notice", message="Bob's invite is on its way.")
    elif word == "cancel":
        waiting[:] = [w for w in waiting if (w["steamid"], str(w["guest"])) != (args[0], args[1])]
        status()
    elif word == "input":
        guest = next((g for g in guests if g["key"] == args[0]), None)
        if guest is None:
            out(event="error", command="input", error="no-guest", message="There's no such guest.")
            continue
        guest.update(keyboard=args[1] == "1", mouse=args[2] == "1", controller=args[3] == "1")
        status()
    elif word == "remove":
        gone = [g for g in guests if g["key"] == args[0]]
        guests[:] = [g for g in guests if g["key"] != args[0]]
        status()
        for g in gone:
            out(event="left", key=g["key"], steamid=g["steamid"], name=g["name"])
    elif word == "friends":
        out(event="friends", friends=FRIENDS)
    elif word == "end":
        session = False
        guests[:] = []
        waiting[:] = []
        out(event="ended")
    elif word == "quit":
        if MODE == "hang-on-quit":
            time.sleep(60)
        out(event="bye")
        break
    else:
        out(event="error", command=word, error="unknown-command", message="Unknown command: " + word)
'''

# How a build from before --serve answers it: wxWidgets' command-line parser
# rejects the option and prints the usage text, on stderr, and the app exits
# (code 255) without a word on stdout.
OLD_STDERR = "\n".join([
    "Unknown long option 'serve'",
    "Usage: RemotePlayWhatever [-h] [-v] [-a <num>] [-i <str>] [-g <num>] [-c <str>] [--minimized]",
    "  -h, --help         \tshow this help message",
    "  -v, --verbose      \tverbose output",
    "  -a, --appid=<num>  \tAppID for non-steam games",
    "  -i, --invite=<str> \tSend remote play invites to provided SteamID64s ( use 0 for guest link )",
    "  -g, --guestid=<num>\tGuest client id",
    "  -c, --cancel=<str> \tCancel invite for provided SteamID64 and GuestID",
    "  --minimized        \tstart hidden in the tray, waiting for Steam if it isn't running yet"
    " (how it starts with Windows)",
]) + "\n"

OLD_RPW = r'''
"""A fake RemotePlayWhatever from before --serve, for test_main.py."""
import json
import os
import sys

LOG = os.environ.get("FAKE_RPW_LOG", "")
if LOG:
    with open(LOG, "a", encoding="utf-8") as f:
        f.write("old " + json.dumps(sys.argv) + "\n")
sys.stderr.write(OLD_STDERR)
sys.exit(255)
'''.replace("OLD_STDERR", repr(OLD_STDERR))


def appimage_of(script):
    """A download that passes for an AppImage (it starts like an ELF file) and that Python runs: a zip app."""
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w") as z:
        z.writestr("__main__.py", script)
    return b"\x7fELF" + buffer.getvalue()


class _QuietHandler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass


class Case(unittest.IsolatedAsyncioTestCase):
    """Each test gets fresh folders, a fresh backend and the fake app."""

    async def asyncSetUp(self):
        self.dir = os.path.join(WORK, self.id().rsplit(".", 1)[-1])
        shutil.rmtree(self.dir, ignore_errors=True)
        for sub in ("home", "settings", "data", "plugin", "logs"):
            os.makedirs(os.path.join(self.dir, sub))
        self.home = os.path.join(self.dir, "home")
        decky.DECKY_USER_HOME = self.home
        decky.DECKY_HOME = os.path.join(self.home, "homebrew")
        decky.DECKY_PLUGIN_SETTINGS_DIR = os.path.join(self.dir, "settings")
        decky.DECKY_PLUGIN_RUNTIME_DIR = os.path.join(self.dir, "data")
        decky.DECKY_PLUGIN_DIR = os.path.join(self.dir, "plugin")
        decky.DECKY_PLUGIN_LOG_DIR = os.path.join(self.dir, "logs")
        self.fake = os.path.join(self.dir, "fake_rpw.py")
        with open(self.fake, "w", encoding="utf-8", newline="\n") as f:
            f.write(FAKE_RPW)
        self.log = os.path.join(self.dir, "fake.log")
        emitted.clear()
        self.plugin = self.make_plugin()

    async def asyncTearDown(self):
        await self.plugin._unload()

    def make_plugin(self, mode=""):
        plugin = main.Plugin()
        plugin.binary_override = self.fake
        plugin.launcher = (sys.executable, "-u")
        env = {"FAKE_RPW_LOG": self.log, "FAKE_RPW_MODE": mode}
        for key in ("SYSTEMROOT", "SYSTEMDRIVE", "TEMP", "TMP"):  # what Python needs on Windows
            if os.name == "nt" and os.environ.get(key):
                env[key] = os.environ[key]
        plugin.extra_env = env
        plugin.restart_delays = ()
        plugin.command_timeout = 10.0
        plugin.ready_timeout = 20.0
        return plugin

    def mode(self, mode):
        self.plugin.extra_env["FAKE_RPW_MODE"] = mode

    def _log_lines(self):
        if not os.path.exists(self.log):
            return []
        with open(self.log, encoding="utf-8") as f:
            return f.read().splitlines()

    def commands(self):
        return [line[4:] for line in self._log_lines() if line.startswith("cmd ")]

    def runs(self):
        return sum(1 for line in self._log_lines() if line.startswith("argv "))

    def old_runs(self, path=None):
        """How often a fake from before --serve ran (the one at path, if given)."""
        argvs = [json.loads(line[4:]) for line in self._log_lines() if line.startswith("old ")]
        same = os.path.normcase
        return sum(1 for argv in argvs if path is None or same(argv[0]) == same(path))

    def put_script(self, folder, name, script):
        """An AppImage stand-in: the tests' launcher runs it with Python."""
        os.makedirs(folder, exist_ok=True)
        path = os.path.join(folder, name)
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(script)
        return path

    def first_run(self):
        lines = self._log_lines()
        argv = next(json.loads(line[5:]) for line in lines if line.startswith("argv "))
        env = next(json.loads(line[4:]) for line in lines if line.startswith("env "))
        return argv, env

    def toasts(self):
        return [args[0]["title"] for event, args in emitted if event == "rpw_toast"]

    def toast_bodies(self):
        return [args[0]["body"] for event, args in emitted if event == "rpw_toast"]

    async def until(self, check, what="the condition", timeout=10.0):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if check():
                return
            await asyncio.sleep(0.02)
        self.fail("Timed out waiting for " + what)

    def serve(self, files):
        """Serves {name: bytes} on 127.0.0.1 and returns the base URL."""
        www = os.path.join(self.dir, "www")
        os.makedirs(www, exist_ok=True)
        for name, data in files.items():
            with open(os.path.join(www, name), "wb") as f:
                f.write(data)
        server = http.server.ThreadingHTTPServer(
            ("127.0.0.1", 0), functools.partial(_QuietHandler, directory=www))
        threading.Thread(target=server.serve_forever, daemon=True).start()
        self.addCleanup(server.server_close)
        self.addCleanup(server.shutdown)
        return "http://127.0.0.1:%d/" % server.server_address[1]


class SessionTests(Case):
    async def test_full_flow(self):
        plugin = self.plugin
        result = await plugin.start()
        self.assertTrue(result["ok"], result)
        self.assertTrue(result["state"]["ready"])
        self.assertEqual(result["state"]["me"], {"steamid": ME, "name": "Deck"})
        self.assertEqual(result["state"]["build"], 91)
        await self.until(lambda: plugin._status is not None, "the first status")
        state = await plugin.get_state()
        self.assertEqual(state["status"]["game"], {"id": "9876543210", "name": "RetroArch", "steam": False})
        self.assertFalse(state["status"]["session"])
        await self.until(  # pushed to the panel, 50 ms after a change
            lambda: any(event == "rpw_state" and args[0]["ready"] for event, args in emitted), "a pushed state")

        # A guest link
        result = await plugin.create_link()
        self.assertTrue(result["ok"], result)
        self.assertEqual(result["url"], "https://s.team/p/FAKE-1")
        self.assertEqual(result["state"]["link"], "https://s.team/p/FAKE-1")
        await self.until(lambda: plugin._status["waiting"], "the link in the status")
        self.assertEqual(plugin._status["waiting"],
                         [{"steamid": "0", "guest": 1, "name": "", "link": "https://s.team/p/FAKE-1"}])
        self.assertTrue(plugin._status["session"])
        self.assertTrue(plugin._status["screen"])

        # Friends: online first, then by name
        result = await plugin.get_friends()
        self.assertTrue(result["ok"], result)
        self.assertEqual([f["name"] for f in result["friends"]], ["Alice", "Bob", "dave", "Carol"])
        self.assertEqual(result["friends"][1], {"steamid": BOB, "name": "Bob", "state": "play", "game": "Celeste"})

        # Alice accepts at once
        result = await plugin.invite(ALICE)
        self.assertTrue(result["ok"], result)
        self.assertIn("Invited Alice", self.toasts())
        await self.until(lambda: plugin._status["guests"], "Alice in the status")
        alice = plugin._status["guests"][0]
        self.assertEqual((alice["key"], alice["name"], alice["keyboard"], alice["controller"]),
                         ("g-alice", "Alice", False, True))
        await self.until(lambda: "Alice joined" in self.toasts(), "Alice's toast")

        # Bob's invite waits, and Steam made no group
        result = await plugin.invite(int(BOB))
        self.assertTrue(result["ok"], result)
        await self.until(lambda: any(w["steamid"] == BOB for w in plugin._status["waiting"]), "Bob's invite")
        await self.until(lambda: "Guests may only watch" in self.toasts(), "the group-failed toast")
        await self.until(lambda: "Bob's invite is on its way." in self.toast_bodies(), "the notice")

        # Taking back Bob's invite, then the link
        bob = next(w for w in plugin._status["waiting"] if w["steamid"] == BOB)
        result = await plugin.cancel(BOB, bob["guest"])
        self.assertTrue(result["ok"], result)
        self.assertEqual([w["steamid"] for w in result["state"]["status"]["waiting"]], ["0"])
        result = await plugin.cancel("0", 1)
        self.assertTrue(result["ok"], result)
        self.assertEqual(result["state"]["status"]["waiting"], [])
        self.assertEqual(result["state"]["link"], "")

        # A refused invite is toasted by the backend
        result = await plugin.invite("666")
        self.assertFalse(result["ok"])
        self.assertTrue(result["toasted"])
        self.assertEqual(result["message"], "Steam refused the invite.")
        self.assertIn("Invite failed", self.toasts())

        # Alice's keyboard, mouse and controller
        result = await plugin.set_input("g-alice", True, False, "1")
        self.assertTrue(result["ok"], result)
        self.assertIn("input g-alice 1 0 1", self.commands())
        alice = result["state"]["status"]["guests"][0]
        self.assertEqual((alice["keyboard"], alice["mouse"], alice["controller"]), (True, False, True))

        # Removing her
        result = await plugin.remove_guest("g-alice")
        self.assertTrue(result["ok"], result)
        self.assertEqual(result["state"]["status"]["guests"], [])
        await self.until(lambda: "Alice left" in self.toasts(), "Alice's leaving toast")

        # Ending the session
        result = await plugin.end_session()
        self.assertTrue(result["ok"], result)
        self.assertIn("Session ended", self.toasts())
        await self.until(lambda: not plugin._status["session"], "the session's end")

        # Stopping sends quit, which would leave a session alone
        result = await plugin.stop()
        self.assertTrue(result["ok"], result)
        self.assertFalse(result["state"]["running"])
        self.assertIsNone(result["state"]["exit"])
        self.assertEqual(self.commands()[-1], "quit")
        self.assertEqual(self.commands().count("end"), 1)
        self.assertEqual(self.runs(), 1)
        result = await plugin.create_link()
        self.assertEqual(result["error"], "not-running")

    async def test_command_line_and_environment(self):
        os.environ["LD_LIBRARY_PATH"] = "/opt/decky-bundle"
        self.addCleanup(os.environ.pop, "LD_LIBRARY_PATH", None)
        self.assertTrue((await self.plugin.start())["ok"])
        argv, env = self.first_run()
        uid = self.plugin._uid()
        self.assertEqual(argv, ["--serve"])
        self.assertEqual(env["HOME"], self.home)
        self.assertEqual(env["USER"], "deck")
        self.assertEqual(env["LOGNAME"], "deck")
        self.assertEqual(env["XDG_RUNTIME_DIR"], "/run/user/%d" % uid)
        self.assertEqual(env["DBUS_SESSION_BUS_ADDRESS"], "unix:path=/run/user/%d/bus" % uid)
        self.assertEqual(env["APPIMAGE_EXTRACT_AND_RUN"], "1")
        self.assertTrue(env["DISPLAY"])
        self.assertTrue(env["LANG"])
        self.assertIsNone(env["LD_LIBRARY_PATH"])  # Decky's own libraries stay out
        if not (hasattr(os, "geteuid") and os.geteuid() == 0):
            self.assertEqual(self.plugin._demotion(), {})

    async def test_no_game(self):
        self.mode("no-game")
        self.assertTrue((await self.plugin.start())["ok"])
        await self.until(lambda: self.plugin._status is not None, "the first status")
        self.assertIsNone(self.plugin._status["game"])
        result = await self.plugin.create_link()
        self.assertFalse(result["ok"])
        self.assertEqual(result["error"], "no-game")
        self.assertEqual(result["message"], "Start a game through Steam first.")
        self.assertTrue(result["toasted"])
        self.assertIn("Start a game through Steam first.", self.toast_bodies())
        result = await self.plugin.invite(ALICE)
        self.assertEqual(result["error"], "no-game")

    async def test_bad_arguments(self):
        for result, error in (
            (await self.plugin.invite("7656 1198"), "bad-steamid"),
            (await self.plugin.invite("abc"), "bad-steamid"),
            (await self.plugin.cancel("0", "x"), "bad-invite"),
            (await self.plugin.set_input("a key", True, True, True), "bad-guest"),
            (await self.plugin.remove_guest(""), "bad-guest"),
            (await self.plugin.create_link(), "not-running"),
        ):
            self.assertFalse(result["ok"])
            self.assertEqual(result["error"], error)
            self.assertFalse(result["toasted"])
        self.assertEqual(self.runs(), 0)
        self.assertTrue((await self.plugin.start())["ok"])
        result = await self.plugin.set_input("nobody", True, False, False)
        self.assertEqual(result["error"], "no-guest")
        self.assertTrue(result["toasted"])
        self.assertIn("input nobody 1 0 0", self.commands())

    async def test_status_requests_are_throttled(self):
        self.assertTrue((await self.plugin.start())["ok"])
        await self.until(lambda: self.plugin._status is not None, "the first status")
        await self.plugin.get_state()  # opens the panel
        await asyncio.sleep(0.1)
        before = self.commands().count("status")
        self.plugin._status_sent = 0.0
        await self.plugin.get_state()
        await self.plugin.get_state()
        await self.until(lambda: self.commands().count("status") > before, "a status request")
        await asyncio.sleep(0.2)
        self.assertEqual(self.commands().count("status"), before + 1)


class LifecycleTests(Case):
    async def test_unload_quits_without_ending_the_session(self):
        self.assertTrue((await self.plugin.start())["ok"])
        self.assertTrue((await self.plugin.create_link())["ok"])
        child = self.plugin._child
        await self.plugin._unload()
        self.assertEqual(child.proc.returncode, 0)
        self.assertTrue(child.saw_bye)
        self.assertEqual(self.commands()[-1], "quit")
        self.assertNotIn("end", self.commands())

    async def test_unload_kills_an_app_that_hangs(self):
        self.mode("hang-on-quit")
        self.plugin.quit_timeout = 0.5
        self.assertTrue((await self.plugin.start())["ok"])
        child = self.plugin._child
        began = time.monotonic()
        await self.plugin._unload()
        took = time.monotonic() - began
        self.assertIsNotNone(child.proc.returncode)
        self.assertNotEqual(child.proc.returncode, 0)
        self.assertGreaterEqual(took, 0.45)
        self.assertLess(took, 6)
        self.assertEqual(self.commands()[-1], "quit")

    async def test_autostart_when_the_panel_opens(self):
        state = await self.plugin.get_state()  # the first call: the panel just opened
        self.assertTrue(state["running"])
        await self.until(lambda: self.plugin._ready() and self.plugin._status is not None, "the app")
        await self.plugin.get_state()
        self.assertEqual(self.runs(), 1)

        self.assertTrue((await self.plugin.stop())["ok"])
        state = await self.plugin.get_state()  # the panel is still open: it stays off
        self.assertFalse(state["running"])
        self.plugin._panel_seen -= 60  # as if the panel closed a minute ago
        state = await self.plugin.get_state()
        self.assertTrue(state["running"])
        await self.until(lambda: self.plugin._ready(), "the app")
        self.assertEqual(self.runs(), 2)

        self.assertTrue((await self.plugin.stop())["ok"])
        self.assertTrue((await self.plugin.set_settings({"autostart": False}))["ok"])
        self.plugin._panel_seen -= 60
        state = await self.plugin.get_state()
        self.assertFalse(state["running"])
        self.assertTrue(state["installed"])
        self.assertEqual(self.runs(), 2)

    async def test_restarts_after_a_crash(self):
        self.mode("die-once")
        self.plugin.restart_delays = (0.05,)
        self.assertTrue((await self.plugin.start())["ok"])
        await self.until(lambda: self.runs() == 2 and self.plugin._ready(), "the restart")
        exits = [args[0]["exit"] for event, args in emitted if event == "rpw_state" and args[0]["exit"]]
        self.assertTrue(exits)
        self.assertEqual(exits[0]["code"], 3)
        self.assertEqual(exits[0]["reason"], "fake: lost the connection to Steam")
        state = self.plugin._snapshot()
        self.assertIsNone(state["exit"])  # cleared once it was ready again
        self.assertFalse(state["gave_up"])

    async def test_startup_errors_are_retried_then_given_up(self):
        self.mode("fail:steam-not-running")
        self.plugin.restart_delays = (0.05, 0.05)
        result = await self.plugin.start()
        self.assertFalse(result["ok"])
        self.assertEqual(result["message"], "Steam isn't running.")
        await self.until(lambda: self.plugin._gave_up, "giving up")
        self.assertEqual(self.runs(), 3)
        state = self.plugin._snapshot()
        self.assertFalse(state["running"])
        self.assertIsNone(state["restart_at"])
        self.assertEqual(state["exit"]["error"], "steam-not-running")
        self.assertEqual(state["exit"]["reason"], "Steam isn't running.")
        self.assertEqual(state["exit"]["code"], 1)
        # Opening the panel tries again.
        await self.plugin.get_state()
        await self.until(lambda: self.runs() == 4, "another try")

    async def test_a_steam_without_the_methods_isnt_retried(self):
        self.mode("fail:methods-missing")
        self.plugin.restart_delays = (0.05, 0.05)
        result = await self.plugin.start()
        self.assertFalse(result["ok"])
        self.assertTrue(result["state"]["gave_up"])
        await asyncio.sleep(0.3)
        await self.plugin.get_state()  # opening the panel doesn't retry the same AppImage
        await asyncio.sleep(0.3)
        self.assertEqual(self.runs(), 1)
        with open(self.fake, "a", encoding="utf-8") as f:  # a new AppImage is tried, though
            f.write("\n# updated\n")
        self.plugin._panel_seen -= 60
        await self.plugin.get_state()
        await self.until(lambda: self.runs() == 2, "a try with the new AppImage")

    async def test_the_exit_reason_comes_from_stderr(self):
        self.mode("crash")
        result = await self.plugin.start()
        self.assertFalse(result["ok"])
        self.assertEqual(result["message"], "Segmentation fault (core dumped)")
        self.assertEqual(result["state"]["exit"]["code"], 139)
        self.assertTrue(result["state"]["gave_up"])  # no restarts in these tests
        self.assertFalse(result["state"]["too_old"])  # a crash isn't an old build


class InstallTests(Case):
    def put(self, folder, name, age=0.0):
        os.makedirs(folder, exist_ok=True)
        path = os.path.join(folder, name)
        with open(path, "wb") as f:
            f.write(b"\x7fELF")
        when = time.time() - age
        os.utime(path, (when, when))
        return path

    async def test_where_the_appimage_is_found(self):
        plugin = self.plugin
        plugin.binary_override = None
        find = functools.partial(plugin._find_binary, fresh=True)
        self.assertIsNone(find())
        found = self.put(decky.DECKY_PLUGIN_RUNTIME_DIR, main.APPIMAGE_NAME)
        self.assertEqual(find(), found)
        found = self.put(os.path.join(decky.DECKY_PLUGIN_DIR, "bin"), "RemotePlayWhatever")
        self.assertEqual(find(), found)
        found = self.put(os.path.join(self.home, "Desktop"), "RemotePlayWhatever-linux-x86_64.AppImage")
        self.assertEqual(find(), found)
        found = self.put(os.path.join(self.home, "Downloads"), "remoteplaywhatever-test.appimage")
        self.assertEqual(find(), found)
        apps = os.path.join(self.home, "Applications")
        self.put(apps, "RemotePlayWhatever-old.AppImage", age=100)
        found = self.put(apps, "RemotePlayWhatever-new.AppImage", age=50)
        self.put(apps, "RemotePlayWhatever-next.AppImage.part")
        self.put(apps, "Other.AppImage")
        self.assertEqual(find(), found)
        chosen = self.put(os.path.join(self.dir, "elsewhere"), "rpw")
        self.assertTrue((await plugin.set_settings({"appimage_path": chosen}))["ok"])
        self.assertEqual(find(), chosen)
        os.remove(chosen)  # a saved path that's gone is skipped
        self.assertEqual(find(), found)
        state = await plugin.get_state()
        self.assertEqual(state["binary"], found)
        self.assertTrue(state["installed"])

    async def test_settings(self):
        plugin = self.plugin
        self.assertEqual(await plugin.get_settings(), main.DEFAULT_SETTINGS)
        result = await plugin.set_settings({"autostart": False})
        self.assertTrue(result["ok"], result)
        self.assertFalse(result["settings"]["autostart"])
        with open(os.path.join(decky.DECKY_PLUGIN_SETTINGS_DIR, "settings.json"), encoding="utf-8") as f:
            self.assertFalse(json.load(f)["autostart"])
        self.assertFalse(main.Plugin()._load_settings()["autostart"])  # read back by a new backend

        # A hand edit while the plugin runs is noticed, and kept by the next save.
        settings_file = os.path.join(decky.DECKY_PLUGIN_SETTINGS_DIR, "settings.json")
        with open(settings_file, "w", encoding="utf-8") as f:
            json.dump({"autostart": False, "download_url": "https://example.com/test.AppImage"}, f)
        later = os.stat(settings_file).st_mtime_ns + 2_000_000_000
        os.utime(settings_file, ns=(later, later))
        self.assertEqual((await plugin.get_settings())["download_url"], "https://example.com/test.AppImage")
        self.assertTrue((await plugin.set_settings({"autostart": True}))["ok"])
        with open(settings_file, encoding="utf-8") as f:
            self.assertEqual(json.load(f), {
                "appimage_path": "", "autostart": True, "download_url": "https://example.com/test.AppImage"})

        missing = os.path.join(self.dir, "nowhere", "rpw.AppImage")
        self.assertEqual((await plugin.set_settings({"appimage_path": missing}))["error"], "not-found")
        self.assertEqual(plugin._load_settings()["appimage_path"], "")
        for bad in ({"autostart": "yes"}, "autostart", {"download_url": "file:///etc/passwd"}):
            self.assertEqual((await plugin.set_settings(bad))["error"], "bad-settings")

        with open(os.path.join(decky.DECKY_PLUGIN_SETTINGS_DIR, "settings.json"), "w", encoding="utf-8") as f:
            f.write("{not json")
        self.assertEqual(main.Plugin()._load_settings(), main.DEFAULT_SETTINGS)

        # Decky uninstalls a plugin to update it, so the AppImage stays.
        kept = self.put(decky.DECKY_PLUGIN_RUNTIME_DIR, main.APPIMAGE_NAME)
        await plugin._uninstall()
        self.assertTrue(os.path.exists(kept))

    async def test_download(self):
        plugin = self.plugin
        plugin.binary_override = None
        payload = b"\x7fELF" + os.urandom(300 * 1024)
        base = self.serve({"rpw.AppImage": payload, "page.html": b"<!doctype html><title>Sign in</title>"})
        self.assertTrue((await plugin.set_settings({"autostart": False, "download_url": base + "rpw.AppImage"}))["ok"])
        result = await plugin.download_appimage()
        self.assertTrue(result["ok"], result)
        dest = os.path.join(decky.DECKY_PLUGIN_RUNTIME_DIR, main.APPIMAGE_NAME)
        self.assertEqual(result["path"], dest)
        with open(dest, "rb") as f:
            self.assertEqual(f.read(), payload)
        if os.name == "posix":
            self.assertTrue(os.stat(dest).st_mode & 0o111)
        self.assertEqual(plugin._load_settings()["appimage_path"], dest)
        self.assertEqual(result["state"]["binary"], dest)
        self.assertTrue(result["state"]["download"]["done"])
        self.assertEqual(result["state"]["download"]["received"], len(payload))
        self.assertIn("Remote Play Whatever downloaded", self.toasts())
        self.assertFalse(plugin._running())  # autostart is off

        # A web page isn't an AppImage, and a failed download keeps the one there.
        self.assertTrue((await plugin.set_settings({"download_url": base + "page.html"}))["ok"])
        result = await plugin.download_appimage()
        self.assertFalse(result["ok"])
        self.assertTrue(result["toasted"])
        self.assertEqual(result["error"], "download-failed")
        self.assertIn("isn't an AppImage", result["message"])
        with open(dest, "rb") as f:
            self.assertEqual(f.read(), payload)
        self.assertFalse(os.path.exists(dest + ".part"))

        self.assertTrue((await plugin.set_settings({"download_url": base + "missing.AppImage"}))["ok"])
        result = await plugin.download_appimage()
        self.assertFalse(result["ok"])
        self.assertIn("404", result["message"])
        self.assertEqual(result["state"]["download"]["error"], result["message"])
        self.assertIn("Download failed", self.toasts())

    async def test_a_download_restarts_an_idle_app_but_not_a_session(self):
        plugin = self.plugin  # binary_override stays, so it's the fake that restarts
        base = self.serve({"rpw.AppImage": b"\x7fELF" + b"\0" * 4096})
        self.assertTrue((await plugin.set_settings({"download_url": base + "rpw.AppImage"}))["ok"])
        self.assertTrue((await plugin.start())["ok"])
        self.assertTrue((await plugin.download_appimage())["ok"])
        await self.until(lambda: self.runs() == 2 and plugin._ready(), "the restart")

        self.assertTrue((await plugin.create_link())["ok"])
        await self.until(lambda: plugin._status and plugin._status["session"], "the session")
        self.assertTrue((await plugin.download_appimage())["ok"])
        await asyncio.sleep(0.3)
        self.assertEqual(self.runs(), 2)
        self.assertTrue(any("after this session" in body for body in self.toast_bodies()))


class TooOldTests(Case):
    """AppImages from before --serve, like every release before the plugin's."""

    async def asyncSetUp(self):
        await super().asyncSetUp()
        self.plugin.binary_override = None  # the AppImages are searched for
        self.plugin.restart_delays = (0.05, 0.05)  # restarts would show in the run counts

    async def test_an_appimage_from_before_serve_is_too_old(self):
        plugin = self.plugin
        old = self.put_script(os.path.join(self.home, "Applications"), main.APPIMAGE_NAME, OLD_RPW)
        await plugin.get_state()  # the panel opens, and starts it
        await self.until(lambda: plugin._snapshot()["too_old"], "the verdict")
        state = await plugin.get_state()
        self.assertFalse(state["running"])
        self.assertEqual(state["binary"], old)
        self.assertEqual(state["exit"]["error"], "too-old")
        self.assertEqual(state["exit"]["reason"], main.TOO_OLD_MESSAGE)
        self.assertEqual(state["exit"]["code"], 255)
        self.assertTrue(state["gave_up"])
        self.assertIsNone(state["restart_at"])
        self.assertEqual(self.old_runs(old), 1)

        # It isn't run again: no restarts, not when the panel reopens, not by Start.
        await asyncio.sleep(0.3)
        plugin._panel_seen -= 60
        state = await plugin.get_state()
        self.assertFalse(state["running"])
        self.assertTrue(state["too_old"])
        result = await plugin.start()
        self.assertFalse(result["ok"])
        self.assertEqual(result["error"], "too-old")
        self.assertEqual(result["message"], main.TOO_OLD_MESSAGE)
        self.assertTrue(result["state"]["too_old"])
        self.assertEqual(result["state"]["exit"]["code"], 255)  # still what it said when it ran
        await asyncio.sleep(0.3)
        plugin._panel_seen -= 60
        await plugin.get_state()
        await asyncio.sleep(0.3)
        self.assertEqual(self.old_runs(), 1)
        self.assertEqual(self.runs(), 0)

        # Update fetches the newest one, which replaces it and starts, though autostart is off.
        base = self.serve({"rpw.AppImage": appimage_of(FAKE_RPW)})
        settings = {"autostart": False, "download_url": base + "rpw.AppImage"}
        self.assertTrue((await plugin.set_settings(settings))["ok"])
        result = await plugin.download_appimage()
        self.assertTrue(result["ok"], result)
        self.assertIn("Starting it now.", self.toast_bodies())
        await self.until(plugin._ready, "the new AppImage")
        state = await plugin.get_state()
        dest = os.path.join(decky.DECKY_PLUGIN_RUNTIME_DIR, main.APPIMAGE_NAME)
        self.assertEqual(state["binary"], dest)
        self.assertFalse(state["too_old"])
        self.assertFalse(state["gave_up"])
        self.assertIsNone(state["exit"])
        self.assertEqual((self.old_runs(), self.runs()), (1, 1))

    async def test_an_older_appimage_doesnt_hide_a_newer_one(self):
        plugin = self.plugin
        old = self.put_script(os.path.join(self.home, "Applications"), main.APPIMAGE_NAME, OLD_RPW)
        good = self.put_script(os.path.join(self.home, "Downloads"), main.APPIMAGE_NAME, FAKE_RPW)
        self.assertEqual(plugin._find_binary(fresh=True), old)  # ~/Applications comes first
        spawn_now = plugin._spawn_now

        async def slow_spawn_now():
            # The AppImage found next takes a moment to start: start() waits for it all the same.
            if plugin._too_old:
                await asyncio.sleep(0.3)
            return await spawn_now()

        plugin._spawn_now = slow_spawn_now
        result = await plugin.start()  # the old one says it's too old, and the next one starts
        self.assertTrue(result["ok"], result)
        self.assertEqual(result["state"]["binary"], good)
        self.assertFalse(result["state"]["too_old"])
        self.assertIsNone(result["state"]["exit"])
        self.assertEqual((self.old_runs(old), self.runs()), (1, 1))

        # From then on, the newer one is used straight away.
        self.assertTrue((await plugin.stop())["ok"])
        plugin._panel_seen -= 60
        state = await plugin.get_state()
        self.assertTrue(state["running"])
        self.assertEqual(state["binary"], good)
        await self.until(plugin._ready, "the newer AppImage")
        self.assertEqual((self.old_runs(), self.runs()), (1, 2))

    async def test_when_every_appimage_is_too_old(self):
        plugin = self.plugin
        old = self.put_script(os.path.join(self.home, "Applications"), main.APPIMAGE_NAME, OLD_RPW)
        older = self.put_script(os.path.join(self.home, "Desktop"), "RemotePlayWhatever.AppImage", OLD_RPW)
        result = await plugin.start()  # each is tried once
        self.assertFalse(result["ok"])
        self.assertEqual(result["error"], "too-old")
        self.assertEqual(result["message"], main.TOO_OLD_MESSAGE)
        self.assertTrue(result["state"]["too_old"])
        self.assertEqual(result["state"]["binary"], old)  # the panel says the first one found is too old
        self.assertEqual(result["state"]["exit"]["reason"], main.TOO_OLD_MESSAGE)
        self.assertEqual((self.old_runs(old), self.old_runs(older)), (1, 1))
        await asyncio.sleep(0.3)
        plugin._panel_seen -= 60
        state = await plugin.get_state()
        self.assertFalse(state["running"])
        self.assertTrue(state["too_old"])
        self.assertEqual(self.old_runs(), 2)

        # A newer one in ~/Downloads starts when the panel next opens.
        good = self.put_script(os.path.join(self.home, "Downloads"), main.APPIMAGE_NAME, FAKE_RPW)
        plugin._panel_seen -= 60
        state = await plugin.get_state()
        self.assertTrue(state["running"])
        self.assertEqual(state["binary"], good)
        await self.until(plugin._ready, "the newer AppImage")
        self.assertEqual((self.old_runs(), self.runs()), (2, 1))

        # A too-old file that changes (replaced by a newer build) is tried again.
        self.assertTrue((await plugin.stop())["ok"])
        self.put_script(os.path.join(self.home, "Applications"), main.APPIMAGE_NAME, FAKE_RPW)
        plugin._panel_seen -= 60
        state = await plugin.get_state()
        self.assertEqual(state["binary"], old)
        await self.until(plugin._ready, "the replaced AppImage")
        self.assertEqual((self.old_runs(), self.runs()), (2, 2))


class PredatesServeTests(unittest.TestCase):
    """Which exits mean a build from before --serve."""

    def child(self, *lines, error=None, ready=False):
        child = main._Child(None, "RemotePlayWhatever.AppImage", False)
        for line in lines:
            child.note_stderr(line)
        child.error = error
        child.ready_at = 1.0 if ready else 0.0
        return child

    def test_too_old(self):
        predates = main._predates_serve
        unknown, *usage = OLD_STDERR.splitlines()
        self.assertTrue(predates(self.child(unknown, *usage)))
        self.assertTrue(predates(self.child(unknown)))
        self.assertTrue(predates(self.child("Unknown option 'serve'")))
        self.assertTrue(predates(self.child("Gtk-Message: 10:00:00.000: Failed to load module", unknown)))
        self.assertTrue(predates(self.child(*usage)))  # usage text and no "error" event

    def test_not_too_old(self):
        predates = main._predates_serve
        unknown, *usage = OLD_STDERR.splitlines()
        error = {"error": "steam-not-running", "message": "Steam isn't running."}
        self.assertFalse(predates(self.child()))
        self.assertFalse(predates(self.child(*usage, error=error)))  # a newer build says why in an event
        self.assertFalse(predates(self.child(unknown, *usage, ready=True)))  # it was ready
        self.assertFalse(predates(self.child("Segmentation fault (core dumped)")))
        self.assertFalse(predates(self.child("fake: lost the connection to Steam")))
        self.assertFalse(predates(self.child("Error: Unable to initialize GTK+, is DISPLAY set properly?")))


def tearDownModule():
    if not os.environ.get("RPW_KEEP_TEST_WORK"):
        shutil.rmtree(WORK, ignore_errors=True)


if __name__ == "__main__":
    unittest.main(verbosity=2)
