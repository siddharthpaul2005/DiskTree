import { useEffect, useMemo, useState } from "react";
import { useApp } from "../state";
import type { ExtensionStat, FileItem, NodeResponse, ScanResultInfo, TreeItem } from "../lib/types";
import { baseName, formatAgo, formatBytes, formatCount, formatDuration, formatPct, middleEllipsis } from "../lib/format";
import { CATEGORIES, categoryOf, OTHER_COLOR, rankColor } from "../lib/colors";
import { LoadError, Loading, PageHead, RevealButton, Stat } from "../components/common";
import { RankedList } from "../components/RankedList";
import { IconRefresh, IconShield, IconSunburst } from "../components/Icons";
import { canElevate } from "../lib/platform";

export function OverviewView() {
  const { api, result, generation, go, reveal, startScan, notify, elevated, restartAsAdmin } = useApp();
  const [root, setRoot] = useState<NodeResponse | null>(null);
  const [files, setFiles] = useState<FileItem[] | null>(null);
  const [exts, setExts] = useState<ExtensionStat[] | null>(null);
  const [hot, setHot] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [attempt, setAttempt] = useState(0);

  useEffect(() => {
    let live = true;
    setError(null);
    Promise.all([api.node("", 1, 12), api.topFiles(12), api.extensions(200)])
      .then(([n, f, e]) => { if (live) { setRoot(n); setFiles(f); setExts(e); } })
      .catch((e) => { if (live) setError(e?.message ?? String(e)); });
    return () => { live = false; };
  }, [api, generation, attempt]);

  const children = root?.children ?? [];
  const colorOf = (item: TreeItem, i: number) => (item.type === "other" ? OTHER_COLOR : rankColor(i));
  const keyOf = (_item: TreeItem, i: number) => `${i}`;

  const categories = useMemo(() => {
    if (!exts) return [];
    const m = new Map<string, { size: number; count: number }>();
    for (const e of exts) {
      const c = categoryOf(e.ext);
      const v = m.get(c.id) ?? { size: 0, count: 0 };
      v.size += e.size; v.count += e.count;
      m.set(c.id, v);
    }
    return CATEGORIES.map((c) => ({ ...c, ...(m.get(c.id) ?? { size: 0, count: 0 }) }))
      .filter((c) => c.size > 0)
      .sort((a, b) => (a.id === "other" ? 1 : 0) - (b.id === "other" ? 1 : 0) || b.size - a.size);
  }, [exts]);
  const catTotal = categories.reduce((s, c) => s + c.size, 0);

  if (!result) return null;
  const skipped = result.skipped;

  return (
    <>
      <PageHead
        title={`Overview of ${baseName(result.scan_path)}`}
        sub={<>Scanned <span className="selectable">{result.scan_path}</span> {formatAgo(result.finished_at)}, in {formatDuration(result.elapsed)}.</>}
        actions={<>
          <button className="btn" onClick={() => startScan(result.scan_path, false).catch((e) => notify(e.message, true))}><IconRefresh /> Rescan</button>
          <button className="btn primary" onClick={() => go("explore", result.scan_path)}><IconSunburst /> Explore</button>
        </>}
      />
      <div className="page">
        <div className="stats">
          <Stat label="Total size" value={formatBytes(result.total_size)} accent note="Everything found in this scan" />
          {result.volume ? (
            <Stat
              label="Space left"
              value={formatBytes(result.volume.free)}
              accent
              note={`free of ${formatBytes(result.volume.total)} on ${driveNames(result.volume.drives)}`}
            />
          ) : null}
          <Stat label="Files" value={formatCount(result.total_files)} note={`in ${formatCount(result.total_dirs)} folders`} />
          <Stat
            label="Not readable"
            value={formatCount(skipped)}
            note={!skipped ? "Every folder was read"
              : elevated ? "Locked even for administrators"
              : "Admin-only folders Windows keeps private"}
          />
        </div>

        <DriveSpace
          result={result}
          elevated={elevated}
          onElevate={canElevate && !elevated && skipped > 0 ? () => restartAsAdmin(result.scan_path) : undefined}
        />

        <section className="glass card">
          <div className="card-head">
            <div>
              <h2 className="card-title">Where your space goes</h2>
              <div className="card-sub">Top-level items, largest first. Click a folder to see inside it.</div>
            </div>
          </div>
          {error ? <LoadError message={error} onRetry={() => setAttempt((a) => a + 1)} /> : !root ? <Loading /> : (
            <>
              <div className={"strip" + (hot !== null ? " has-hot" : "")} role="img"
                aria-label="Share of space taken by each top-level item">
                {children.map((c, i) => (
                  <span
                    key={i}
                    className={hot === `${i}` ? "hot" : ""}
                    style={{ flexGrow: c.size, flexBasis: 0, background: colorOf(c, i) }}
                    title={`${c.name}: ${formatBytes(c.size)} (${formatPct(root.size ? c.size / root.size : 0)})`}
                    onMouseEnter={() => setHot(`${i}`)}
                    onMouseLeave={() => setHot(null)}
                  />
                ))}
              </div>
              <div className="legend">
                {children.slice(0, 8).map((c, i) => (
                  <span className="legend-item" key={i} onMouseEnter={() => setHot(`${i}`)} onMouseLeave={() => setHot(null)}>
                    <span className="swatch" style={{ background: colorOf(c, i) }} />
                    {middleEllipsis(c.name, 28)} <b>{formatPct(root.size ? c.size / root.size : 0)}</b>
                  </span>
                ))}
              </div>
              <div style={{ marginTop: 18 }}>
                <RankedList
                  items={children}
                  total={root.size}
                  colorOf={colorOf}
                  keyOf={keyOf}
                  hot={hot}
                  onHot={setHot}
                  onOpen={(it) => go("explore", it.path)}
                  onReveal={(it) => reveal(it.path)}
                />
              </div>
            </>
          )}
        </section>

        <div className="grid-2">
          <section className="glass card">
            <div className="card-head">
              <div>
                <h2 className="card-title">Largest files</h2>
                <div className="card-sub">The single files taking the most space.</div>
              </div>
            </div>
            {error ? null : !files ? <Loading /> : files.map((f) => (
              <div className="mini-row" key={f.path}>
                <span className="swatch" style={{ background: categoryOf(f.ext).color }} />
                <span className="row-name">
                  <span className="primary">{f.name}</span>
                  <span className="secondary" title={f.path}>{middleEllipsis(f.path, 64)}</span>
                </span>
                <span className="row-size">{formatBytes(f.size)}</span>
                <span className="row-actions"><RevealButton onReveal={() => reveal(f.path)} /></span>
              </div>
            ))}
          </section>

          <section className="glass card">
            <div className="card-head">
              <div>
                <h2 className="card-title">File types</h2>
                <div className="card-sub">Space grouped by kind of file, based on the extension.</div>
              </div>
            </div>
            {error ? null : !exts ? <Loading /> : (
              <>
                <div className="strip" role="img" aria-label="Space by file type">
                  {categories.map((c) => (
                    <span key={c.id} style={{ flexGrow: c.size, flexBasis: 0, background: c.color }}
                      title={`${c.label}: ${formatBytes(c.size)}`} />
                  ))}
                </div>
                <div style={{ marginTop: 14 }}>
                  {categories.map((c) => (
                    <div className="mini-row" key={c.id}>
                      <span className="swatch" style={{ background: c.color }} />
                      <span className="row-name">
                        <span className="primary">{c.label}</span>
                        <span className="secondary">{formatCount(c.count)} files · {topExts(exts, c.id)}</span>
                      </span>
                      <span className="row-pct">{formatPct(catTotal ? c.size / catTotal : 0)}</span>
                      <span className="row-size" style={{ minWidth: 74 }}>{formatBytes(c.size)}</span>
                    </div>
                  ))}
                </div>
              </>
            )}
          </section>
        </div>
      </div>
    </>
  );
}

function driveNames(drives: string[]): string {
  return drives.map((d) => d.replace(/\\$/, "")).join(", ");
}

/** How the drive's space splits: what the scan found, what's in use but hidden, and what's free. */
function DriveSpace({ result, elevated, onElevate }: {
  result: ScanResultInfo;
  elevated: boolean;
  /** Offered when folders were skipped for lack of administrator rights */
  onElevate?: () => void;
}) {
  const v = result.volume;
  if (!v || !v.total) return null;
  const found = Math.min(result.allocated, v.used);
  const rest = Math.max(0, v.used - found);
  const where = v.drives.length > 1 ? `${v.drives.length} drives (${driveNames(v.drives)})` : v.drives[0];
  // With admin rights the engine measures the file table, so it gets its own segment
  const table = v.complete && v.file_table != null ? Math.min(v.file_table, rest) : 0;
  const other = rest - table;
  const segs = v.complete
    ? [
        { label: "Files found", value: found, color: "var(--gold)",
          note: "Everything this scan lists, as stored on disk" },
        ...(table > 0 ? [{ label: "Drive's file table", value: table, color: "var(--warn)",
          note: "Where Windows keeps track of every file; very small files live inside it" }] : []),
        { label: elevated ? "Other system space" : "Hidden by Windows", value: other, color: "var(--red)",
          note: elevated
            ? `Windows' own bookkeeping that no listing shows${result.skipped ? `, plus ${formatCount(result.skipped)} folders locked even for administrators` : ""}`
            : `In use, but no app can list it without admin rights: restore points, ${
                result.skipped ? `${formatCount(result.skipped)} admin-only folders, ` : ""
              }the drive's file table (which also holds very small files) and other accounts' Recycle Bins` },
        { label: "Free", value: v.free, color: "var(--free)", note: "Space left for new files" },
      ]
    : [
        { label: "This folder", value: found, color: "var(--gold)", note: "What this scan lists, as stored on disk" },
        { label: "Rest of the drive", value: rest, color: "var(--brass)", note: "Used by everything outside this folder" },
        { label: "Free", value: v.free, color: "var(--free)", note: "Space left for new files" },
      ];
  // Explorer-style sizes count hard-linked Windows files twice; say why the list adds up to more
  const listedMore = result.total_size - result.allocated > Math.max(256 * 1024 ** 2, result.allocated * 0.01);

  return (
    <section className="glass card">
      <div className="card-head">
        <div>
          <h2 className="card-title">Drive space</h2>
          <div className="card-sub">{where} · {formatBytes(v.total)} in total</div>
        </div>
        <div className="space-free">
          <div className="big">{formatBytes(v.free)}</div>
          <div className="lbl">left · {formatPct(v.free / v.total)} free</div>
        </div>
      </div>
      <div className="strip" role="img"
        aria-label={segs.map((s) => `${s.label} ${formatBytes(s.value)}`).join(", ")}>
        {segs.map((s) => (
          <span key={s.label} style={{ flexGrow: s.value, flexBasis: 0, background: s.color }}
            title={`${s.label}: ${formatBytes(s.value)}`} />
        ))}
      </div>
      <div className="space-legend">
        {segs.map((s) => (
          <div key={s.label}>
            <div className="space-label"><span className="swatch" style={{ background: s.color }} />{s.label}</div>
            <div className="space-value">{formatBytes(s.value)}</div>
            <div className="space-note">{s.note}</div>
          </div>
        ))}
      </div>
      {onElevate && (
        <div className="space-admin">
          <span>
            <b>See inside the hidden part.</b> Windows only lets administrators open {formatCount(result.skipped)} of
            these folders. DiskTree restarts with administrator rights (Windows asks first) and scans again. It still
            only reads names and sizes.
          </span>
          <button className="btn" onClick={onElevate}><IconShield /> Restart as administrator</button>
        </div>
      )}
      {listedMore && (
        <div className="space-foot">
          The sizes below add up to {formatBytes(result.total_size)}, the way File Explorer counts them. On disk
          they take {formatBytes(result.allocated)}: some Windows files appear in two folders but are stored once,
          and tiny files take no space of their own.
        </div>
      )}
    </section>
  );
}

function topExts(exts: ExtensionStat[], catId: string): string {
  const list = exts.filter((e) => categoryOf(e.ext).id === catId).slice(0, 4)
    .map((e) => (e.ext ? e.ext.replace(/^\./, "").toUpperCase() : "no extension"));
  return list.join(", ");
}
