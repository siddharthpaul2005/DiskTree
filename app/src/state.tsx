import { createContext, useCallback, useContext, useEffect, useRef, useState, type ReactNode } from "react";
import type { Api } from "./lib/api";
import { WHOLE_PC, type ScanResultInfo, type ScanStatus } from "./lib/types";
import { restartAsAdmin as shellRestartAsAdmin, takeStartupScan } from "./lib/platform";

export type View = "home" | "overview" | "explore" | "flow" | "search" | "snapshots";

interface AppState {
  api: Api;
  status: ScanStatus | null;
  result: ScanResultInfo | null;
  /** Increments every time a new scan result becomes available. */
  generation: number;
  /** False while the engine stops answering. */
  online: boolean;
  /** Running as administrator, so scans can read every folder */
  elevated: boolean;
  /** Restart with administrator rights (Windows asks first), then rescan `rescan` */
  restartAsAdmin: (rescan?: string) => Promise<void>;
  view: View;
  focus: string;
  go: (view: View, focus?: string) => void;
  setFocus: (path: string) => void;
  startScan: (path: string, skipDevFolders: boolean) => Promise<void>;
  cancelScan: () => Promise<void>;
  reveal: (path: string) => void;
  notify: (message: string, error?: boolean) => void;
}

const Ctx = createContext<AppState | null>(null);

export function useApp(): AppState {
  const v = useContext(Ctx);
  if (!v) throw new Error("useApp outside provider");
  return v;
}

export function AppProvider({ api, notify, children }: { api: Api; notify: AppState["notify"]; children: ReactNode }) {
  const [status, setStatus] = useState<ScanStatus | null>(null);
  const [generation, setGeneration] = useState(0);
  const [view, setView] = useState<View>("home");
  const [focus, setFocus] = useState("");
  const [online, setOnline] = useState(true);
  const [elevated, setElevated] = useState(false);
  const [fullRead, setFullRead] = useState(false);
  // True from the moment we start a scan until the engine reports it stopped.
  // Completion is detected from this, not from timestamps: two scans of a
  // small folder can finish within the same second.
  const awaitingScan = useRef(false);
  const seenInitial = useRef(false);
  const timer = useRef<number | undefined>(undefined);

  const poll = useCallback(async () => {
    window.clearTimeout(timer.current);
    try {
      const s = await api.status();
      setStatus(s);
      setOnline(true);
      if (awaitingScan.current) {
        if (!s.running) {
          awaitingScan.current = false;
          if (s.state === "done" && s.result) {
            setGeneration((g) => g + 1);
            setFocus(s.result.scan_path);
            setView((v) => (v === "home" ? "overview" : v));
          }
        }
      } else if (!seenInitial.current) {
        // A result the engine already had when the UI connected
        seenInitial.current = true;
        if (s.state === "done" && s.result) {
          setGeneration((g) => g + 1);
          setFocus(s.result.scan_path);
        }
      }
      if (s.running) timer.current = window.setTimeout(poll, 250);
    } catch (e: any) {
      setOnline(false);
      notify(e.message ?? String(e), true);
      timer.current = window.setTimeout(poll, 2000);
    }
  }, [api, notify]);

  // Light heartbeat so the UI notices if the engine goes away (and comes back)
  useEffect(() => {
    const check = () => api.health().then((h) => {
      setOnline(true);
      setElevated(!!h.elevated);
      setFullRead(!!h.full_read);
    }, () => setOnline(false));
    check();
    const id = window.setInterval(check, 5000);
    return () => window.clearInterval(id);
  }, [api]);

  useEffect(() => {
    seenInitial.current = false;
    poll();
    return () => window.clearTimeout(timer.current);
  }, [poll]);

  const startScan = useCallback(async (path: string, skipDevFolders: boolean) => {
    awaitingScan.current = true;
    seenInitial.current = true;
    try {
      // Ask the engine directly: right after a restart the heartbeat may not have run yet
      const full = fullRead || !!(await api.health().catch(() => null))?.full_read;
      const s = await api.startScan(path, { skipDevFolders, wholePc: path === WHOLE_PC, fullRead: full });
      setStatus(s);
    } catch (e) {
      awaitingScan.current = false;
      throw e;
    }
    poll();
  }, [api, poll, fullRead]);

  // After "Restart as administrator", scan again what the user was looking at
  useEffect(() => {
    let live = true;
    takeStartupScan().then((path) => {
      if (live && path) startScan(path, false).catch((e) => notify(e.message, true));
    });
    return () => { live = false; };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [api]);

  const restartAsAdmin = useCallback(async (rescan?: string) => {
    try {
      const r = await shellRestartAsAdmin(rescan);
      if (r === "cancelled") notify("Still running without administrator rights.");
    } catch (e: any) {
      notify(e.message ?? String(e), true);
    }
  }, [notify]);

  const cancelScan = useCallback(async () => {
    const s = await api.cancelScan();
    setStatus(s);
  }, [api]);

  const reveal = useCallback((path: string) => {
    api.reveal(path).catch((e) => notify(e.message, true));
  }, [api, notify]);

  const go = useCallback((v: View, f?: string) => {
    if (f !== undefined) setFocus(f);
    setView(v);
  }, []);

  const result = status?.result ?? null;

  return (
    <Ctx.Provider value={{
      api, status, result, generation, online, elevated, restartAsAdmin, view, focus, go, setFocus,
      startScan, cancelScan, reveal, notify,
    }}>
      {children}
    </Ctx.Provider>
  );
}
