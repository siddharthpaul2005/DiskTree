// Colour system for the charts: gold and red first, then warm hues, all tuned
// for the obsidian background with white text on top (contrast >= 4.5 at 0.9 alpha).

export const PALETTE = [
  "#ffc53d", // Stark gold
  "#e62429", // hot-rod red
  "#f5e6c8", // ivory
  "#ff8a1f", // orange
  "#b0303a", // crimson
  "#d9b45a", // brass
  "#ff6b5b", // coral
  "#8c6d2c", // bronze
  "#fff08a", // pale yellow
  "#a8a29e", // warm grey
] as const;

export const OTHER_COLOR = "#78716c";
export const FILE_TINT = "#a8a29e";

/** Stable colour for the i-th ranked item. */
export function rankColor(i: number): string {
  return PALETTE[i % PALETTE.length];
}

/** Same hue, lighter/darker for deeper rings. depth 0 = the hue itself. */
export function shade(hex: string, depth: number): string {
  const { r, g, b } = hexToRgb(hex);
  const t = Math.min(0.42, depth * 0.13);
  const mix = (c: number) => Math.round(c + (16 - c) * t); // toward near-black
  return rgbToHex(mix(r), mix(g), mix(b));
}

export function withAlpha(hex: string, alpha: number): string {
  const { r, g, b } = hexToRgb(hex);
  return `rgba(${r}, ${g}, ${b}, ${alpha})`;
}

export function hexToRgb(hex: string) {
  const h = hex.replace("#", "");
  const full = h.length === 3 ? h.split("").map((c) => c + c).join("") : h;
  const n = parseInt(full, 16);
  return { r: (n >> 16) & 255, g: (n >> 8) & 255, b: n & 255 };
}

function rgbToHex(r: number, g: number, b: number) {
  return "#" + [r, g, b].map((v) => v.toString(16).padStart(2, "0")).join("");
}

// ── File categories ──────────────────────────────────────────────

export interface Category {
  id: string;
  label: string;
  color: string;
}

export const CATEGORIES: Category[] = [
  { id: "video", label: "Videos", color: "#e62429" },
  { id: "image", label: "Pictures", color: "#ff8a1f" },
  { id: "audio", label: "Music & audio", color: "#ffc53d" },
  { id: "doc", label: "Documents", color: "#f5e6c8" },
  { id: "archive", label: "Archives & disk images", color: "#d9b45a" },
  { id: "code", label: "Code & data", color: "#fff08a" },
  { id: "app", label: "Apps & libraries", color: "#b0303a" },
  { id: "game", label: "Game data", color: "#ff6b5b" },
  { id: "other", label: "Other", color: OTHER_COLOR },
];

const EXT_TO_CAT: Record<string, string> = {};
const add = (cat: string, exts: string) => exts.split(" ").forEach((e) => (EXT_TO_CAT[e] = cat));
add("video", "mp4 mkv avi mov wmv webm m4v flv mpg mpeg ts m2ts 3gp");
add("image", "jpg jpeg png gif bmp webp heic heif tif tiff raw cr2 nef arw dng svg ico psd");
add("audio", "mp3 flac wav aac ogg m4a wma opus aiff");
add("doc", "pdf doc docx xls xlsx ppt pptx odt ods odp txt rtf md csv epub");
add("archive", "zip rar 7z tar gz bz2 xz zst iso img vhd vhdx vmdk wim esd cab");
add("code", "js ts tsx jsx py java c cpp h hpp cs go rs rb php json xml yml yaml sql db sqlite log html css");
add("app", "exe dll msi sys lib a so pdb obj msix appx node");
add("game", "pak vpk bsa ba2 upk uasset forge bundle assets ucas utoc");

export function categoryOf(ext: string): Category {
  const id = EXT_TO_CAT[ext.replace(/^\./, "").toLowerCase()] ?? "other";
  return CATEGORIES.find((c) => c.id === id)!;
}

/** Readable text colour (dark or light) for a label drawn on `hex`. */
export function textOn(hex: string): string {
  const { r, g, b } = hexToRgb(hex);
  const lin = (c: number) => { const s = c / 255; return s <= 0.03928 ? s / 12.92 : ((s + 0.055) / 1.055) ** 2.4; };
  const L = 0.2126 * lin(r) + 0.7152 * lin(g) + 0.0722 * lin(b);
  return L > 0.18 ? "#0c0b09" : "#f5f5f4";
}
