import { useEffect, useState, type ReactNode } from "react";
import { createPortal } from "react-dom";
import { IconFile, IconFolder, IconReveal, IconStack, IconWarn } from "./Icons";
import type { TreeItem } from "../lib/types";
import { categoryOf, FILE_TINT, OTHER_COLOR } from "../lib/colors";

export function PageHead({ title, sub, actions }: { title: ReactNode; sub?: ReactNode; actions?: ReactNode }) {
  return (
    <header className="page-head">
      <div style={{ minWidth: 0 }}>
        <h1 className="page-title">{title}</h1>
        {sub && <p className="page-sub">{sub}</p>}
      </div>
      {actions && <div className="page-actions">{actions}</div>}
    </header>
  );
}

export function Stat({ label, value, note, accent }: { label: string; value: ReactNode; note?: ReactNode; accent?: boolean }) {
  return (
    <div className="glass stat">
      <div className="stat-label">{label}</div>
      <div className={"stat-value" + (accent ? " grad" : "")}>{value}</div>
      {note && <div className="stat-note">{note}</div>}
    </div>
  );
}

/** Shown instead of a spinner when data could not be loaded. */
export function LoadError({ message, onRetry }: { message: string; onRetry: () => void }) {
  return (
    <div className="empty" role="alert">
      <IconWarn style={{ color: "var(--bad)" }} />
      <h3>Couldn't load this view</h3>
      <p className="selectable" style={{ maxWidth: 420 }}>{message}</p>
      <button className="btn primary" onClick={onRetry}>Try again</button>
    </div>
  );
}

export function Loading({ text = "Loading…" }: { text?: string }) {
  return <div className="loading"><div className="spinner" />{text}</div>;
}

export function Empty({ icon, title, children }: { icon?: ReactNode; title: string; children?: ReactNode }) {
  return (
    <div className="empty">
      {icon}
      <h3>{title}</h3>
      {children}
    </div>
  );
}

/** Icon for a tree item: folder, file (tinted by category), or "smaller items" bucket. */
export function ItemIcon({ item, color }: { item: Pick<TreeItem, "type"> & { ext?: string }; color?: string }) {
  if (item.type === "dir") return <IconFolder style={{ color: color ?? "#ffc53d" }} />;
  if (item.type === "other") return <IconStack style={{ color: OTHER_COLOR }} />;
  return <IconFile style={{ color: color ?? (item.ext ? categoryOf(item.ext).color : FILE_TINT) }} />;
}

export function RevealButton({ onReveal, label = "Show in File Explorer" }: { onReveal: () => void; label?: string }) {
  return (
    <button
      className="btn ghost small icon"
      title={label}
      aria-label={label}
      onClick={(e) => { e.stopPropagation(); onReveal(); }}
    >
      <IconReveal />
    </button>
  );
}

export interface TipContent { title: string; lines: [string, string][]; hint?: string }

/** Floating tooltip that follows the pointer and stays inside the window. */
export function Tooltip({ tip, x, y }: { tip: TipContent | null; x: number; y: number }) {
  const [size, setSize] = useState({ w: 260, h: 90 });
  const [el, setEl] = useState<HTMLDivElement | null>(null);
  useEffect(() => {
    if (el) setSize({ w: el.offsetWidth, h: el.offsetHeight });
  }, [el, tip]);
  if (!tip) return null;
  const left = Math.min(x + 16, window.innerWidth - size.w - 12);
  const top = y + 18 + size.h > window.innerHeight - 8 ? y - size.h - 14 : y + 18;
  return createPortal(
    <div className="tooltip" ref={setEl} style={{ left, top }}>
      <div className="t-name">{tip.title}</div>
      {tip.lines.map(([k, v]) => (
        <div className="t-line" key={k}><span>{k}</span><b>{v}</b></div>
      ))}
      {tip.hint && <div className="t-hint">{tip.hint}</div>}
    </div>,
    document.body,
  );
}

export function Toast({ message, error, onDone }: { message: string; error?: boolean; onDone: () => void }) {
  useEffect(() => {
    const t = setTimeout(onDone, error ? 6000 : 3200);
    return () => clearTimeout(t);
  }, [message, error, onDone]);
  return <div className={"toast" + (error ? " error" : "")} role="status">{message}</div>;
}
