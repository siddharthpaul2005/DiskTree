import { useEffect, useMemo, useRef, useState } from "react";
import { useApp } from "../state";
import { WHOLE_PC, type NodeResponse } from "../lib/types";
import { formatBytes, formatCount, formatPct, middleEllipsis } from "../lib/format";
import { textOn } from "../lib/colors";
import { itemKey, layoutPartition, nodeColor, type PNode } from "../lib/tree";
import { LoadError, Loading, PageHead, Tooltip, type TipContent } from "../components/common";
import { Breadcrumb } from "../components/Breadcrumb";
import { IconReveal, IconSunburst, IconUp } from "../components/Icons";

const LEVELS = 4; // columns after the current folder

export function FlowView() {
  const { api, result, generation, focus, setFocus, go, reveal, notify } = useApp();
  const [data, setData] = useState<NodeResponse | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [attempt, setAttempt] = useState(0);
  const path = focus || result?.scan_path || "";

  useEffect(() => {
    let live = true;
    setError(null);
    api.node(path, LEVELS, 25, 0.002)
      .then((n) => live && setData(n))
      .catch((e) => {
        if (!live) return;
        // A folder that is not in this scan: fall back to the scan root
        if (e?.status === 404 && result && path !== result.scan_path) {
          notify(e.message, true);
          setFocus(result.scan_path);
        } else {
          setError(e?.message ?? String(e));
        }
      });
    return () => { live = false; };
  }, [api, path, generation, notify, result, setFocus, attempt]);

  if (!result) return null;
  const atRoot = !data || data.breadcrumb.length <= 1;
  const up = () => data && !atRoot && setFocus(data.breadcrumb[data.breadcrumb.length - 2].path);

  return (
    <>
      <PageHead
        title="Path flow"
        sub="Read left to right: each column splits the folder before it into what it contains. Taller blocks are bigger. Follow the tallest blocks to find where the space really sits."
        actions={<>
          <button className="btn" disabled={atRoot} onClick={up}><IconUp /> Up one level</button>
          <button className="btn" onClick={() => go("explore")}><IconSunburst /> Explore</button>
        </>}
      />
      <div className="page">
        <div className="glass" style={{ padding: "8px 10px", display: "flex", alignItems: "center", gap: 8 }}>
          {data ? <Breadcrumb crumbs={data.breadcrumb} onGo={setFocus} /> : <span className="hint">{error ? "" : "Loading…"}</span>}
          {data && data.path !== WHOLE_PC && (
            <button className="btn small" style={{ marginLeft: "auto" }} onClick={() => reveal(data.path)}>
              <IconReveal /> Show in File Explorer
            </button>
          )}
        </div>
        <section className="glass card" style={{ flex: 1, minHeight: 480, display: "flex", flexDirection: "column" }}>
          {error ? <LoadError message={error} onRetry={() => setAttempt((a) => a + 1)} /> : !data ? <Loading /> : <Icicle data={data} onOpen={setFocus} onUp={up} />}
        </section>
      </div>
    </>
  );
}

function Icicle({ data, onOpen, onUp }: { data: NodeResponse; onOpen: (p: string) => void; onUp: () => void }) {
  const wrap = useRef<HTMLDivElement>(null);
  const [box, setBox] = useState({ w: 900, h: 520 });
  const [hot, setHot] = useState<PNode | null>(null);
  const [tip, setTip] = useState<{ tip: TipContent; x: number; y: number } | null>(null);

  useEffect(() => {
    const el = wrap.current;
    if (!el) return;
    const ro = new ResizeObserver(([e]) =>
      setBox({ w: Math.max(500, Math.floor(e.contentRect.width)), h: Math.max(360, Math.floor(e.contentRect.height)) }));
    ro.observe(el);
    return () => ro.disconnect();
  }, []);

  const cols = LEVELS + 1;
  const colW = box.w / cols;
  // x runs vertically (size), y runs horizontally (depth)
  const nodes = useMemo(() => {
    const root = layoutPartition(data, box.h, cols);
    return root.descendants().filter((n) => n.depth < cols && n.x1 - n.x0 >= 1.5);
  }, [data, box.h, cols]);

  const related = (n: PNode) => {
    if (!hot) return false;
    for (let p: PNode | null = hot; p; p = p.parent) if (p === n) return true;     // ancestors
    for (let p: PNode | null = n; p; p = p.parent) if (p === hot) return true;     // descendants
    return false;
  };

  const tipFor = (n: PNode): TipContent => {
    const d = n.data;
    const lines: [string, string][] = [["Size", formatBytes(d.size)], ["Of this view", formatPct(data.size ? d.size / data.size : 0)]];
    if (d.type === "dir") lines.push(["Files", formatCount(d.files)]);
    if (d.type === "other") lines.push(["Items", formatCount(d.count)]);
    return {
      title: d.type === "other" ? d.name : middleEllipsis(d.path, 80),
      lines,
      hint: n.depth === 0 ? "Click to go up one level" : d.type === "dir" ? "Click to make this the first column" : undefined,
    };
  };

  return (
    <>
      <div className="icicle-cols" aria-hidden>
        {Array.from({ length: cols }, (_, i) => (
          <span key={i} style={{ width: colW, paddingLeft: 4 }}>{i === 0 ? "This folder" : `Level ${i}`}</span>
        ))}
      </div>
      <div ref={wrap} style={{ flex: 1, minHeight: 0, position: "relative" }}>
        <svg
          width={box.w}
          height={box.h}
          className={"icicle" + (hot ? " has-hot" : "")}
          role="img"
          aria-label={`Path flow of ${data.name || data.path}`}
          style={{ position: "absolute", inset: 0 }}
          onMouseLeave={() => { setHot(null); setTip(null); }}
        >
          {nodes.map((n) => {
            const x = n.depth * colW;
            const y = n.x0;
            const w = colW - 3;
            const h = n.x1 - n.x0;
            const k = itemKey(n.data) + n.depth;
            const showName = h >= 17;
            const showSize = h >= 34;
            const cls = "icicle-cell" + (n.data.type !== "dir" ? " file" : "") +
              (hot ? (n === hot ? " hot" : related(n) ? " hot-path" : "") : "");
            const fill = n.depth === 0 ? "#e7e5e4" : nodeColor(n);
            const maxChars = Math.floor((w - 16) / 7);
            return (
              <g
                key={k}
                className={cls}
                onMouseEnter={() => setHot(n)}
                onMouseMove={(e) => setTip({ tip: tipFor(n), x: e.clientX, y: e.clientY })}
                onClick={() => { if (n.depth === 0) onUp(); else if (n.data.type === "dir") onOpen(n.data.path); }}
              >
                <rect x={x} y={y} width={w} height={Math.max(0.5, h - 1)} rx={0} fill={fill} />
                {showName && (
                  <text style={{ fill: textOn(fill) }} x={x + 9} y={y + (showSize ? 18 : h / 2 + 4)}>
                    {middleEllipsis(n.data.name || n.data.path, maxChars)}
                  </text>
                )}
                {showSize && (
                  <text className="size" style={{ fill: textOn(fill), opacity: 0.78 }} x={x + 9} y={y + 34}>
                    {formatBytes(n.data.size)} · {formatPct(data.size ? n.data.size / data.size : 0)}
                  </text>
                )}
              </g>
            );
          })}
        </svg>
      </div>
      <Tooltip tip={tip?.tip ?? null} x={tip?.x ?? 0} y={tip?.y ?? 0} />
    </>
  );
}
