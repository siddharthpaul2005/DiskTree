import { useCallback, useEffect, useState } from "react";
import { useApp } from "../state";
import { WHOLE_PC, type Drive, type Place } from "../lib/types";
import { driveTitle, formatAgo, formatBytes, formatCount, formatDuration, formatPct } from "../lib/format";
import { canElevate, isTauri, pickFolder } from "../lib/platform";
import { Empty, Loading, PageHead } from "../components/common";
import { IconArrowRight, IconDrive, IconFolder, IconRefresh, IconShield, IconStack, IconUsb } from "../components/Icons";

export function HomeView() {
  const { api, startScan, result, go, notify, status, elevated, restartAsAdmin } = useApp();
  const [drives, setDrives] = useState<Drive[] | null>(null);
  const [folder, setFolder] = useState("");
  const [skipDev, setSkipDev] = useState(false);
  const [places, setPlaces] = useState<Place[]>([]);

  useEffect(() => { api.places().then(setPlaces).catch(() => setPlaces([])); }, [api]);

  const loadDrives = useCallback(() => {
    setDrives(null);
    api.drives().then(setDrives).catch((e) => { notify(e.message, true); setDrives([]); });
  }, [api, notify]);

  useEffect(loadDrives, [loadDrives]);

  const scan = async (path: string) => {
    if (!path.trim()) return;
    try { await startScan(path.trim(), skipDev); }
    catch (e: any) { notify(e.message, true); }
  };

  const ready = drives?.filter((d) => d.ready) ?? [];
  const lastFailed = status?.state === "error" ? status.error : status?.state === "cancelled" ? "The last scan was cancelled." : "";

  return (
    <>
      <PageHead
        title="Your storage"
        sub="Pick a drive or a folder to scan. DiskTree only reads file sizes and dates; nothing is moved, changed or uploaded."
        actions={<button className="btn" onClick={loadDrives}><IconRefresh /> Refresh drives</button>}
      />
      <div className="page">
        {lastFailed && <div className="banner error"><span className="grow">{lastFailed}</span></div>}

        {result && (
          <div className="glass card" style={{ display: "flex", alignItems: "center", gap: 18 }}>
            <div className="drive-icon"><IconFolder /></div>
            <div style={{ minWidth: 0, flex: 1 }}>
              <div className="card-sub" style={{ marginTop: 0 }}>Latest results</div>
              <div className="drive-name ellipsis selectable">{result.scan_path}</div>
              <div className="muted" style={{ fontSize: 13 }}>
                <b className="num" style={{ color: "var(--text)" }}>{formatBytes(result.total_size)}</b> in{" "}
                {formatCount(result.total_files)} files · scanned {formatAgo(result.finished_at)} in {formatDuration(result.elapsed)}
              </div>
            </div>
            <button className="btn" onClick={() => scan(result.scan_path)}><IconRefresh /> Rescan</button>
            <button className="btn primary" onClick={() => go("overview")}>Open results <IconArrowRight /></button>
          </div>
        )}

        <section>
          <h2 className="card-title" style={{ margin: "6px 4px 12px" }}>Drives</h2>
          {drives === null ? (
            <Loading text="Reading drives…" />
          ) : ready.length === 0 ? (
            <div className="glass"><Empty title="No drives found">Try a folder path below.</Empty></div>
          ) : (
            <div className="grid-auto">
              {ready.map((d) => <DriveCard key={d.root} drive={d} onScan={() => scan(d.root)} />)}
              <WholePcCard drives={ready} onScan={() => scan(WHOLE_PC)} />
            </div>
          )}
          {canElevate && !elevated && (
            <div className="admin-hint">
              <span>
                Some Windows folders can only be opened by administrators, so a normal scan can't see inside them.
                For a complete picture, restart DiskTree as administrator (Windows asks first).
              </span>
              <button className="btn small" onClick={() => restartAsAdmin()}><IconShield /> Restart as administrator</button>
            </div>
          )}
        </section>

        <section className="glass card">
          <div className="card-head">
            <div>
              <h2 className="card-title">Scan a folder</h2>
              <div className="card-sub">Faster than a whole drive when you already know where to look, e.g. Downloads or a projects folder.</div>
            </div>
          </div>
          <form className="folder-form" onSubmit={(e) => { e.preventDefault(); scan(folder); }}>
            <input
              className="input mono"
              placeholder="C:\Users\you\Downloads"
              value={folder}
              onChange={(e) => setFolder(e.target.value)}
              spellCheck={false}
              aria-label="Folder path"
            />
            {isTauri && (
              <button type="button" className="btn" onClick={async () => { const p = await pickFolder(); if (p) setFolder(p); }}>
                <IconFolder /> Browse…
              </button>
            )}
            <button type="submit" className="btn primary" disabled={!folder.trim()}>Scan folder</button>
          </form>
          {places.length > 0 && (
            <div className="places" aria-label="Common folders">
              <span className="hint">Quick pick:</span>
              {places.map((p) => (
                <button
                  key={p.id}
                  type="button"
                  className={"place" + (folder === p.path ? " active" : "")}
                  title={p.path}
                  onClick={() => setFolder(p.path)}
                >
                  <IconFolder /> {p.name}
                </button>
              ))}
            </div>
          )}
          <label className="check" style={{ marginTop: 14 }}>
            <input type="checkbox" checked={skipDev} onChange={(e) => setSkipDev(e.target.checked)} />
            Skip developer folders (node_modules, .git). Applies to drive scans too.
          </label>
        </section>
      </div>
    </>
  );
}

/** "Scan the whole PC": every fixed drive in one scan, shown under "This PC". */
function WholePcCard({ drives, onScan }: { drives: Drive[]; onScan: () => void }) {
  const fixed = drives.filter((d) => d.type === "fixed");
  const total = fixed.reduce((s, d) => s + d.total, 0);
  const used = fixed.reduce((s, d) => s + d.used, 0);
  const frac = total ? used / total : 0;
  return (
    <div className="glass drive">
      <div className="drive-top">
        <div className="drive-icon"><IconStack /></div>
        <div style={{ minWidth: 0 }}>
          <div className="drive-name ellipsis">Whole PC</div>
          <div className="drive-meta">
            {fixed.length} drive{fixed.length === 1 ? "" : "s"}: {fixed.map((d) => d.root.replace(/\\$/, "")).join(", ")}
          </div>
        </div>
        <div className="drive-pct">
          <div className="big">{formatPct(frac)}</div>
          <div className="lbl">used</div>
        </div>
      </div>
      <div className="meter"><span style={{ width: `${Math.max(1, frac * 100)}%` }} /></div>
      <div className="drive-nums">
        <span><b>{formatBytes(used)}</b> used</span>
        <span><b>{formatBytes(total - used)}</b> free of {formatBytes(total)}</span>
      </div>
      <button className="btn primary" onClick={onScan} disabled={fixed.length === 0}>Scan whole PC</button>
    </div>
  );
}

function DriveCard({ drive, onScan }: { drive: Drive; onScan: () => void }) {
  const frac = drive.total ? drive.used / drive.total : 0;
  const tone = frac >= 0.9 ? "bad" : frac >= 0.75 ? "warn" : "";
  const Icon = drive.type === "removable" ? IconUsb : IconDrive;
  return (
    <div className="glass drive">
      <div className="drive-top">
        <div className="drive-icon"><Icon /></div>
        <div style={{ minWidth: 0 }}>
          <div className="drive-name ellipsis">{driveTitle(drive.label, drive.root, drive.type)}</div>
          <div className="drive-meta">{[drive.filesystem, drive.type].filter(Boolean).join(" · ")}</div>
        </div>
        <div className="drive-pct">
          <div className="big">{formatPct(frac)}</div>
          <div className="lbl">used</div>
        </div>
      </div>
      <div className={"meter " + tone}><span style={{ width: `${Math.max(1, frac * 100)}%` }} /></div>
      <div className="drive-nums">
        <span><b>{formatBytes(drive.used)}</b> used</span>
        <span><b>{formatBytes(drive.free)}</b> free of {formatBytes(drive.total)}</span>
      </div>
      <button className="btn primary" onClick={onScan}>Scan {drive.root.replace(/\\$/, "")}</button>
    </div>
  );
}
