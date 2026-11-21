import { useCallback, useEffect, useState } from "react";
import { useApp } from "../state";
import type { Delta, DiffResponse, Snapshot } from "../lib/types";
import { formatBytes, formatCount, formatDate, formatDelta, formatAgo, middleEllipsis } from "../lib/format";
import { Empty, Loading, PageHead, RevealButton, Stat } from "../components/common";
import { IconCamera, IconCompare, IconTrash } from "../components/Icons";

export function SnapshotsView() {
  const { api, result, generation, notify, reveal } = useApp();
  const [snaps, setSnaps] = useState<Snapshot[] | null>(null);
  const [label, setLabel] = useState("");
  const [saving, setSaving] = useState(false);
  const [selected, setSelected] = useState<number | null>(null);
  const [diff, setDiff] = useState<DiffResponse | null>(null);
  const [diffBusy, setDiffBusy] = useState(false);
  const [confirmDelete, setConfirmDelete] = useState<Snapshot | null>(null);

  const load = useCallback(() => {
    api.snapshots().then(setSnaps).catch((e) => { notify(e.message, true); setSnaps([]); });
  }, [api, notify]);
  useEffect(load, [load, generation]);

  const save = async () => {
    setSaving(true);
    try {
      const s = await api.saveSnapshot(label.trim());
      notify(`Snapshot saved${s.label ? `: ${s.label}` : ""}`);
      setLabel("");
      load();
    } catch (e: any) { notify(e.message, true); }
    setSaving(false);
  };

  const compare = async (s: Snapshot) => {
    setSelected(s.id);
    setDiff(null);
    setDiffBusy(true);
    try { setDiff(await api.diff(s.id, 200)); }
    catch (e: any) { notify(e.message, true); setSelected(null); }
    setDiffBusy(false);
  };

  const remove = async (s: Snapshot) => {
    setConfirmDelete(null);
    try {
      await api.deleteSnapshot(s.id);
      if (selected === s.id) { setSelected(null); setDiff(null); }
      notify("Snapshot deleted");
      load();
    } catch (e: any) { notify(e.message, true); }
  };

  return (
    <>
      <PageHead
        title="Snapshots"
        sub="A snapshot remembers the size of every file at one moment. Compare the current scan with an older snapshot to see exactly what grew, shrank, appeared or disappeared."
      />
      <div className="page">
        <section className="glass card">
          <div className="card-head">
            <div>
              <h2 className="card-title">Save the current scan</h2>
              <div className="card-sub">
                {result ? <>Saves <span className="selectable">{result.scan_path}</span> ({formatBytes(result.total_size)}) on this PC only.</>
                  : "Scan a drive or folder first, then save it here."}
              </div>
            </div>
          </div>
          <form className="folder-form" onSubmit={(e) => { e.preventDefault(); save(); }}>
            <input className="input" placeholder="Label (optional), e.g. Before cleanup" value={label}
              onChange={(e) => setLabel(e.target.value)} disabled={!result} aria-label="Snapshot label" maxLength={80} />
            <button className="btn primary" type="submit" disabled={!result || saving}><IconCamera /> {saving ? "Saving…" : "Save snapshot"}</button>
          </form>
        </section>

        <section className="glass card">
          <div className="card-head">
            <div>
              <h2 className="card-title">Saved snapshots</h2>
              <div className="card-sub">Compare works best with a snapshot of the same folder as the current scan.</div>
            </div>
          </div>
          {!snaps ? <Loading /> : snaps.length === 0 ? (
            <Empty icon={<IconCamera />} title="No snapshots yet">Save one now, then rescan in a few days and compare.</Empty>
          ) : snaps.map((s) => (
            <div key={s.id} className={"snap-row" + (selected === s.id ? " selected" : "")}>
              <div className="row-name">
                <span className="primary">
                  {s.label || `Snapshot #${s.id}`}{" "}
                  {s.matches_current && <span className="badge accent" style={{ marginLeft: 6 }}>Same folder as current scan</span>}
                </span>
                <span className="secondary" title={s.scan_path}>{middleEllipsis(s.scan_path, 60)}</span>
              </div>
              <div className="row-name">
                <span>{formatDate(s.created_at)}</span>
                <span className="secondary">{formatAgo(s.created_at)}</span>
              </div>
              <div className="row-size">{formatBytes(s.total_size)}</div>
              <div className="row-pct">{formatCount(s.total_files)} files</div>
              <div style={{ display: "flex", gap: 6, justifyContent: "flex-end" }}>
                <button className="btn small" disabled={!result || diffBusy} onClick={() => compare(s)}
                  title={result ? "Compare with the current scan" : "Scan first to compare"}>
                  <IconCompare /> Compare
                </button>
                <button className="btn ghost small icon" title="Delete snapshot" aria-label="Delete snapshot" onClick={() => setConfirmDelete(s)}>
                  <IconTrash />
                </button>
              </div>
            </div>
          ))}
        </section>

        {diffBusy && <div className="glass"><Loading text="Comparing…" /></div>}
        {diff && <DiffPanel diff={diff} onReveal={reveal} />}
      </div>

      {confirmDelete && (
        <div className="overlay" role="dialog" aria-modal="true" onClick={() => setConfirmDelete(null)}>
          <div className="glass dialog" onClick={(e) => e.stopPropagation()}>
            <h2>Delete this snapshot?</h2>
            <p className="muted" style={{ marginTop: 10 }}>
              “{confirmDelete.label || `Snapshot #${confirmDelete.id}`}” from {formatDate(confirmDelete.created_at)} will be removed
              from DiskTree. Your files are not touched. This cannot be undone.
            </p>
            <div className="dialog-foot">
              <button className="btn" onClick={() => setConfirmDelete(null)}>Keep it</button>
              <button className="btn danger" onClick={() => remove(confirmDelete)}><IconTrash /> Delete snapshot</button>
            </div>
          </div>
        </div>
      )}
    </>
  );
}

type Tab = "dirs" | "added" | "removed" | "grown" | "shrunk";

function DiffPanel({ diff, onReveal }: { diff: DiffResponse; onReveal: (p: string) => void }) {
  const [tab, setTab] = useState<Tab>("dirs");
  const tabs: { id: Tab; label: string; count?: number }[] = [
    { id: "dirs", label: "Folders" },
    { id: "added", label: "New files", count: diff.counts.added },
    { id: "removed", label: "Deleted files", count: diff.counts.removed },
    { id: "grown", label: "Grew", count: diff.counts.grown },
    { id: "shrunk", label: "Shrank", count: diff.counts.shrunk },
  ];
  const rows: Delta[] = diff[tab];
  const maxAbs = Math.max(1, ...rows.map((r) => Math.abs(r.delta)));
  const net = diff.net_delta;

  return (
    <section className="glass card">
      <div className="card-head">
        <div>
          <h2 className="card-title">What changed since {diff.snapshot.label || `snapshot #${diff.snapshot.id}`}</h2>
          <div className="card-sub">From {formatDate(diff.snapshot.created_at)} to the current scan.</div>
        </div>
        {!diff.same_path && <span className="badge warn" title={diff.snapshot.scan_path}>Different folder: results may look like everything changed</span>}
      </div>

      <div className="stats" style={{ marginBottom: 18 }}>
        <Stat label="Net change" value={<span className={net > 0 ? "up" : net < 0 ? "down" : ""}>{formatDelta(net)}</span>}
          note={net > 0 ? "More space used than before" : net < 0 ? "Space was freed" : "No change in size"} />
        <Stat label="New files" value={formatCount(diff.counts.added)} note={formatDelta(diff.bytes.added)} />
        <Stat label="Deleted files" value={formatCount(diff.counts.removed)} note={formatDelta(-diff.bytes.removed)} />
        <Stat label="Grew / shrank" value={`${formatCount(diff.counts.grown)} / ${formatCount(diff.counts.shrunk)}`}
          note={`${formatDelta(diff.bytes.grown)} / ${formatDelta(-diff.bytes.shrunk)}`} />
      </div>

      <div style={{ display: "flex", alignItems: "center", gap: 12, marginBottom: 10 }}>
        <div className="tabs" role="tablist">
          {tabs.map((t) => (
            <button key={t.id} role="tab" aria-selected={tab === t.id} className={"tab" + (tab === t.id ? " active" : "")} onClick={() => setTab(t.id)}>
              {t.label}{t.count !== undefined && <span className="count">{formatCount(t.count)}</span>}
            </button>
          ))}
        </div>
        <span className="hint" style={{ marginLeft: "auto" }}>
          <span className="up">Red</span> = uses more space, <span className="down">green</span> = uses less.
        </span>
      </div>

      {rows.length === 0 ? <Empty title="Nothing here">No {tabs.find((t) => t.id === tab)!.label.toLowerCase()} between these two scans.</Empty> : (
        <div>
          <div className="delta-row" style={{ fontSize: 11, fontWeight: 600, letterSpacing: "0.08em", textTransform: "uppercase", color: "var(--text-3)" }}>
            <span>Path</span><span style={{ textAlign: "right" }}>Before → after</span><span style={{ textAlign: "center" }}>Change</span><span style={{ textAlign: "right" }}>Difference</span>
          </div>
          {rows.map((r) => {
            const w = (Math.abs(r.delta) / maxAbs) * 50;
            const grow = r.delta > 0;
            return (
              <div className="delta-row" key={r.path}>
                <span className="row-name" style={{ flexDirection: "row", alignItems: "center", gap: 6 }}>
                  <span className="ellipsis selectable" title={r.path}>{middleEllipsis(r.path, 80)}</span>
                  {tab !== "removed" && <span className="row-actions"><RevealButton onReveal={() => onReveal(absolute(diff.scan_path, r.path))} /></span>}
                </span>
                <span className="hint num" style={{ textAlign: "right", whiteSpace: "nowrap" }}>
                  {formatBytes(r.before)} → {formatBytes(r.after)}
                </span>
                <span className="diverge">
                  <span style={{
                    left: grow ? "50%" : `${50 - w}%`, width: `${Math.max(w, 0.6)}%`,
                    background: grow ? "#e62429" : "#4ade80",
                  }} />
                </span>
                <span className={"row-size " + (grow ? "up" : "down")}>{formatDelta(r.delta)}</span>
              </div>
            );
          })}
        </div>
      )}
    </section>
  );
}

/** Diff paths are relative to the scanned folder; Explorer needs a full path. */
function absolute(root: string, rel: string): string {
  if (/^[A-Za-z]:[\\/]/.test(rel) || rel.startsWith("\\\\") || rel.startsWith("/")) return rel;
  const sep = root.includes("/") && !root.includes("\\") ? "/" : "\\";
  return root.replace(/[\\/]+$/, "") + sep + rel.replace(/^[\\/]+/, "");
}
