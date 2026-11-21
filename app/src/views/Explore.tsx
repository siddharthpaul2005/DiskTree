import { useEffect, useMemo, useRef, useState } from "react";
import { arc as d3arc } from "d3-shape";
import { useApp } from "../state";
import { WHOLE_PC, type NodeResponse } from "../lib/types";
import { formatBytes, formatCount, formatPct, middleEllipsis } from "../lib/format";
import { childColor, itemKey, layoutPartition, nodeColor, topAncestor, type PNode } from "../lib/tree";
import { LoadError, Loading, PageHead, Tooltip, type TipContent } from "../components/common";
import { RankedList } from "../components/RankedList";
import { Breadcrumb } from "../components/Breadcrumb";
import { IconFlow, IconReveal, IconUp } from "../components/Icons";

const RINGS = 3;

export function ExploreView() {
  const { api, result, generation, focus, setFocus, go, reveal, notify } = useApp();
  const [data, setData] = useState<NodeResponse | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [attempt, setAttempt] = useState(0);
  const [hot, setHot] = useState<string | null>(null);
  const [tip, setTip] = useState<{ tip: TipContent; x: number; y: number } | null>(null);
  const path = focus || result?.scan_path || "";

  useEffect(() => {
    let live = true;
    setError(null);
    setHot(null);
    api.node(path, RINGS, 30, 0.003)
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

  return (
    <>
      <PageHead
        title="Explore"
        sub="The inner ring is this folder's contents; each ring outward goes one level deeper. Arc length is size. Click a folder to step into it, click the centre to go back up."
        actions={<>
          <button className="btn" disabled={atRoot} onClick={() => data && setFocus(data.breadcrumb[data.breadcrumb.length - 2].path)}>
            <IconUp /> Up one level
          </button>
          <button className="btn" onClick={() => go("flow")}><IconFlow /> Path flow</button>
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
        <div className="explore">
          <section className="glass chart-card">
            {error ? <LoadError message={error} onRetry={() => setAttempt((a) => a + 1)} /> : !data ? <Loading /> : (
              <Sunburst
                data={data}
                hot={hot}
                onHot={setHot}
                onOpen={(p) => setFocus(p)}
                onUp={() => !atRoot && setFocus(data.breadcrumb[data.breadcrumb.length - 2].path)}
                canGoUp={!atRoot}
                onTip={setTip}
              />
            )}
            <div className="chart-legend">
              <span>Ring 1 · contents</span><span>Ring 2 · one level in</span><span>Ring 3 · two levels in</span>
            </div>
          </section>
          <section className="glass list-card">
            <div className="card-head" style={{ padding: "0 8px" }}>
              <div style={{ minWidth: 0 }}>
                <h2 className="card-title ellipsis">{data ? data.name || data.path : "…"}</h2>
                <div className="card-sub">
                  {data ? `${formatBytes(data.size)} · ${formatCount(data.files)} files · ${formatCount(data.dirs)} folders` : ""}
                </div>
              </div>
            </div>
            <div className="list-scroll">
              {data && (
                <RankedList
                  items={data.children ?? []}
                  total={data.size}
                  colorOf={childColor}
                  keyOf={itemKey}
                  hot={hot}
                  onHot={setHot}
                  onOpen={(it) => setFocus(it.path)}
                  onReveal={(it) => reveal(it.path)}
                  pctLabel="Share"
                  compact
                />
              )}
            </div>
          </section>
        </div>
      </div>
      <Tooltip tip={tip?.tip ?? null} x={tip?.x ?? 0} y={tip?.y ?? 0} />
    </>
  );
}

interface SunburstProps {
  data: NodeResponse;
  hot: string | null;
  onHot: (k: string | null) => void;
  onOpen: (path: string) => void;
  onUp: () => void;
  canGoUp: boolean;
  onTip: (t: { tip: TipContent; x: number; y: number } | null) => void;
}

function Sunburst({ data, hot, onHot, onOpen, onUp, canGoUp, onTip }: SunburstProps) {
  const wrap = useRef<HTMLDivElement>(null);
  const [size, setSize] = useState(460);

  useEffect(() => {
    const el = wrap.current;
    if (!el) return;
    const ro = new ResizeObserver(([e]) => {
      const s = Math.floor(Math.min(e.contentRect.width, e.contentRect.height));
      setSize(Math.max(200, Math.min(720, s)));
    });
    ro.observe(el);
    return () => ro.disconnect();
  }, []);

  const R = size / 2 - 6;
  const r0 = R * 0.3;
  const ring = (R - r0) / RINGS;

  const nodes = useMemo(() => {
    const root = layoutPartition(data, 2 * Math.PI, RINGS + 1);
    return root.descendants().filter((n) => n.depth >= 1 && n.depth <= RINGS && n.x1 - n.x0 > 0.0025);
  }, [data]);

  const arcGen = useMemo(() => d3arc<PNode>()
    .startAngle((d) => d.x0)
    .endAngle((d) => d.x1)
    .padAngle((d) => Math.min((d.x1 - d.x0) / 2, 0.004))
    .padRadius(R)
    .innerRadius((d) => r0 + (d.depth - 1) * ring + 1)
    .outerRadius((d) => r0 + d.depth * ring - 1)
    .cornerRadius(0), [R, r0, ring]);

  // A hovered list row (depth-1 key) lights its whole branch
  const hotNode = hot ? nodes.find((n) => itemKey(n.data) === hot) : undefined;
  const isHot = (n: PNode) => {
    if (!hotNode) return false;
    if (n === hotNode) return true;
    // ancestors of the hovered arc, and descendants of a hovered top-level row
    for (let p = hotNode.parent; p; p = p.parent) if (p === n) return true;
    if (hotNode.depth === 1) return topAncestor(n) === hotNode;
    return false;
  };

  const centre = hotNode ?? null;
  const label = centre ? centre.data : data;
  const pct = centre ? (data.size ? centre.data.size / data.size : 0) : 1;

  const tipFor = (n: PNode): TipContent => {
    const d = n.data;
    const lines: [string, string][] = [["Size", formatBytes(d.size)], ["Of this folder", formatPct(data.size ? d.size / data.size : 0)]];
    if (d.type === "dir") lines.push(["Files", formatCount(d.files)]);
    if (d.type === "other") lines.push(["Items", formatCount(d.count)]);
    return {
      title: d.type === "other" ? d.name : middleEllipsis(d.path, 80),
      lines,
      hint: d.type === "dir" ? "Click to open this folder" : d.type === "other" ? "Small items grouped together" : undefined,
    };
  };

  return (
    <div className="chart-wrap" ref={wrap}>
      <svg
        width={size}
        height={size}
        viewBox={`${-size / 2} ${-size / 2} ${size} ${size}`}
        className={"sunburst" + (hotNode ? " has-hot" : "")}
        role="img"
        aria-label={`Sunburst chart of ${data.name || data.path}`}
        onMouseLeave={() => { onHot(null); onTip(null); }}
      >
        {nodes.map((n) => {
          const k = itemKey(n.data);
          const hotCls = hotNode ? (n === hotNode ? " hot" : isHot(n) ? " hot-path" : "") : "";
          return (
            <path
              key={k + n.depth}
              d={arcGen(n) ?? undefined}
              fill={nodeColor(n)}
              className={"arc" + (n.data.type !== "dir" ? " file" : "") + hotCls}
              onMouseEnter={() => onHot(k)}
              onMouseMove={(e) => onTip({ tip: tipFor(n), x: e.clientX, y: e.clientY })}
              onClick={() => n.data.type === "dir" && onOpen(n.data.path)}
            />
          );
        })}
        <circle
          className="center-hit"
          r={r0 - 3}
          onClick={onUp}
          onMouseEnter={() => { onHot(null); onTip(null); }}
          style={{ cursor: canGoUp ? "pointer" : "default" }}
        />
        <circle className="center-ring" r={r0 - 3} />
        {/* Text scales with the hole so lines never overlap on a small chart */}
        <g className="center-label">
          <text className="c-name" y={-r0 * 0.3} style={{ fontSize: clamp(r0 * 0.2, 10, 15) }}>
            {middleEllipsis(label.name || label.path, Math.max(8, Math.floor(r0 / 5.6)))}
          </text>
          <text className="c-size" y={r0 * 0.13} style={{ fontSize: clamp(r0 * 0.34, 14, 26) }}>{formatBytes(label.size)}</text>
          <text className="c-pct" y={r0 * 0.43} style={{ fontSize: clamp(r0 * 0.15, 9, 12) }}>
            {centre ? `${formatPct(pct)} of this folder` : canGoUp ? "click to go up" : "whole scan"}
          </text>
        </g>
      </svg>
    </div>
  );
}

function clamp(v: number, lo: number, hi: number) {
  return Math.max(lo, Math.min(hi, v));
}
