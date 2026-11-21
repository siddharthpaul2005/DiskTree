import { useCallback, useEffect, useState, type ReactNode } from "react";
import type { Api } from "./lib/api";
import { appVersion, connectEngine, glassMode, openExternal, restartEngine } from "./lib/platform";
import { checkForUpdate, type UpdateInfo } from "./lib/update";
import { formatAgo, formatBytes, middleEllipsis } from "./lib/format";
import { AppProvider, useApp, type View } from "./state";
import { Loading, Toast } from "./components/common";
import {
  IconCamera, IconClose, IconFlow, IconHome, IconOverview, IconSearch, IconShield, IconSunburst, IconWarn,
} from "./components/Icons";
import { ScanOverlay } from "./components/ScanOverlay";
import { HomeView } from "./views/Home";
import { OverviewView } from "./views/Overview";
import { ExploreView } from "./views/Explore";
import { FlowView } from "./views/Flow";
import { SearchView } from "./views/Search";
import { SnapshotsView } from "./views/Snapshots";
import logoUrl from "./assets/logo.svg";

type Conn = { state: "connecting" } | { state: "ready"; api: Api } | { state: "failed"; message: string };

export default function App() {
  const [conn, setConn] = useState<Conn>({ state: "connecting" });
  const [toast, setToast] = useState<{ message: string; error?: boolean } | null>(null);
  const [version, setVersion] = useState("");

  const connect = useCallback(async () => {
    setConn({ state: "connecting" });
    try {
      const api = await connectEngine();
      await api.health();
      setConn({ state: "ready", api });
    } catch (e: any) {
      setConn({ state: "failed", message: e?.message ?? String(e) });
    }
  }, []);

  useEffect(() => {
    glassMode().then((m) => document.documentElement.setAttribute("data-glass", m));
    appVersion().then(setVersion);
    connect();
  }, [connect]);

  const notify = useCallback((message: string, error?: boolean) => setToast({ message, error }), []);
  const clearToast = useCallback(() => setToast(null), []);

  return (
    <>
      {conn.state === "ready" ? (
        <AppProvider api={conn.api} notify={notify}>
          <Shell version={version} />
        </AppProvider>
      ) : (
        <div className="overlay" style={{ background: "transparent", backdropFilter: "none" }}>
          <div className="glass dialog" style={{ textAlign: "center" }}>
            <img src={logoUrl} alt="" width={56} height={56} style={{ borderRadius: 14 }} />
            {conn.state === "connecting" ? (
              <Loading text="Starting the DiskTree engine…" />
            ) : (
              <>
                <h2 style={{ marginTop: 14 }}>The engine could not start</h2>
                <p className="muted selectable" style={{ marginTop: 10 }}>{conn.message}</p>
                <div className="dialog-foot" style={{ justifyContent: "center" }}>
                  <button className="btn primary" onClick={async () => { await restartEngine(); connect(); }}>
                    Try again
                  </button>
                </div>
              </>
            )}
          </div>
        </div>
      )}
      {toast && <Toast message={toast.message} error={toast.error} onDone={clearToast} />}
    </>
  );
}

const NAV: { view: View; title: string; icon: ReactNode; needsScan: boolean; group: string }[] = [
  { view: "home", title: "Home", icon: <IconHome />, needsScan: false, group: "Start" },
  { view: "overview", title: "Overview", icon: <IconOverview />, needsScan: true, group: "Results" },
  { view: "explore", title: "Explore", icon: <IconSunburst />, needsScan: true, group: "Results" },
  { view: "flow", title: "Path flow", icon: <IconFlow />, needsScan: true, group: "Results" },
  { view: "search", title: "Search", icon: <IconSearch />, needsScan: true, group: "Results" },
  { view: "snapshots", title: "Snapshots", icon: <IconCamera />, needsScan: false, group: "History" },
];

function Shell({ version }: { version: string }) {
  const { view, go, result, status, online, elevated } = useApp();
  const [update, setUpdate] = useState<UpdateInfo | null>(null);
  const [updateDismissed, setUpdateDismissed] = useState(false);

  useEffect(() => {
    if (version) checkForUpdate(version).then(setUpdate);
  }, [version]);

  // Keep the user on a page that has data
  useEffect(() => {
    if (!result && NAV.find((n) => n.view === view)?.needsScan) go("home");
  }, [result, view, go]);

  const running = !!status?.running;
  let group = "";

  return (
    <div className="app">
      <aside className="glass sidebar" aria-label="Navigation">
        <div className="brand">
          <img src={logoUrl} alt="" />
          <div>
            <div className="brand-name">DiskTree</div>
            <div className="brand-sub" title={version ? `Version ${version}` : undefined}>Storage intelligence</div>
          </div>
        </div>
        {NAV.map((n) => {
          const label = n.group !== group ? <div className="nav-label" key={n.group}>{n.group}</div> : null;
          group = n.group;
          const disabled = n.needsScan && !result;
          return [
            label,
            <button
              key={n.view}
              className={"nav-item" + (view === n.view ? " active" : "")}
              onClick={() => go(n.view)}
              disabled={disabled}
              title={disabled ? "Scan a drive or folder first" : undefined}
              aria-current={view === n.view ? "page" : undefined}
            >
              {n.icon}
              <span className="nav-title">{n.title}</span>
            </button>,
          ];
        })}
        <div className="sidebar-foot">
          {result ? (
            <div title={result.scan_path}>
              <div style={{ color: "var(--text-2)" }}>Current scan</div>
              <div className="ellipsis" style={{ color: "var(--text)", fontWeight: 600 }}>
                {middleEllipsis(result.scan_path, 26)}
              </div>
              <div>{formatAgo(result.finished_at)}</div>
              {result.volume && (
                <div className="foot-free" title="Space left on the drive">
                  {formatBytes(result.volume.free)} free
                  {result.volume.drives.length === 1 ? ` on ${result.volume.drives[0].replace(/\\$/, "")}` : ""}
                </div>
              )}
            </div>
          ) : (
            <div>No scan yet</div>
          )}
          {elevated && (
            <div className="admin-badge" title="Scans can read folders only administrators can open">
              <IconShield /> Administrator
            </div>
          )}
          {!online && (
            <div className="engine-state" role="status" style={{ color: "var(--bad)" }}>
              <span className="dot bad" />
              Engine not responding
            </div>
          )}
        </div>
      </aside>

      <main className="main">
        {update && !updateDismissed && (
          <div className="banner" style={{ margin: "0 6px 10px" }}>
            <IconWarn style={{ width: 18, height: 18, color: "var(--accent)" }} />
            <span className="grow">DiskTree {update.version} is available. You have {version}.</span>
            <button className="btn small primary" onClick={() => openExternal(update.url)}>Download</button>
            <button className="btn ghost small icon" aria-label="Dismiss" onClick={() => setUpdateDismissed(true)}>
              <IconClose />
            </button>
          </div>
        )}
        {view === "home" && <HomeView />}
        {view === "overview" && result && <OverviewView />}
        {view === "explore" && result && <ExploreView />}
        {view === "flow" && result && <FlowView />}
        {view === "search" && result && <SearchView />}
        {view === "snapshots" && <SnapshotsView />}
      </main>

      {running && <ScanOverlay />}
    </div>
  );
}
