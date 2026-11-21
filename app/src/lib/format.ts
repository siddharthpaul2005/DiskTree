const UNITS = ["B", "KB", "MB", "GB", "TB", "PB"];

/** 1536 -> "1.5 KB". Three significant digits, binary units (like Explorer). */
export function formatBytes(bytes: number): string {
  if (!Number.isFinite(bytes) || bytes <= 0) return "0 B";
  let i = 0;
  let v = bytes;
  while (v >= 1024 && i < UNITS.length - 1) { v /= 1024; i++; }
  if (i === 0) return `${Math.round(v)} B`;
  const digits = v >= 100 ? 0 : v >= 10 ? 1 : 2;
  return `${v.toFixed(digits)} ${UNITS[i]}`;
}

/** Signed size for diffs: "+1.2 GB", "−340 MB", "0 B". */
export function formatDelta(bytes: number): string {
  if (bytes === 0) return "0 B";
  return (bytes > 0 ? "+" : "\u2212") + formatBytes(Math.abs(bytes));
}

/** 1234567 -> "1,234,567" */
export function formatCount(n: number): string {
  return Math.round(n).toLocaleString("en-US");
}

/** 0.4271 -> "42.7%", tiny values -> "<0.1%" */
export function formatPct(fraction: number): string {
  if (!Number.isFinite(fraction) || fraction <= 0) return "0%";
  const p = fraction * 100;
  if (p < 0.1) return "<0.1%";
  if (p >= 99.95) return "100%";
  return `${p >= 10 ? p.toFixed(0) : p.toFixed(1)}%`;
}

/** Seconds -> "0.8 s", "12 s", "3 min 05 s" */
export function formatDuration(sec: number): string {
  if (!Number.isFinite(sec) || sec < 0) return "0 s";
  if (sec < 10) return `${sec.toFixed(1)} s`;
  if (sec < 60) return `${Math.round(sec)} s`;
  const m = Math.floor(sec / 60);
  const s = Math.round(sec % 60);
  return `${m} min ${String(s).padStart(2, "0")} s`;
}

/** Unix seconds -> "22 Nov 2026, 14:05" */
export function formatDate(unix: number): string {
  if (!unix) return "unknown";
  return new Date(unix * 1000).toLocaleString("en-GB", {
    day: "numeric", month: "short", year: "numeric", hour: "2-digit", minute: "2-digit",
  });
}

/** Unix seconds -> "3 days ago" */
export function formatAgo(unix: number, now = Date.now() / 1000): string {
  if (!unix) return "unknown";
  const d = Math.max(0, now - unix);
  if (d < 60) return "just now";
  if (d < 3600) return plural(Math.floor(d / 60), "minute");
  if (d < 86400) return plural(Math.floor(d / 3600), "hour");
  if (d < 86400 * 30) return plural(Math.floor(d / 86400), "day");
  if (d < 86400 * 365) return plural(Math.floor(d / (86400 * 30)), "month");
  return plural(Math.floor(d / (86400 * 365)), "year");
}

function plural(n: number, unit: string) {
  return `${n} ${unit}${n === 1 ? "" : "s"} ago`;
}

/** Last component of a Windows or POSIX path; drive roots stay "C:\". */
export function baseName(path: string): string {
  const trimmed = path.replace(/[\\/]+$/, "");
  if (/^[A-Za-z]:$/.test(trimmed)) return trimmed + "\\";
  const i = Math.max(trimmed.lastIndexOf("\\"), trimmed.lastIndexOf("/"));
  return i >= 0 ? trimmed.slice(i + 1) || path : trimmed || path;
}

/** Parent folder of a path ("" at the root). */
export function parentPath(path: string): string {
  const trimmed = path.replace(/[\\/]+$/, "");
  const i = Math.max(trimmed.lastIndexOf("\\"), trimmed.lastIndexOf("/"));
  if (i < 0) return "";
  const p = trimmed.slice(0, i);
  return /^[A-Za-z]:$/.test(p) ? p + "\\" : p;
}

/** Shortens the middle of a long path so both ends stay readable. */
export function middleEllipsis(text: string, max = 60): string {
  if (text.length <= max) return text;
  const keep = max - 1;
  const head = Math.ceil(keep * 0.4);
  const tail = keep - head;
  return text.slice(0, head) + "\u2026" + text.slice(text.length - tail);
}

/** Drive card title: "Local Disk (C:)" like Explorer. */
export function driveTitle(label: string, root: string, type: string): string {
  const letter = root.replace(/[\\/]+$/, "");
  const name = label || (type === "removable" ? "USB Drive" : type === "network" ? "Network Drive" : "Local Disk");
  return `${name} (${letter})`;
}
