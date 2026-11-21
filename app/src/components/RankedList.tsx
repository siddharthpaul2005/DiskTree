import type { TreeItem } from "../lib/types";
import { formatBytes, formatCount, formatPct } from "../lib/format";
import { ItemIcon, RevealButton } from "./common";
import { IconArrowRight } from "./Icons";

export interface RankedListProps {
  items: TreeItem[];
  /** Size used for the percentage column (usually the parent folder). */
  total: number;
  colorOf: (item: TreeItem, index: number) => string;
  onOpen?: (item: TreeItem) => void;
  onReveal?: (item: TreeItem) => void;
  hot?: string | null;
  onHot?: (key: string | null) => void;
  keyOf?: (item: TreeItem, index: number) => string;
  pctLabel?: string;
  /** Narrow layout: no rank column, bar drawn under the name. */
  compact?: boolean;
}

/**
 * The core list: rank · icon · name+detail · bar · size · share · actions.
 * Bars are scaled to the largest item so small differences stay visible;
 * the share column gives the true fraction of the parent.
 */
export function RankedList({
  items, total, colorOf, onOpen, onReveal, hot, onHot, keyOf, pctLabel = "Share", compact = false,
}: RankedListProps) {
  const max = Math.max(1, ...items.map((i) => i.size));
  const key = keyOf ?? ((i: TreeItem, n: number) => i.path + "|" + i.type + n);
  return (
    <div className={"rows" + (compact ? " compact" : "")} onMouseLeave={() => onHot?.(null)}>
      <div className="rows-head" aria-hidden>
        {!compact && <span className="r">#</span>}<span /><span>Name</span>{!compact && <span>Relative size</span>}
        <span className="r">Size</span><span className="r">{pctLabel}</span><span style={{ width: 62 }} />
      </div>
      {items.map((item, i) => {
        const k = key(item, i);
        const color = colorOf(item, i);
        const canOpen = !!onOpen && item.type === "dir";
        const bar = <span className="row-bar"><span style={{ width: `${(item.size / max) * 100}%`, background: color }} /></span>;
        return (
          <div
            key={k}
            className={"row" + (canOpen ? " clickable" : "") + (hot === k ? " hot" : "")}
            onMouseEnter={() => onHot?.(k)}
            onClick={() => canOpen && onOpen!(item)}
            onKeyDown={(e) => { if (canOpen && (e.key === "Enter" || e.key === " ")) { e.preventDefault(); onOpen!(item); } }}
            tabIndex={canOpen ? 0 : -1}
            role={canOpen ? "button" : undefined}
            aria-label={canOpen ? `Open ${item.name}` : undefined}
          >
            {!compact && <span className="row-rank">{item.type === "other" ? "" : i + 1}</span>}
            <span className="row-icon"><ItemIcon item={item} color={item.type === "dir" ? color : undefined} /></span>
            <span className="row-name">
              <span className="primary" title={item.type === "other" ? undefined : item.path}>{item.name}</span>
              <span className="secondary">{detail(item)}</span>
              {compact && bar}
            </span>
            {!compact && bar}
            <span className="row-size">{formatBytes(item.size)}</span>
            <span className="row-pct">{formatPct(total ? item.size / total : 0)}</span>
            <span className="row-actions" style={{ width: 62, justifyContent: "flex-end" }}>
              {onReveal && item.type !== "other" && <RevealButton onReveal={() => onReveal(item)} />}
              {canOpen && (
                <button className="btn ghost small icon" title="Open this folder" aria-label="Open this folder"
                  onClick={(e) => { e.stopPropagation(); onOpen!(item); }}>
                  <IconArrowRight />
                </button>
              )}
            </span>
          </div>
        );
      })}
    </div>
  );
}

function detail(item: TreeItem): string {
  if (item.type === "dir") {
    if (item.denied) return "Access denied (Windows protected)";
    return `${formatCount(item.files)} files · ${formatCount(item.dirs)} folders`;
  }
  if (item.type === "other") return "Grouped together to keep the list readable";
  return item.ext ? `${item.ext.replace(/^\./, "").toUpperCase()} file` : "File";
}
