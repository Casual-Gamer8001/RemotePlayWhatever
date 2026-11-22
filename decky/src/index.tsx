import {
  ButtonItem,
  ConfirmModal,
  DialogButton,
  Field,
  Focusable,
  PanelSection,
  PanelSectionRow,
  ProgressBar,
  ToggleField,
  showModal,
  staticClasses,
} from "@decky/ui";
import {
  addEventListener,
  callable,
  definePlugin,
  removeEventListener,
  toaster,
  useQuickAccessVisible,
} from "@decky/api";
import { useEffect, useMemo, useState } from "react";
import { FaUserFriends } from "react-icons/fa";
import qrcode from "qrcode-generator";

// What the backend (main.py) sends. SteamIDs are strings.

interface Game {
  id: string;
  name: string;
  steam: boolean;
}

interface Guest {
  key: string;
  steamid: string; // "0": joined through a guest link
  name: string;
  keyboard: boolean;
  mouse: boolean;
  controller: boolean;
  joined: number; // Unix time
}

interface Waiting {
  steamid: string; // "0": a guest link
  guest: number;
  name: string;
  link: string;
}

interface Status {
  game: Game | null;
  screen: boolean;
  session: boolean;
  link: string;
  guests: Guest[];
  waiting: Waiting[];
}

interface Download {
  active: boolean;
  done: boolean;
  error: string;
  received: number;
  total: number;
  percent: number;
}

interface Settings {
  appimage_path: string;
  autostart: boolean;
  download_url: string;
}

interface State {
  instance: string;
  seq: number;
  version: string;
  installed: boolean;
  binary: string;
  running: boolean;
  ready: boolean;
  starting: boolean;
  build: number | null;
  status: Status | null;
  link: string;
  exit: { code: number | null; reason: string; error: string } | null;
  restart_at: number | null;
  gave_up: boolean;
  too_old: boolean; // the AppImage found is from before the plugin: it needs an update
  download: Download;
  settings: Settings;
}

interface Friend {
  steamid: string;
  name: string;
  state: string;
  game: string;
}

interface Result {
  ok: boolean;
  error: string;
  message: string;
  toasted: boolean; // the backend has shown the problem already
  state?: State;
}

interface FriendsResult extends Result {
  friends?: Friend[];
}

interface Toast {
  title: string;
  body: string;
}

const getState = callable<[], State>("get_state");
const start = callable<[], Result>("start");
const stop = callable<[], Result>("stop");
const createLink = callable<[], Result>("create_link");
const invite = callable<[steamid: string], Result>("invite");
const cancel = callable<[steamid: string, guest: number], Result>("cancel");
const setInput = callable<[key: string, keyboard: boolean, mouse: boolean, controller: boolean], Result>(
  "set_input",
);
const removeGuest = callable<[key: string], Result>("remove_guest");
const endSession = callable<[], Result>("end_session");
const getFriends = callable<[], FriendsResult>("get_friends");
const setSettings = callable<[settings: Partial<Settings>], Result>("set_settings");
const downloadAppImage = callable<[], Result>("download_appimage");

// One state for every component: from polls, from the answers to commands and
// from the backend's "rpw_state" events, whichever is newest.

let latest: State | null = null;
const subscribers = new Set<(state: State) => void>();

function publish(state: State | null | undefined) {
  if (!state) return;
  if (latest && latest.instance === state.instance && state.seq < latest.seq) return;
  latest = state;
  subscribers.forEach((notify) => notify(state));
}

async function refresh() {
  try {
    publish(await getState());
  } catch (e) {
    console.error("Remote Play Whatever:", e);
  }
}

// Older Decky Loaders lack useQuickAccessVisible: the panel then polls whenever it's shown.
const useVisible: () => boolean = typeof useQuickAccessVisible === "function" ? useQuickAccessVisible : () => true;

function useRpwState(): State | null {
  const [state, setState] = useState<State | null>(latest);
  const visible = useVisible();
  useEffect(() => {
    subscribers.add(setState);
    return () => {
      subscribers.delete(setState);
    };
  }, []);
  useEffect(() => {
    if (!visible) return undefined;
    refresh();
    const timer = window.setInterval(refresh, 2000);
    return () => window.clearInterval(timer);
  }, [visible]);
  return state;
}

function toast(title: string, body = "") {
  toaster.toast({ title, body });
}

/** Runs a backend call and shows its problem, unless the backend has. */
async function act(call: () => Promise<Result>): Promise<Result | null> {
  try {
    const result = await call();
    publish(result.state);
    if (!result.ok && !result.toasted) toast("Remote Play Whatever", result.message || "That didn't work.");
    return result;
  } catch (e) {
    toast("Remote Play Whatever", String(e));
    return null;
  }
}

function useBusy(): [boolean, (call: () => Promise<Result>) => Promise<void>] {
  const [busy, setBusy] = useState(false);
  const run = async (call: () => Promise<Result>) => {
    setBusy(true);
    try {
      await act(call);
    } finally {
      setBusy(false);
    }
  };
  return [busy, run];
}

function megabytes(bytes: number) {
  return `${(bytes / 1048576).toFixed(1)} MB`;
}

/** Why it stopped; a too-old AppImage's reason no longer counts once another AppImage is the one found. */
function exitReason(state: State): string {
  if (!state.exit || (state.exit.error === "too-old" && !state.too_old)) return "";
  return state.exit.reason;
}

function describe(state: State): { title: string; detail: string } {
  const download = state.download;
  if (download.active) {
    const done = download.total
      ? `${download.percent}% of ${megabytes(download.total)}`
      : megabytes(download.received);
    return { title: "Downloading Remote Play Whatever…", detail: done };
  }
  if (!state.installed) {
    return {
      title: "Remote Play Whatever isn't installed",
      detail: download.error
        ? `The download failed: ${download.error}`
        : "Download it, or put its AppImage in ~/Applications.",
    };
  }
  if (state.too_old && !state.running) {
    const update = "Select Update to get the newest one.";
    return {
      title: "This Remote Play Whatever is too old for the plugin.",
      detail: download.error ? `${update} The last download failed: ${download.error}` : update,
    };
  }
  const reason = exitReason(state);
  if (!state.running) {
    if (state.restart_at) return { title: "Restarting…", detail: reason };
    if (reason) return { title: "Remote Play Whatever stopped", detail: reason };
    return { title: "Remote Play Whatever is off", detail: "" };
  }
  if (!state.ready || !state.status) return { title: "Starting…", detail: reason };
  const { game, session, screen, guests } = state.status;
  if (session) {
    const count = guests.length;
    const parts = [game?.name ?? "", screen ? "Your whole screen is shared" : ""].filter(Boolean);
    return {
      title: count ? `Sharing · ${count === 1 ? "1 guest" : `${count} guests`}` : "Sharing · no guests yet",
      detail: parts.join(" · "),
    };
  }
  if (!game) return { title: "No game running: start one from your library", detail: "" };
  return {
    title: `Ready to share: ${game.name}`,
    detail: game.steam ? "" : "Steam shares your whole screen for non-Steam games.",
  };
}

function StatusSection({ state }: { state: State }) {
  const [busy, run] = useBusy();
  const { title, detail } = describe(state);
  const download = state.download;
  return (
    <PanelSection>
      <PanelSectionRow>
        <Field label={title} description={detail || undefined} bottomSeparator="none" />
      </PanelSectionRow>
      {download.active && (
        <PanelSectionRow>
          <ProgressBar nProgress={download.percent} indeterminate={!download.total} />
        </PanelSectionRow>
      )}
      {!state.installed && !download.active && (
        <PanelSectionRow>
          <ButtonItem layout="below" disabled={busy} onClick={() => run(downloadAppImage)}>
            Download
          </ButtonItem>
        </PanelSectionRow>
      )}
      {state.installed && state.too_old && !state.running && !download.active && (
        <PanelSectionRow>
          <ButtonItem layout="below" disabled={busy} onClick={() => run(downloadAppImage)}>
            Update
          </ButtonItem>
        </PanelSectionRow>
      )}
      {state.installed && !state.too_old && !state.running && !download.active && (
        <PanelSectionRow>
          <ButtonItem layout="below" disabled={busy} onClick={() => run(start)}>
            {busy ? "Starting…" : exitReason(state) ? "Try again" : "Start"}
          </ButtonItem>
        </PanelSectionRow>
      )}
    </PanelSection>
  );
}

/** A QR code as an SVG: black squares on white, with the standard 4-module margin. */
function QrCode({ text }: { text: string }) {
  const code = useMemo(() => {
    try {
      const qr = qrcode(0, "M");
      qr.addData(text);
      qr.make();
      const count = qr.getModuleCount();
      const margin = 4;
      let path = "";
      for (let row = 0; row < count; row++) {
        for (let col = 0; col < count; ) {
          if (!qr.isDark(row, col)) {
            col++;
            continue;
          }
          let end = col;
          while (end < count && qr.isDark(row, end)) end++;
          path += `M${col + margin} ${row + margin}h${end - col}v1h${col - end}z`;
          col = end;
        }
      }
      return { size: count + 2 * margin, path };
    } catch (e) {
      console.error("Remote Play Whatever: no QR code", e);
      return null;
    }
  }, [text]);
  if (!code) return null;
  return (
    <div style={{ display: "flex", justifyContent: "center", padding: "6px 0" }}>
      <svg
        width={184}
        height={184}
        viewBox={`0 0 ${code.size} ${code.size}`}
        shapeRendering="crispEdges"
        role="img"
        aria-label="QR code of the guest link"
      >
        <rect width={code.size} height={code.size} fill="#ffffff" />
        <path d={code.path} fill="#000000" />
      </svg>
    </div>
  );
}

/** Copies text in the window that was clicked: the Quick Access Menu's, which has the focus. */
async function copyText(text: string, event?: Event): Promise<boolean> {
  const node = (event?.currentTarget ?? event?.target) as Node | null | undefined;
  const doc = node?.ownerDocument ?? document;
  const view = doc.defaultView ?? window;
  try {
    await view.navigator.clipboard.writeText(text);
    return true;
  } catch {
    // Try the old way.
  }
  const area = doc.createElement("textarea");
  area.value = text;
  area.setAttribute("readonly", "");
  area.style.position = "fixed";
  area.style.opacity = "0";
  doc.body.appendChild(area);
  area.select();
  let copied = false;
  try {
    copied = doc.execCommand("copy");
  } catch {
    copied = false;
  }
  area.remove();
  return copied;
}

function LinkSection({ link }: { link: string }) {
  const [busy, run] = useBusy();
  const copy = async (event: MouseEvent) => {
    const copied = await copyText(link, event);
    if (copied) toast("Link copied", link);
    else toast("Couldn't copy the link", "Scan the QR code instead, or type the link.");
  };
  if (!link) {
    return (
      <PanelSection title="Guest link">
        <PanelSectionRow>
          <ButtonItem
            layout="below"
            disabled={busy}
            onClick={() => run(createLink)}
            description="Anyone can join with it, through the free Steam Link app."
          >
            {busy ? "Creating…" : "Create guest link"}
          </ButtonItem>
        </PanelSectionRow>
      </PanelSection>
    );
  }
  return (
    <PanelSection title="Guest link">
      <PanelSectionRow>
        <QrCode text={link} />
      </PanelSectionRow>
      <PanelSectionRow>
        <Field
          focusable
          bottomSeparator="none"
          label={<span style={{ wordBreak: "break-all" }}>{link}</span>}
          description="One guest per link. It opens the Steam Link app."
        />
      </PanelSectionRow>
      <PanelSectionRow>
        <ButtonItem layout="below" onClick={copy}>
          Copy link
        </ButtonItem>
      </PanelSectionRow>
      <PanelSectionRow>
        <ButtonItem layout="below" disabled={busy} onClick={() => run(createLink)}>
          {busy ? "Creating…" : "New guest link"}
        </ButtonItem>
      </PanelSectionRow>
    </PanelSection>
  );
}

const PRESENCE: Record<string, string> = {
  online: "Online",
  away: "Away",
  snooze: "Snooze",
  busy: "Busy",
  trade: "Looking to trade",
  play: "Looking to play",
  offline: "Offline",
};

function presence(friend: Friend) {
  return friend.game ? `Playing ${friend.game}` : PRESENCE[friend.state] ?? friend.state;
}

function FriendButton({ friend, onPick }: { friend: Friend; onPick: (friend: Friend) => void }) {
  return (
    <DialogButton
      onClick={() => onPick(friend)}
      style={{
        display: "flex",
        justifyContent: "space-between",
        alignItems: "center",
        gap: "12px",
        textAlign: "left",
        minHeight: "auto",
        padding: "8px 14px",
      }}
    >
      <span style={{ overflow: "hidden", textOverflow: "ellipsis", whiteSpace: "nowrap" }}>{friend.name}</span>
      <span
        style={{
          opacity: 0.7,
          fontSize: "0.85em",
          whiteSpace: "nowrap",
          overflow: "hidden",
          textOverflow: "ellipsis",
          maxWidth: "50%",
        }}
      >
        {presence(friend)}
      </span>
    </DialogButton>
  );
}

function FriendsModal({ closeModal }: { closeModal?: () => void }) {
  const [friends, setFriends] = useState<Friend[] | null>(null);
  const [problem, setProblem] = useState("");
  const [showOffline, setShowOffline] = useState(false);
  useEffect(() => {
    let live = true;
    getFriends()
      .then((result) => {
        if (!live) return;
        publish(result.state);
        setFriends(result.friends ?? []);
        if (!result.ok) setProblem(result.message || "Couldn't get your friends list.");
      })
      .catch((e) => {
        if (!live) return;
        setFriends([]);
        setProblem(String(e));
      });
    return () => {
      live = false;
    };
  }, []);
  const pick = (friend: Friend) => {
    closeModal?.();
    act(() => invite(friend.steamid));
  };
  const online = (friends ?? []).filter((friend) => friend.state !== "offline");
  const offline = (friends ?? []).filter((friend) => friend.state === "offline");
  return (
    <ConfirmModal strTitle="Invite a friend" strOKButtonText="Close" bAlertDialog closeModal={closeModal}>
      <Focusable
        style={{ display: "flex", flexDirection: "column", gap: "6px", maxHeight: "55vh", overflowY: "auto" }}
      >
        {friends === null && <div>Loading your friends…</div>}
        {problem && <div>{problem}</div>}
        {friends !== null && !problem && friends.length === 0 && <div>Your friends list is empty.</div>}
        {online.map((friend) => (
          <FriendButton key={friend.steamid} friend={friend} onPick={pick} />
        ))}
        {offline.length > 0 && !showOffline && (
          <DialogButton onClick={() => setShowOffline(true)}>Show offline friends ({offline.length})</DialogButton>
        )}
        {showOffline && (
          <div style={{ opacity: 0.7, fontSize: "0.9em", marginTop: "6px" }}>
            Friends who look offline may be Invisible: the invite still reaches them.
          </div>
        )}
        {showOffline &&
          offline.map((friend) => <FriendButton key={friend.steamid} friend={friend} onPick={pick} />)}
      </Focusable>
    </ConfirmModal>
  );
}

function FriendsSection() {
  return (
    <PanelSection title="Friends">
      <PanelSectionRow>
        <ButtonItem layout="below" onClick={() => showModal(<FriendsModal />)}>
          Invite a friend
        </ButtonItem>
      </PanelSectionRow>
    </PanelSection>
  );
}

function WaitingSection({ waiting }: { waiting: Waiting[] }) {
  return (
    <PanelSection title="Waiting">
      {waiting.map((entry) => (
        <PanelSectionRow key={`${entry.steamid}:${entry.guest}`}>
          <ButtonItem
            label={entry.steamid === "0" ? "Guest link" : entry.name || "A friend"}
            description={entry.steamid === "0" ? "Not used yet" : "Invited"}
            onClick={() => act(() => cancel(entry.steamid, entry.guest))}
          >
            Cancel
          </ButtonItem>
        </PanelSectionRow>
      ))}
    </PanelSection>
  );
}

function joinedText(joined: number) {
  if (!joined) return "Connected";
  const minutes = Math.max(0, Math.floor((Date.now() / 1000 - joined) / 60));
  if (minutes < 1) return "Joined just now";
  if (minutes < 60) return `Joined ${minutes} min ago`;
  return `Joined ${Math.floor(minutes / 60)} h ${minutes % 60} min ago`;
}

type InputKind = "keyboard" | "mouse" | "controller";

function GuestRows({ guest }: { guest: Guest }) {
  // Shows a flipped toggle right away, until the backend's answer replaces it.
  const [pending, setPending] = useState<Partial<Record<InputKind, boolean>>>({});
  const shown = { ...guest, ...pending };
  const name = guest.name || "Guest";
  const set = async (kind: InputKind, on: boolean) => {
    const next = { ...shown, [kind]: on };
    setPending((old) => ({ ...old, [kind]: on }));
    await act(() => setInput(guest.key, next.keyboard, next.mouse, next.controller));
    setPending({});
  };
  return (
    <>
      <PanelSectionRow>
        <Field label={name} description={joinedText(guest.joined)} bottomSeparator="none" />
      </PanelSectionRow>
      <PanelSectionRow>
        <ToggleField
          label="Keyboard"
          checked={shown.keyboard}
          onChange={(on) => set("keyboard", on)}
          bottomSeparator="none"
        />
      </PanelSectionRow>
      <PanelSectionRow>
        <ToggleField label="Mouse" checked={shown.mouse} onChange={(on) => set("mouse", on)} bottomSeparator="none" />
      </PanelSectionRow>
      <PanelSectionRow>
        <ToggleField
          label="Controller"
          checked={shown.controller}
          onChange={(on) => set("controller", on)}
          bottomSeparator="none"
        />
      </PanelSectionRow>
      <PanelSectionRow>
        <ButtonItem layout="below" onClick={() => act(() => removeGuest(guest.key))}>
          Remove {name}
        </ButtonItem>
      </PanelSectionRow>
    </>
  );
}

function GuestsSection({ guests }: { guests: Guest[] }) {
  return (
    <PanelSection title="Guests">
      {guests.map((guest) => (
        <GuestRows key={guest.key} guest={guest} />
      ))}
    </PanelSection>
  );
}

function SessionSection() {
  const confirm = () =>
    showModal(
      <ConfirmModal
        strTitle="End the session?"
        strDescription="Everyone is disconnected, and invites and links nobody has used stop working."
        strOKButtonText="End session"
        strCancelButtonText="Keep it"
        bDestructiveWarning
        onOK={() => {
          act(endSession);
        }}
      />,
    );
  return (
    <PanelSection>
      <PanelSectionRow>
        <ButtonItem layout="below" onClick={confirm}>
          End session
        </ButtonItem>
      </PanelSectionRow>
    </PanelSection>
  );
}

function SettingsSection({ state }: { state: State }) {
  const [busy, run] = useBusy();
  const download = state.download;
  const versions = [`Plugin ${state.version}`, state.build ? `app build ${state.build}` : ""].filter(Boolean);
  return (
    <PanelSection title="Settings">
      <PanelSectionRow>
        <Field
          label="AppImage"
          description={<span style={{ wordBreak: "break-all" }}>{state.binary || "Not found"}</span>}
          bottomSeparator="none"
        />
      </PanelSectionRow>
      <PanelSectionRow>
        <ButtonItem
          layout="below"
          disabled={download.active || busy}
          onClick={() => run(downloadAppImage)}
          description="The newest release, from GitHub."
        >
          {download.active
            ? `Downloading… ${download.total ? `${download.percent}%` : ""}`
            : state.installed
              ? "Update"
              : "Download"}
        </ButtonItem>
      </PanelSectionRow>
      <PanelSectionRow>
        <ToggleField
          label="Start when the panel opens"
          description="Runs Remote Play Whatever in the background, never as a game."
          checked={state.settings.autostart}
          onChange={(on) => act(() => setSettings({ autostart: on }))}
        />
      </PanelSectionRow>
      {state.running && (
        <PanelSectionRow>
          <ButtonItem
            layout="below"
            disabled={busy}
            onClick={() => run(stop)}
            description="A session goes on without it."
          >
            Stop Remote Play Whatever
          </ButtonItem>
        </PanelSectionRow>
      )}
      <PanelSectionRow>
        <Field description={versions.join(" · ")} bottomSeparator="none" />
      </PanelSectionRow>
    </PanelSection>
  );
}

function Content() {
  const state = useRpwState();
  if (!state) {
    return (
      <PanelSection>
        <PanelSectionRow>
          <Field label="Loading…" bottomSeparator="none" />
        </PanelSectionRow>
      </PanelSection>
    );
  }
  const status = state.ready ? state.status : null;
  return (
    <>
      <StatusSection state={state} />
      {status?.game && <LinkSection link={state.link} />}
      {status?.game && <FriendsSection />}
      {status && status.waiting.length > 0 && <WaitingSection waiting={status.waiting} />}
      {status && status.guests.length > 0 && <GuestsSection guests={status.guests} />}
      {status?.session && <SessionSection />}
      <SettingsSection state={state} />
    </>
  );
}

export default definePlugin(() => {
  const onState = addEventListener<[state: State]>("rpw_state", (state) => publish(state));
  const onToast = addEventListener<[message: Toast]>("rpw_toast", (message) =>
    toast(message.title, message.body ?? ""),
  );
  return {
    name: "Remote Play Whatever",
    titleView: <div className={staticClasses.Title}>Remote Play Whatever</div>,
    content: <Content />,
    icon: <FaUserFriends />,
    onDismount() {
      removeEventListener("rpw_state", onState);
      removeEventListener("rpw_toast", onToast);
    },
  };
});
