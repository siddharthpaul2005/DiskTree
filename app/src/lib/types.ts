// Shapes returned by the engine's local API (see src/service/engine.cpp)

export interface Drive {
  root: string;
  label: string;
  filesystem: string;
  type: "fixed" | "removable" | "network" | "cdrom" | "ramdisk" | "unknown" | string;
  total: number;
  free: number;
  used: number;
  ready: boolean;
}

export type ScanState = "idle" | "scanning" | "building" | "done" | "cancelled" | "error";

export interface ScanResultInfo {
  scan_path: string;
  total_size: number;
  total_files: number;
  total_dirs: number;
  elapsed: number;
  skipped: number;
  finished_at: number;
  /** Bytes the files really take on disk (hard links once, compression, cluster rounding) */
  allocated: number;
  /** Space on the drive(s) the scan is on; null when unknown */
  volume: Volume | null;
}

export interface Volume {
  total: number;
  free: number;
  used: number;
  drives: string[];
  /** Size of the drives' NTFS file table; only known with administrator rights */
  file_table?: number | null;
  /** true when the scan covered the whole drive(s), so used - allocated is hidden space */
  complete: boolean;
}

export interface ScanStatus {
  state: ScanState;
  running: boolean;
  scan_path: string;
  error: string;
  files: number;
  dirs: number;
  bytes: number;
  current_path: string;
  elapsed: number;
  result: ScanResultInfo | null;
}

export interface FileItem {
  type: "file";
  name: string;
  path: string;
  size: number;
  ext: string;
  modified: number;
  pct: number;
}

export interface DirItem {
  type: "dir";
  name: string;
  path: string;
  size: number;
  files: number;
  dirs: number;
  modified: number;
  pct: number;
  denied: boolean;
  has_children?: boolean;
  children?: TreeItem[];
}

export interface OtherItem {
  type: "other";
  name: string;
  path: string;
  size: number;
  count: number;
  pct: number;
}

export type TreeItem = FileItem | DirItem | OtherItem;

export interface NodeResponse extends DirItem {
  breadcrumb: { name: string; path: string }[];
}

export interface ExtensionStat {
  ext: string;
  size: number;
  count: number;
}

export interface SearchResponse {
  query: string;
  dirs: DirItem[];
  files: FileItem[];
}

export interface Snapshot {
  id: number;
  scan_path: string;
  label: string;
  created_at: number;
  total_size: number;
  total_files: number;
  total_dirs: number;
  matches_current?: boolean;
}

export interface Delta {
  path: string;
  before: number;
  after: number;
  delta: number;
}

export interface DiffResponse {
  snapshot: Snapshot;
  scan_path: string;
  same_path: boolean;
  net_delta: number;
  unchanged: number;
  counts: { added: number; removed: number; grown: number; shrunk: number };
  bytes: { added: number; removed: number; grown: number; shrunk: number };
  dirs: Delta[];
  added: Delta[];
  removed: Delta[];
  grown: Delta[];
  shrunk: Delta[];
}

export interface Place {
  id: string;
  name: string;
  path: string;
}

/** Name the engine gives the root of a whole-PC scan. */
export const WHOLE_PC = "This PC";

export interface Health {
  name: string;
  version: string;
  db: string;
  /** Running as administrator */
  elevated?: boolean;
  /** Can read every folder, including ones closed to administrators */
  full_read?: boolean;
}
