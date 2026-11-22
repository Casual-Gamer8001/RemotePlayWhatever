"""Remote Play Whatever for Decky Loader: the backend.

It runs RemotePlayWhatever's headless ``--serve`` mode as a plain background
child process, never as a game. In Game Mode, Steam mishandles two non-Steam
games running at once: closing Remote Play Whatever, added to Steam as a
non-Steam game, could freeze Steam until the Deck restarted (upstream issues
#58, #69 and #132). Started from here, Steam doesn't see it at all, and the game
being shared is the one the user starts from their library as usual.

The Quick Access Menu panel (src/index.tsx) calls the public coroutines of
Plugin. The backend speaks the ``--serve`` JSON-lines protocol (plain-word
commands on stdin, one JSON event per line on stdout) and relays the news to the
panel as the Decky events "rpw_state" and "rpw_toast". Standard library only.
"""

from __future__ import annotations

import asyncio
import collections
import json
import os
import re
import signal
import ssl
import stat
import subprocess
import time
import urllib.error
import urllib.request
import uuid
from typing import Any, Awaitable, Callable, Deque, Dict, Iterator, List, Optional, Tuple

try:
    import pwd
except ImportError:  # not Linux: only the tests run there
    pwd = None  # type: ignore[assignment]

import decky

DOWNLOAD_URL = (
    "https://github.com/Casual-Gamer8001/RemotePlayWhatever/releases/latest/download/"
    "RemotePlayWhatever-linux-x86_64.AppImage"
)
APPIMAGE_NAME = "RemotePlayWhatever-linux-x86_64.AppImage"
SEARCH_FOLDERS = ("Applications", "Downloads", "Desktop")  # in the user's home, in this order
PROTOCOL = 1
STATUS_INTERVAL = 2.0  # at most one "status" request this often (only while the panel polls)
PANEL_IDLE = 5.0  # no get_state() for this long: the panel was closed
QUIT_TIMEOUT = 3.0  # "quit" gets this long, then the process is killed
RESTART_DELAYS = (3.0, 10.0, 30.0)  # waits before each restart after an exit; then it gives up
STABLE_AFTER = 60.0  # running this long resets the restart count
LINE_LIMIT = 8 * 1024 * 1024  # longest stdout line (a big friends list is one line)
SESSION_ENV = ("DISPLAY", "XAUTHORITY", "LANG", "LANGUAGE", "LC_ALL", "LC_CTYPE", "LC_MESSAGES")
TOASTED_COMMANDS = ("link", "invite", "cancel", "input", "remove", "end")
DEFAULT_SETTINGS: Dict[str, Any] = {"appimage_path": "", "autostart": True, "download_url": ""}
CA_FILES = ("/etc/ssl/certs/ca-certificates.crt", "/etc/ssl/cert.pem", "/etc/pki/tls/certs/ca-bundle.crt")
# A build from before --serve can't work with the plugin. Its wxWidgets command-line parser says
# so on stderr, "Unknown long option 'serve'" and then the usage text, and exits (code 255).
TOO_OLD = "too-old"
TOO_OLD_MESSAGE = "This Remote Play Whatever is too old for the plugin. Select Update to get the newest one."
UNKNOWN_SERVE = re.compile(r"unknown (long )?option.*\bserve\b", re.IGNORECASE)
USAGE_LINE = re.compile(r"^\s*usage:|^\s+(-\w, )?--[a-z][\w-]*", re.IGNORECASE)

Event = Dict[str, Any]
Result = Dict[str, Any]


class _Child:
    """One run of ``RemotePlayWhatever --serve``."""

    def __init__(self, proc: asyncio.subprocess.Process, binary: str, own_group: bool) -> None:
        self.proc = proc
        self.binary = binary
        self.own_group = own_group  # it leads its own process group (POSIX)
        self.ready_at = 0.0
        self.error: Optional[Dict[str, Any]] = None  # its last "error" event that wasn't about a command
        self.stderr: Deque[str] = collections.deque(maxlen=20)
        self.unknown_serve = False  # stderr said --serve is an unknown option
        self.usage = False  # stderr had command-line usage text
        self.saw_bye = False
        self.stopping = False
        self.stdout_done = asyncio.Event()
        self.stderr_done = asyncio.Event()
        self.exited = asyncio.Event()

    @property
    def alive(self) -> bool:
        return self.proc.returncode is None

    @property
    def ready(self) -> bool:
        return self.ready_at > 0 and self.alive

    def note_stderr(self, line: str) -> None:
        self.stderr.append(line[:500])
        if UNKNOWN_SERVE.search(line):
            self.unknown_serve = True
        if USAGE_LINE.search(line):
            self.usage = True


class Plugin:
    """The backend of the Quick Access Menu panel."""

    # Test hooks: the tests point binary_override at a fake RemotePlayWhatever,
    # run it through launcher (an interpreter), and add extra_env to its
    # environment (the PATH and SYSTEMROOT Python needs on Windows).
    binary_override: Optional[str] = None
    launcher: Tuple[str, ...] = ()
    extra_env: Optional[Dict[str, str]] = None
    restart_delays: Tuple[float, ...] = RESTART_DELAYS
    quit_timeout: float = QUIT_TIMEOUT
    command_timeout: float = 15.0
    ready_timeout: float = 30.0

    def __init__(self) -> None:
        self._child: Optional[_Child] = None
        self._spawn_lock: Optional[asyncio.Lock] = None
        self._tasks: set = set()
        self._waiters: List[Tuple[Callable[[Event], bool], "asyncio.Future[Optional[Event]]", float]] = []
        self._me: Optional[Dict[str, str]] = None
        self._build: Any = None
        self._status: Optional[Dict[str, Any]] = None
        self._status_at = 0.0
        self._status_sent = 0.0
        self._link = ""
        self._friends: List[Dict[str, str]] = []
        self._events: Deque[Event] = collections.deque(maxlen=30)
        self._error: Optional[Dict[str, Any]] = None
        self._exit: Optional[Dict[str, Any]] = None
        self._restart_count = 0
        self._restart_task: Optional["asyncio.Task[Any]"] = None
        self._restart_at: Optional[float] = None
        self._gave_up = False
        self._fatal: Optional[Tuple[str, float, int]] = None  # the AppImage that can't work with this Steam
        self._too_old: set = set()  # the AppImages (_file_key) from before --serve: never run again
        self._panel_seen = float("-inf")
        self._settings: Optional[Dict[str, Any]] = None
        self._settings_stamp: Optional[int] = None  # settings.json's mtime when read, to notice hand edits
        self._binary_cache: Tuple[float, Optional[str]] = (float("-inf"), None)
        self._download: Dict[str, Any] = _download_state()
        self._download_cancel = False
        self._download_emitted = 0.0
        self._seq = 0
        self._emit_pending = False
        self._instance = uuid.uuid4().hex[:12]
        self._unloading = False

    # Decky's lifecycle

    async def _main(self) -> None:
        self._load_settings()
        binary = self._find_binary(fresh=True)
        decky.logger.info(
            "Remote Play Whatever plugin %s for %s; AppImage: %s", _version(), self._user(), binary or "not found"
        )

    async def _unload(self) -> None:
        # "quit", not "end": a Remote Play session outlives the plugin, like the app's window.
        self._unloading = True
        self._download_cancel = True
        self._cancel_restart()
        await self._shutdown()
        for task in list(self._tasks):
            task.cancel()
        decky.logger.info("Unloaded")

    async def _uninstall(self) -> None:
        # Decky calls this when it updates the plugin too, so the downloaded AppImage stays.
        decky.logger.info("Uninstalled")

    # Called by the panel

    async def get_state(self) -> Result:
        """The panel's heartbeat: everything it shows. Polled every 2 s while it's open."""
        now = time.monotonic()
        opened = now - self._panel_seen > PANEL_IDLE
        self._panel_seen = now
        if opened:
            await self._panel_opened()
        if self._ready() and now - self._status_sent >= STATUS_INTERVAL - 0.25:
            answer = await self._request_status()
            if answer is not None and (opened or now - self._status_at > 3 * STATUS_INTERVAL):
                # A panel that just opened shows fresh news, if it comes quickly.
                try:
                    await asyncio.wait_for(asyncio.shield(answer), 0.75)
                except asyncio.TimeoutError:
                    pass
        return self._snapshot()

    async def start(self) -> Result:
        if self._unloading:
            return self._result(False, "unloading", "Decky is stopping the plugin.")
        self._cancel_restart()
        self._restart_count = 0
        self._gave_up = False
        self._fatal = None
        if await self._spawn():
            await self._wait_ready(self.ready_timeout)
        if self._ready():
            return self._result(True)
        if self._running():
            return self._result(False, "not-ready", "Remote Play Whatever is still starting.")
        if (self._exit or {}).get("error") == TOO_OLD:
            return self._result(False, TOO_OLD, TOO_OLD_MESSAGE)
        return self._result(False, "not-started", self._exit_message() or "Remote Play Whatever didn't start.")

    async def stop(self) -> Result:
        self._cancel_restart()
        self._gave_up = False
        await self._shutdown()
        self._changed()
        return self._result(True)

    async def create_link(self) -> Result:
        event, problem = await self._ask("link", "link", ("invite",), lambda e: _text(e.get("steamid")) == "0")
        if problem is not None:
            return problem
        if event.get("result") != "ok" or not _text(event.get("url")):
            message = _text(event.get("message")) or "Steam didn't make a guest link."
            return self._result(False, "failed", message, toasted=event.get("result") != "ok")
        return self._result(True, url=_text(event.get("url")))

    async def invite(self, steamid: Any) -> Result:
        sid = _steamid(steamid)
        if sid is None or sid == "0":
            return self._result(False, "bad-steamid", "That isn't a friend's SteamID.")
        event, problem = await self._ask(
            "invite " + sid, "invite", ("invite",), lambda e: _text(e.get("steamid")) == sid
        )
        if problem is not None:
            return problem
        if event.get("result") != "ok":
            message = _text(event.get("message")) or "Steam didn't send the invite."
            return self._result(False, "failed", message, toasted=True)
        return self._result(True, url=_text(event.get("url")))

    async def cancel(self, steamid: Any, guest: Any) -> Result:
        """Takes back an invite or a guest link (steamid "0") nobody has used yet."""
        sid = _steamid(steamid)
        guest_id = _int(guest)
        if sid is None or guest_id is None or guest_id < 0:
            return self._result(False, "bad-invite", "That isn't an invite.")
        _, problem = await self._ask("cancel {} {}".format(sid, guest_id), "cancel", ("status",))
        return problem if problem is not None else self._result(True)

    async def set_input(self, key: Any, keyboard: Any, mouse: Any, controller: Any) -> Result:
        """What a guest may use: their keyboard, mouse and controller."""
        token = _token(key)
        if token is None:
            return self._result(False, "bad-guest", "That isn't a guest.")
        line = "input {} {} {} {}".format(token, _flag(keyboard), _flag(mouse), _flag(controller))
        _, problem = await self._ask(line, "input", ("status",))
        return problem if problem is not None else self._result(True)

    async def remove_guest(self, key: Any) -> Result:
        token = _token(key)
        if token is None:
            return self._result(False, "bad-guest", "That isn't a guest.")
        _, problem = await self._ask("remove " + token, "remove", ("status",))
        return problem if problem is not None else self._result(True)

    async def end_session(self) -> Result:
        _, problem = await self._ask("end", "end", ("ended",))
        if problem is not None:
            return problem
        await self._request_status()
        return self._result(True)

    async def get_friends(self) -> Result:
        """Steam friends, online ones first, each with a steamid, name, state and game."""
        _, problem = await self._ask("friends", "friends", ("friends",))
        if problem is not None:
            if self._friends:
                return self._result(True, friends=self._friends, stale=True)
            problem["friends"] = []
            return problem
        return self._result(True, friends=self._friends)

    async def get_settings(self) -> Result:
        """appimage_path ("" when it's searched for), autostart and download_url ("" for the latest release)."""
        return self._public_settings()

    async def set_settings(self, values: Any = None) -> Result:
        """Changes appimage_path ("" means search for it), autostart and download_url."""
        if not isinstance(values, dict):
            return self._result(False, "bad-settings", "Settings must be an object.")
        settings = dict(self._load_settings())
        if "autostart" in values:
            if not isinstance(values["autostart"], bool):
                return self._result(False, "bad-settings", "autostart must be true or false.")
            settings["autostart"] = values["autostart"]
        if "appimage_path" in values:
            path = _text(values["appimage_path"]).strip()
            if path:
                path = os.path.abspath(os.path.expanduser(path))
                if not os.path.isfile(path):
                    return self._result(False, "not-found", "There's no file at " + path + ".")
            settings["appimage_path"] = path
        if "download_url" in values:
            url = _text(values["download_url"]).strip()
            if url and not url.lower().startswith(("https://", "http://")):
                return self._result(False, "bad-settings", "download_url must be an http(s) address.")
            settings["download_url"] = url
        if not self._save_settings(settings):
            return self._result(False, "not-saved", "Couldn't save the settings.")
        self._binary_cache = (float("-inf"), None)
        self._changed()
        return self._result(True, settings=self._public_settings())

    async def download_appimage(self) -> Result:
        """Downloads the newest AppImage into the plugin's data folder and uses it."""
        if self._download["active"]:
            return self._result(False, "busy", "It's downloading already.")
        folder = _text(getattr(decky, "DECKY_PLUGIN_RUNTIME_DIR", ""))
        if not folder:
            return self._result(False, "no-folder", "Decky gave the plugin no data folder.")
        url = self._load_settings()["download_url"] or DOWNLOAD_URL
        dest = os.path.join(folder, APPIMAGE_NAME)
        # The AppImage found can't run (too old, say): the new one replaces it and starts.
        current = None if self._running() else self._find_binary(fresh=True)
        blocked = bool(current) and self._known_bad(current)
        self._download = _download_state(active=True)
        self._download_cancel = False
        self._changed()
        decky.logger.info("Downloading %s to %s", url, dest)
        loop = asyncio.get_running_loop()
        try:
            size = await loop.run_in_executor(None, self._fetch, url, dest, loop)
        except Exception as e:  # noqa: BLE001 - every failure is reported the same way
            message = _download_error(e)
            self._download = _download_state(error=message)
            self._changed()
            decky.logger.error("Download failed: %s", message)
            await self._toast("Download failed", message)
            return self._result(False, "download-failed", message, toasted=True)
        decky.logger.info("Downloaded %d bytes", size)
        self._download = _download_state(done=True, received=size, total=size)
        settings = dict(self._load_settings())
        settings["appimage_path"] = dest
        self._save_settings(settings)
        self._binary_cache = (float("-inf"), None)
        self._fatal = None
        self._gave_up = False
        body = ""
        if not self._unloading:
            # Use the new version now, unless that would interrupt a session.
            running = self._running()
            if running and (self._status or {}).get("session"):
                body = "It's used when Remote Play Whatever next starts, after this session."
            elif running or blocked or self._restart_task is not None or self._load_settings()["autostart"]:
                body = "Starting it now."
                self._cancel_restart()
                self._restart_count = 0
                await self._shutdown()
                await self._spawn()
        await self._toast("Remote Play Whatever downloaded", body)
        self._changed()
        return self._result(True, path=dest, bytes=size)

    # The child process

    def _running(self) -> bool:
        return self._child is not None and self._child.alive

    def _ready(self) -> bool:
        return self._child is not None and self._child.ready

    async def _panel_opened(self) -> None:
        """Starts RemotePlayWhatever when the panel opens, if the settings say so."""
        if self._unloading or self._running() or self._restart_task is not None or self._download["active"]:
            return
        if not self._load_settings()["autostart"]:
            return
        binary = self._find_binary(fresh=True)
        if not binary or self._known_bad(binary):
            return
        self._gave_up = False
        self._restart_count = 0
        await self._spawn()

    async def _spawn(self) -> bool:
        if self._spawn_lock is None:
            self._spawn_lock = asyncio.Lock()
        async with self._spawn_lock:
            if self._running():
                return True
            if self._unloading:
                return False
            return await self._spawn_now()

    async def _spawn_now(self) -> bool:
        binary = self._find_binary(fresh=True)
        if not binary:
            self._exit = _exit_info(None, "Remote Play Whatever isn't installed.")
            self._changed()
            return False
        if self._is_too_old(binary):
            # Every AppImage found is from before --serve: running one again won't help.
            self._gave_up = True
            self._fatal = _file_key(binary)
            if (self._exit or {}).get("error") != TOO_OLD:
                self._exit = _exit_info(None, TOO_OLD_MESSAGE, TOO_OLD)
            self._changed()
            return False
        problem = _make_executable(binary)
        if problem:
            self._gave_up = True
            self._exit = _exit_info(None, problem)
            self._changed()
            return False
        command = list(self.launcher) + [binary, "--serve"]
        env = self._child_env()
        options: Dict[str, Any] = self._demotion()
        own_group = os.name == "posix"
        if own_group:
            # Its own process group, so a kill reaches the AppImage runtime and the app it runs.
            options["start_new_session"] = True
        home = env.get("HOME", "")
        try:
            proc = await asyncio.create_subprocess_exec(
                *command,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                env=env,
                cwd=home if home and os.path.isdir(home) else None,
                limit=LINE_LIMIT,
                **options,
            )
        except (OSError, ValueError, subprocess.SubprocessError) as e:
            reason = "Couldn't start {}: {}".format(os.path.basename(binary), getattr(e, "strerror", None) or e)
            decky.logger.error("%s", reason)
            self._gave_up = True
            self._exit = _exit_info(None, reason)
            self._changed()
            return False
        child = _Child(proc, binary, own_group)
        self._child = child
        self._me = None
        self._build = None
        self._status = None
        self._status_at = 0.0
        self._status_sent = 0.0
        self._link = ""
        self._error = None
        decky.logger.info("Started %s (pid %d)", " ".join(command), proc.pid)
        self._task(self._read_stdout(child))
        self._task(self._read_stderr(child))
        self._task(self._watch(child))
        self._changed()
        return True

    def _child_env(self) -> Dict[str, str]:
        """A clean environment for the user's session.

        Decky's own environment isn't passed on: it's a PyInstaller bundle whose
        LD_LIBRARY_PATH points at its own libraries.
        """
        user = self._user()
        uid = self._uid()
        runtime = "/run/user/{}".format(uid)
        env = {
            "HOME": self._home(),
            "USER": user,
            "LOGNAME": user,
            "PATH": "/usr/local/bin:/usr/bin:/bin:/usr/local/sbin:/usr/sbin:/sbin",
            "XDG_RUNTIME_DIR": runtime,
            "DBUS_SESSION_BUS_ADDRESS": "unix:path={}/bus".format(runtime),
            # Game Mode may give the user no FUSE: the AppImage then unpacks itself into /tmp and runs.
            "APPIMAGE_EXTRACT_AND_RUN": "1",
        }
        env.update(_steam_env(uid))
        env.setdefault("DISPLAY", ":0")
        env.setdefault("LANG", "C.UTF-8")
        if self.extra_env:
            env.update(self.extra_env)
        return env

    def _demotion(self) -> Dict[str, Any]:
        """If Decky runs this backend as root, the app still runs as the Deck's user."""
        if pwd is None or not hasattr(os, "geteuid") or os.geteuid() != 0:
            return {}
        try:
            entry = pwd.getpwnam(self._user())
        except KeyError:
            return {}
        if entry.pw_uid == 0:
            return {}
        try:
            groups = os.getgrouplist(entry.pw_name, entry.pw_gid)
        except OSError:
            groups = [entry.pw_gid]
        return {"user": entry.pw_uid, "group": entry.pw_gid, "extra_groups": groups}

    async def _read_stdout(self, child: _Child) -> None:
        stream = child.proc.stdout
        try:
            while stream is not None:
                try:
                    raw = await stream.readline()
                except ValueError:  # a line over LINE_LIMIT
                    decky.logger.warning("Skipped an overlong line from RemotePlayWhatever")
                    continue
                if not raw:
                    break
                line = raw.decode("utf-8", "replace").strip()
                if not line:
                    continue
                try:
                    event = json.loads(line)
                except ValueError:
                    decky.logger.warning("Not JSON from RemotePlayWhatever: %s", line[:300])
                    continue
                if not isinstance(event, dict) or not isinstance(event.get("event"), str):
                    continue
                try:
                    await self._handle(child, event)
                except Exception:  # noqa: BLE001 - one bad event mustn't stop the reader
                    decky.logger.exception("Couldn't handle %s", line[:300])
        finally:
            child.stdout_done.set()

    async def _read_stderr(self, child: _Child) -> None:
        stream = child.proc.stderr
        logged = 0
        try:
            while stream is not None:
                try:
                    raw = await stream.readline()
                except ValueError:
                    continue
                if not raw:
                    break
                line = raw.decode("utf-8", "replace").rstrip()
                if not line.strip():
                    continue
                child.note_stderr(line)
                if logged < 200:  # its log, within reason
                    logged += 1
                    decky.logger.info("RemotePlayWhatever: %s", line[:500])
        finally:
            child.stderr_done.set()

    async def _watch(self, child: _Child) -> None:
        code = await child.proc.wait()
        try:
            # Its last words, on stdout and stderr, say why it stopped.
            await asyncio.wait_for(asyncio.gather(child.stdout_done.wait(), child.stderr_done.wait()), 2.0)
        except asyncio.TimeoutError:
            pass
        try:
            if self._on_exit(child, code):
                await self._spawn()
        finally:
            child.exited.set()

    def _on_exit(self, child: _Child, code: Optional[int]) -> bool:
        """Records why it stopped and restarts it if that may help. True: start another AppImage now."""
        if child is not self._child:
            return False
        self._child = None
        for _, answer, _ in self._waiters:
            if not answer.done():
                answer.set_result(None)
        self._waiters = []
        if child.ready_at > 0 and time.monotonic() - child.ready_at >= STABLE_AFTER:
            self._restart_count = 0
        if child.stopping or self._unloading:
            decky.logger.info("RemotePlayWhatever stopped (exit code %s)", code)
            self._exit = None
            self._changed()
            return False
        too_old = _predates_serve(child)
        if too_old:
            reason, error = TOO_OLD_MESSAGE, TOO_OLD
        else:
            reason = _exit_reason(child, code)
            error = _text((child.error or {}).get("error"))
        self._exit = _exit_info(code, reason, error, list(child.stderr)[-5:])
        decky.logger.warning("RemotePlayWhatever exited (code %s): %s", code, reason)
        if code == 0 and child.saw_bye:
            self._exit = None  # it said goodbye: nothing went wrong
        elif too_old:
            # A build from before --serve can't work with the plugin: it isn't run again until the file changes.
            key = _file_key(child.binary)
            if key is not None:
                self._too_old.add(key)
            self._gave_up = True
            self._fatal = key
            other = self._find_binary(fresh=True)
            if other and not self._known_bad(other):
                decky.logger.info("%s is too old for the plugin; trying %s", child.binary, other)
                self._exit = None
                self._gave_up = False
                self._changed()
                return True
        elif error == "methods-missing":
            # This Steam lacks what the app needs: restarting won't help until the AppImage changes.
            self._gave_up = True
            self._fatal = _file_key(child.binary)
        else:
            self._schedule_restart()
        self._changed()
        return False

    def _schedule_restart(self) -> None:
        if self._unloading or self._restart_task is not None:
            return
        if self._restart_count >= len(self.restart_delays):
            self._gave_up = True
            decky.logger.warning("RemotePlayWhatever keeps stopping; not restarting it again")
            return
        delay = float(self.restart_delays[self._restart_count])
        self._restart_count += 1
        self._restart_at = time.time() + delay
        decky.logger.info(
            "Restarting RemotePlayWhatever in %.0f s (%d of %d)", delay, self._restart_count, len(self.restart_delays)
        )
        self._restart_task = self._task(self._restart_after(delay))

    async def _restart_after(self, delay: float) -> None:
        await asyncio.sleep(delay)
        self._restart_task = None
        self._restart_at = None
        if not self._unloading and not self._running():
            await self._spawn()
        self._changed()

    def _cancel_restart(self) -> None:
        task, self._restart_task, self._restart_at = self._restart_task, None, None
        if task is not None and not task.done():
            task.cancel()

    async def _shutdown(self) -> None:
        """Asks the app to quit (the session goes on) and kills it if it hasn't within quit_timeout."""
        child = self._child
        if child is None:
            return
        child.stopping = True
        if child.alive:
            await self._write("quit", child)
            try:
                await asyncio.wait_for(child.proc.wait(), self.quit_timeout)
            except asyncio.TimeoutError:
                decky.logger.warning("RemotePlayWhatever didn't quit within %.0f s; killing it", self.quit_timeout)
                _kill(child)
                try:
                    await asyncio.wait_for(child.proc.wait(), 2.0)
                except asyncio.TimeoutError:
                    pass
        try:
            await asyncio.wait_for(child.exited.wait(), 3.0)
        except asyncio.TimeoutError:
            pass

    async def _write(self, line: str, child: Optional[_Child] = None) -> bool:
        child = child or self._child
        if child is None or not child.alive or child.proc.stdin is None:
            return False
        try:
            child.proc.stdin.write((line + "\n").encode("utf-8"))
            await asyncio.wait_for(child.proc.stdin.drain(), 2.0)
            return True
        except (OSError, RuntimeError, asyncio.TimeoutError) as e:
            decky.logger.warning("Couldn't send %s: %s", line.split(" ", 1)[0], e)
            return False

    # Commands and their answers

    async def _ask(
        self,
        line: str,
        command: str,
        events: Tuple[str, ...],
        also: Optional[Callable[[Event], bool]] = None,
    ) -> Tuple[Event, Optional[Result]]:
        """Sends a command and waits for its answer: (event, None), or ({}, a failed result)."""
        problem = await self._need_ready()
        if problem is not None:
            return {}, problem
        answer = self._expect(_matcher(command, events, also))
        if not await self._write(line):
            self._forget(answer)
            return {}, self._result(False, "not-running", "Remote Play Whatever isn't running.")
        try:
            event = await asyncio.wait_for(answer, self.command_timeout)
        except asyncio.TimeoutError:
            return {}, self._result(False, "timeout", "Remote Play Whatever didn't answer.")
        if event is None:
            message = self._exit_message() or "Remote Play Whatever stopped."
            return {}, self._result(False, "stopped", message)
        if event.get("event") == "error":
            message = _text(event.get("message")) or "Remote Play Whatever couldn't do that."
            code = _text(event.get("error")) or "error"
            return {}, self._result(False, code, message, toasted=command in TOASTED_COMMANDS)
        return event, None

    async def _need_ready(self) -> Optional[Result]:
        if self._running() and not self._ready():
            await self._wait_ready(self.ready_timeout)
        if self._ready():
            return None
        if self._running():
            return self._result(False, "not-ready", "Remote Play Whatever is still starting.")
        return self._result(False, "not-running", self._exit_message() or "Remote Play Whatever isn't running.")

    async def _wait_ready(self, timeout: float) -> None:
        deadline = time.monotonic() + timeout
        child = self._child
        while child is not None and not child.ready and time.monotonic() < deadline:
            if child.alive:
                await asyncio.sleep(0.05)
                continue
            # _watch sets exited once _on_exit has recorded why it stopped, so the caller can say,
            # and, if it was too old, once the AppImage found next has started in its place.
            try:
                await asyncio.wait_for(child.exited.wait(), max(0.1, deadline - time.monotonic()))
            except asyncio.TimeoutError:
                return
            if self._child is child:
                return
            child = self._child

    def _expect(self, match: Callable[[Event], bool]) -> "asyncio.Future[Optional[Event]]":
        """Registers for the first event that matches; answers come in the order commands went."""
        answer: "asyncio.Future[Optional[Event]]" = asyncio.get_running_loop().create_future()
        self._waiters.append((match, answer, time.monotonic() + 120.0))
        return answer

    def _forget(self, answer: "asyncio.Future[Optional[Event]]") -> None:
        self._waiters = [w for w in self._waiters if w[1] is not answer]
        if not answer.done():
            answer.cancel()

    def _answer(self, event: Event) -> None:
        now = time.monotonic()
        waiting = []
        matched = False
        for entry in self._waiters:
            match, answer, expires = entry
            if answer.done():
                continue
            if expires < now:
                answer.cancel()
                continue
            if not matched:
                try:
                    matched = bool(match(event))
                except Exception:  # noqa: BLE001
                    matched = False
                if matched:
                    answer.set_result(event)
                    continue
            waiting.append(entry)
        self._waiters = waiting

    async def _request_status(self) -> "Optional[asyncio.Future[Optional[Event]]]":
        child = self._child
        if child is None or not child.ready:
            return None
        self._status_sent = time.monotonic()
        answer = self._expect(_matcher("status", ("status",)))
        if not await self._write("status", child):
            self._forget(answer)
            return None
        return answer

    # Events from the app

    async def _handle(self, child: _Child, event: Event) -> None:
        if child is not self._child:
            return
        kind = event["event"]
        if kind not in ("status", "friends"):
            self._events.append(dict(event, at=time.time()))
            decky.logger.info("Event: %s", json.dumps(event)[:300])
        handler = getattr(self, "_on_" + kind.replace("-", "_"), None)
        if handler is not None:
            await handler(child, event)
        self._answer(event)

    async def _on_ready(self, child: _Child, event: Event) -> None:
        child.ready_at = time.monotonic()
        self._me = {"steamid": _text(event.get("steamid")), "name": _text(event.get("name"))}
        self._build = event.get("build")
        if event.get("protocol") != PROTOCOL:
            decky.logger.warning(
                "RemotePlayWhatever speaks protocol %r; this plugin knows %d", event.get("protocol"), PROTOCOL
            )
        self._error = None
        self._exit = None
        self._gave_up = False
        self._fatal = None
        self._changed()
        await self._request_status()

    async def _on_status(self, child: _Child, event: Event) -> None:
        self._status_at = time.monotonic()
        status = _clean_status(event)
        if status != self._status or status["link"] != self._link:
            self._status = status
            self._link = status["link"]
            self._changed()

    async def _on_invite(self, child: _Child, event: Event) -> None:
        sid = _text(event.get("steamid"))
        ok = event.get("result") == "ok"
        message = _text(event.get("message"))
        if sid == "0":
            if ok and _text(event.get("url")):
                self._link = _text(event.get("url"))
                self._changed()
            elif not ok:
                await self._toast("No guest link", message or "Steam didn't make a guest link.")
        else:
            name = self._name_of(sid)
            if ok:
                await self._toast("Invited " + name if name else "Invite sent", "It's in their Steam chat.")
            else:
                await self._toast("Invite failed", message or "Steam didn't send the invite.")
        await self._request_status()

    async def _on_joined(self, child: _Child, event: Event) -> None:
        await self._toast(_guest_name(event) + " joined", "Remote Play Together")
        await self._request_status()

    async def _on_left(self, child: _Child, event: Event) -> None:
        await self._toast(_guest_name(event) + " left", "Remote Play Together")
        await self._request_status()

    async def _on_notice(self, child: _Child, event: Event) -> None:
        await self._toast("Remote Play Whatever", _text(event.get("message")))
        await self._request_status()

    async def _on_group_failed(self, child: _Child, event: Event) -> None:
        message = _text(event.get("message")) or "Steam made no Remote Play group for this game."
        await self._toast("Guests may only watch", message)

    async def _on_friends(self, child: _Child, event: Event) -> None:
        self._friends = _clean_friends(event.get("friends"))

    async def _on_ended(self, child: _Child, event: Event) -> None:
        await self._toast("Session ended", "Everyone was disconnected.")
        await self._request_status()

    async def _on_bye(self, child: _Child, event: Event) -> None:
        child.saw_bye = True

    async def _on_error(self, child: _Child, event: Event) -> None:
        info = {
            "error": _text(event.get("error")),
            "message": _text(event.get("message")),
            "command": _text(event.get("command")),
            "at": time.time(),
        }
        if info["command"]:
            # The answer to a command; the method that sent it reports it.
            if info["command"] in TOASTED_COMMANDS:
                await self._toast("Remote Play Whatever", info["message"] or "That didn't work.")
            return
        child.error = info
        self._error = info
        self._changed()

    def _name_of(self, steamid: str) -> str:
        for friend in self._friends:
            if friend["steamid"] == steamid:
                return friend["name"]
        for entry in (self._status or {}).get("waiting", []):
            if entry["steamid"] == steamid and entry["name"]:
                return entry["name"]
        return ""

    # Talking to the panel

    async def _toast(self, title: str, body: str) -> None:
        await self._emit("rpw_toast", {"title": title, "body": body})

    async def _emit(self, event: str, *args: Any) -> None:
        try:
            await decky.emit(event, *args)
        except Exception as e:  # noqa: BLE001 - the panel may be gone
            decky.logger.debug("Couldn't send %s to the panel: %s", event, e)

    def _changed(self) -> None:
        """Sends the panel the new state, once per burst of changes."""
        self._seq += 1
        if self._emit_pending:
            return
        try:
            asyncio.get_running_loop()
        except RuntimeError:
            return
        self._emit_pending = True
        self._task(self._send_state())

    async def _send_state(self) -> None:
        try:
            await asyncio.sleep(0.05)
        finally:
            self._emit_pending = False
        await self._emit("rpw_state", self._snapshot())

    def _snapshot(self) -> Result:
        child = self._child
        running = child is not None and child.alive
        binary = child.binary if child is not None and running else self._find_binary()
        return {
            "instance": self._instance,
            "seq": self._seq,
            "version": _version(),
            "installed": bool(binary),
            "binary": binary or "",
            "running": running,
            "ready": bool(child is not None and child.ready),
            "starting": bool(child is not None and running and not child.ready),
            "me": self._me,
            "build": self._build,
            "status": self._status,
            "link": self._link,
            "error": self._error,
            "exit": self._exit,
            "restart_at": self._restart_at,
            "gave_up": self._gave_up,
            "too_old": not running and self._is_too_old(binary),
            "download": dict(self._download),
            "settings": self._public_settings(),
            "events": list(self._events)[-10:],
        }

    def _result(self, ok: bool, error: str = "", message: str = "", toasted: bool = False, **extra: Any) -> Result:
        result: Result = {"ok": ok, "error": error, "message": message, "toasted": toasted}
        result.update(extra)
        result["state"] = self._snapshot()
        return result

    def _exit_message(self) -> str:
        if self._exit and self._exit.get("reason"):
            return _text(self._exit["reason"])
        if self._error and self._error.get("message"):
            return _text(self._error["message"])
        return ""

    def _task(self, coro: Awaitable[Any]) -> "asyncio.Task[Any]":
        task = asyncio.ensure_future(coro)
        self._tasks.add(task)
        task.add_done_callback(self._task_done)
        return task

    def _task_done(self, task: "asyncio.Task[Any]") -> None:
        self._tasks.discard(task)
        if not task.cancelled() and task.exception() is not None:
            decky.logger.error("A background task failed", exc_info=task.exception())

    # Settings and the AppImage

    def _user(self) -> str:
        return _text(getattr(decky, "DECKY_USER", "")) or os.environ.get("USER") or "deck"

    def _home(self) -> str:
        return _text(getattr(decky, "DECKY_USER_HOME", "")) or os.path.expanduser("~")

    def _uid(self) -> int:
        if pwd is not None:
            try:
                return pwd.getpwnam(self._user()).pw_uid
            except KeyError:
                pass
        return os.getuid() if hasattr(os, "getuid") else 1000

    def _settings_file(self) -> str:
        return os.path.join(_text(decky.DECKY_PLUGIN_SETTINGS_DIR), "settings.json")

    def _load_settings(self) -> Dict[str, Any]:
        path = self._settings_file()
        stamp = _mtime_ns(path)
        if self._settings is not None and stamp == self._settings_stamp:
            return self._settings
        data: Any = {}
        try:
            with open(path, "r", encoding="utf-8") as f:
                data = json.load(f)
        except FileNotFoundError:
            pass
        except (OSError, ValueError) as e:
            decky.logger.warning("Couldn't read the settings: %s", e)
        settings = dict(DEFAULT_SETTINGS)
        if isinstance(data, dict):
            for key, default in DEFAULT_SETTINGS.items():
                if isinstance(data.get(key), type(default)):
                    settings[key] = data[key]
        if self._settings is not None and settings != self._settings:
            decky.logger.info("settings.json changed: %s", json.dumps(settings))
            self._binary_cache = (float("-inf"), None)
        self._settings = settings
        self._settings_stamp = stamp
        return settings

    def _save_settings(self, settings: Dict[str, Any]) -> bool:
        self._settings = dict(settings)
        path = self._settings_file()
        try:
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path + ".tmp", "w", encoding="utf-8") as f:
                json.dump(self._settings, f, indent=2)
                f.write("\n")
            os.replace(path + ".tmp", path)
            return True
        except OSError as e:
            decky.logger.error("Couldn't save the settings: %s", e)
            return False
        finally:
            self._settings_stamp = _mtime_ns(path)

    def _public_settings(self) -> Dict[str, Any]:
        settings = self._load_settings()
        return {
            "appimage_path": settings["appimage_path"],
            "autostart": settings["autostart"],
            "download_url": settings["download_url"],
        }

    def _find_binary(self, fresh: bool = False) -> Optional[str]:
        now = time.monotonic()
        if not fresh and now - self._binary_cache[0] < 5.0:
            return self._binary_cache[1]
        found = self._search_binary()
        self._binary_cache = (now, found)
        return found

    def _search_binary(self) -> Optional[str]:
        """The first AppImage that isn't too old for the plugin; if they all are, the first one, to say so."""
        first: Optional[str] = None
        for path in self._candidates():
            if not self._is_too_old(path):
                return path
            if first is None:
                first = path
        return first

    def _candidates(self) -> Iterator[str]:
        """In order: the saved path, ~/Applications, ~/Downloads, ~/Desktop, the plugin's bin/, its data folder."""
        if self.binary_override:
            if os.path.isfile(self.binary_override):
                yield self.binary_override
            return
        saved = self._load_settings()["appimage_path"]
        if saved:
            saved = os.path.expanduser(saved)
            if os.path.isfile(saved):
                yield saved
            else:
                decky.logger.debug("The saved AppImage %s is gone; searching", saved)
        home = self._home()
        for folder in SEARCH_FOLDERS:
            yield from _appimages(os.path.join(home, folder))
        plugin_dir = _text(getattr(decky, "DECKY_PLUGIN_DIR", ""))
        if plugin_dir:
            yield from _appimages(os.path.join(plugin_dir, "bin"), any_extension=True)
        # Where download_appimage() puts it, in case settings.json was lost.
        yield from _appimages(_text(getattr(decky, "DECKY_PLUGIN_RUNTIME_DIR", "")))

    def _is_too_old(self, path: Optional[str]) -> bool:
        """It's from before --serve (it ran and said so), and hasn't changed since."""
        if not path:
            return False
        return _file_key(path) in self._too_old

    def _known_bad(self, path: str) -> bool:
        """Running it again won't help: too old for the plugin, or lacking methods in this Steam."""
        key = _file_key(path)
        return key is not None and (key == self._fatal or key in self._too_old)

    def _fetch(self, url: str, dest: str, loop: asyncio.AbstractEventLoop) -> int:
        """Downloads url to dest (in a worker thread). Returns its size."""
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        part = dest + ".part"
        request = urllib.request.Request(url, headers={"User-Agent": "RemotePlayWhatever-Decky/" + _version()})
        received = 0
        head = b""
        try:
            with urllib.request.urlopen(request, timeout=30, context=_ssl_context()) as response:
                total = int(response.headers.get("Content-Length") or 0)
                with open(part, "wb") as out:
                    while True:
                        if self._download_cancel:
                            raise RuntimeError("The download was cancelled.")
                        chunk = response.read(256 * 1024)
                        if not chunk:
                            break
                        if len(head) < 4:
                            head += chunk[: 4 - len(head)]
                        out.write(chunk)
                        received += len(chunk)
                        loop.call_soon_threadsafe(self._download_progress, received, total)
            if total and received != total:
                raise OSError("The download stopped after {} of {} bytes.".format(received, total))
            if head != b"\x7fELF":
                raise ValueError("That download isn't an AppImage.")
            os.chmod(part, 0o755)
            os.replace(part, dest)
            self._give_to_user(dest)
            return received
        except BaseException:
            try:
                os.remove(part)
            except OSError:
                pass
            raise

    def _download_progress(self, received: int, total: int) -> None:
        if not self._download["active"]:
            return
        self._download.update(received=received, total=total, percent=int(received * 100 / total) if total else 0)
        now = time.monotonic()
        if now - self._download_emitted >= 0.5:
            self._download_emitted = now
            self._changed()

    def _give_to_user(self, path: str) -> None:
        if pwd is None or not hasattr(os, "geteuid") or os.geteuid() != 0:
            return
        try:
            entry = pwd.getpwnam(self._user())
            os.chown(path, entry.pw_uid, entry.pw_gid)
        except (KeyError, OSError) as e:
            decky.logger.warning("Couldn't give %s to %s: %s", path, self._user(), e)


# Helpers


def _version() -> str:
    return _text(getattr(decky, "DECKY_PLUGIN_VERSION", "")) or "dev"


def _text(value: Any) -> str:
    if isinstance(value, str):
        return value
    return "" if value is None else str(value)


def _steamid(value: Any) -> Optional[str]:
    text = _text(value).strip()
    return text if text.isascii() and text.isdigit() and len(text) <= 20 else None


def _int(value: Any) -> Optional[int]:
    if isinstance(value, bool):
        return None
    if isinstance(value, int):
        return value
    if isinstance(value, float) and value.is_integer():
        return int(value)
    text = _text(value).strip()
    return int(text) if text.isascii() and text.isdigit() else None


def _token(value: Any) -> Optional[str]:
    """A guest's key: one printable word."""
    text = _text(value).strip()
    if not text or len(text) > 128 or not text.isprintable() or any(c.isspace() for c in text):
        return None
    return text


def _flag(value: Any) -> str:
    return "1" if value is True or value == 1 or _text(value).strip().lower() in ("1", "true", "yes", "on") else "0"


def _guest_name(event: Event) -> str:
    return _text(event.get("name")).strip() or "A guest"


def _matcher(command: str, events: Tuple[str, ...], also: Optional[Callable[[Event], bool]] = None):
    def match(event: Event) -> bool:
        kind = event.get("event")
        if kind == "error":
            return event.get("command") == command
        return kind in events and (also is None or also(event))

    return match


def _clean_status(event: Event) -> Dict[str, Any]:
    game = event.get("game")
    if isinstance(game, dict):
        game = {
            "id": _text(game.get("id")),
            "name": _text(game.get("name")) or "your game",
            "steam": bool(game.get("steam")),
        }
    else:
        game = None
    guests = []
    for guest in event.get("guests") or []:
        if isinstance(guest, dict):
            guests.append(
                {
                    "key": _text(guest.get("key")),
                    "steamid": _text(guest.get("steamid")),
                    "name": _text(guest.get("name")),
                    "keyboard": bool(guest.get("keyboard")),
                    "mouse": bool(guest.get("mouse")),
                    "controller": bool(guest.get("controller")),
                    "joined": _int(guest.get("joined")) or 0,
                }
            )
    waiting = []
    for entry in event.get("waiting") or []:
        if isinstance(entry, dict):
            waiting.append(
                {
                    "steamid": _text(entry.get("steamid")) or "0",
                    "guest": _int(entry.get("guest")) or 0,
                    "name": _text(entry.get("name")),
                    "link": _text(entry.get("link")),
                }
            )
    return {
        "game": game,
        "screen": bool(event.get("screen")),
        "session": bool(event.get("session")),
        "link": _text(event.get("link")),
        "guests": guests,
        "waiting": waiting,
    }


def _clean_friends(raw: Any) -> List[Dict[str, str]]:
    friends = []
    for friend in raw if isinstance(raw, list) else []:
        if not isinstance(friend, dict):
            continue
        sid = _steamid(friend.get("steamid"))
        if not sid:
            continue
        friends.append(
            {
                "steamid": sid,
                "name": _text(friend.get("name")) or sid,
                "state": _text(friend.get("state")).lower() or "offline",
                "game": _text(friend.get("game")),
            }
        )
    friends.sort(key=lambda f: (f["state"] == "offline", f["name"].casefold()))
    return friends


def _exit_info(code: Optional[int], reason: str, error: str = "", stderr: Optional[List[str]] = None) -> Dict[str, Any]:
    return {"code": code, "reason": reason, "error": error, "stderr": stderr or [], "at": time.time()}


def _exit_reason(child: _Child, code: Optional[int]) -> str:
    if child.error and child.error.get("message"):
        return _text(child.error["message"])
    for line in reversed(child.stderr):
        if line.strip():
            return line.strip()[:300]
    if code is not None and code < 0:
        return "Remote Play Whatever was stopped by signal {}.".format(-code)
    return "Remote Play Whatever closed (exit code {}).".format(code)


def _download_state(active: bool = False, done: bool = False, error: str = "", received: int = 0, total: int = 0):
    percent = 100 if done else 0
    return {"active": active, "done": done, "error": error, "received": received, "total": total, "percent": percent}


def _download_error(e: BaseException) -> str:
    if isinstance(e, urllib.error.HTTPError):
        return "The server answered {} {}.".format(e.code, e.reason)
    if isinstance(e, urllib.error.URLError):
        return "Couldn't reach the server: {}.".format(e.reason)
    return str(e) or e.__class__.__name__


def _ssl_context() -> ssl.SSLContext:
    """The default trust store, plus certifi's and the system's (Decky's bundled Python may lack one)."""
    context = ssl.create_default_context()
    files = list(CA_FILES)
    try:
        import certifi  # type: ignore[import-not-found]

        files.insert(0, certifi.where())
    except Exception:  # noqa: BLE001 - optional
        pass
    for path in files:
        if os.path.isfile(path):
            try:
                context.load_verify_locations(cafile=path)
            except (OSError, ssl.SSLError):
                pass
    return context


def _appimages(folder: str, any_extension: bool = False) -> List[str]:
    """The RemotePlayWhatever*.AppImage files in folder, newest first (any RemotePlayWhatever* with any_extension)."""
    if not folder:
        return []
    try:
        names = os.listdir(folder)
    except OSError:
        return []
    found: List[Tuple[float, str]] = []
    for name in names:
        lower = name.lower()
        if not lower.startswith("remoteplaywhatever") or lower.endswith(".part"):
            continue
        if not any_extension and not lower.endswith(".appimage"):
            continue
        path = os.path.join(folder, name)
        try:
            info = os.stat(path)
        except OSError:
            continue
        if stat.S_ISREG(info.st_mode):
            found.append((info.st_mtime, path))
    found.sort(key=lambda item: item[0], reverse=True)
    return [path for _, path in found]


def _predates_serve(child: _Child) -> bool:
    """Whether it's a build from before --serve: it stopped before "ready", rejecting the option.

    wxWidgets says "Unknown long option 'serve'" and prints the usage text. A newer
    build never prints usage text in --serve mode: its problems are "error" events.
    """
    if child.ready_at > 0:
        return False
    return child.unknown_serve or (child.error is None and child.usage)


def _make_executable(path: str) -> Optional[str]:
    """chmod +x, like a file manager's "Allow executing"; returns a problem, or None."""
    if os.name != "posix" or os.access(path, os.X_OK):
        return None
    try:
        mode = os.stat(path).st_mode
        os.chmod(path, mode | ((mode & 0o444) >> 2))
        return None
    except OSError as e:
        return "{} isn't executable and couldn't be made executable: {}.".format(path, e.strerror or e)


def _file_key(path: str) -> Optional[Tuple[str, float, int]]:
    try:
        info = os.stat(path)
    except OSError:
        return None
    return (path, info.st_mtime, info.st_size)


def _mtime_ns(path: str) -> Optional[int]:
    try:
        return os.stat(path).st_mtime_ns
    except OSError:
        return None


def _steam_env(uid: int) -> Dict[str, str]:
    """DISPLAY, XAUTHORITY and the locale from the user's running Steam client."""
    try:
        pids = [p for p in os.listdir("/proc") if p.isdigit()]
    except OSError:
        return {}
    for pid in pids:
        try:
            if os.stat("/proc/" + pid).st_uid != uid:
                continue
            with open("/proc/" + pid + "/comm", "r", encoding="utf-8", errors="replace") as f:
                if f.read().strip() != "steam":
                    continue
            with open("/proc/" + pid + "/environ", "rb") as f:
                raw = f.read()
        except OSError:
            continue
        found = {}
        for item in raw.split(b"\0"):
            key, _, value = item.decode("utf-8", "replace").partition("=")
            if key in SESSION_ENV and value:
                found[key] = value
        if found.get("DISPLAY"):
            return found
    return {}


def _kill(child: _Child) -> None:
    if child.own_group and hasattr(os, "killpg"):
        try:
            os.killpg(child.proc.pid, signal.SIGKILL)  # type: ignore[attr-defined]
            return
        except OSError:
            pass
    try:
        child.proc.kill()
    except (OSError, ProcessLookupError):
        pass
