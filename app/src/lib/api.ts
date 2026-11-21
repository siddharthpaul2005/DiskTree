import type {
  DiffResponse, Drive, ExtensionStat, FileItem, DirItem, Health, NodeResponse, Place,
  ScanStatus, SearchResponse, Snapshot,
} from "./types";

export class ApiError extends Error {
  constructor(public status: number, public code: string, message: string) {
    super(message);
  }
}

type Params = Record<string, string | number | boolean | undefined>;

/** Thin client for the engine's local HTTP API. */
export class Api {
  constructor(public readonly base: string, private readonly token: string) {}

  private async request<T>(method: string, path: string, params?: Params, body?: unknown): Promise<T> {
    const url = new URL(this.base + path);
    for (const [k, v] of Object.entries(params ?? {}))
      if (v !== undefined) url.searchParams.set(k, String(v));
    let res: Response;
    try {
      res = await fetch(url, {
        method,
        headers: {
          "X-DiskTree-Token": this.token,
          ...(body !== undefined ? { "Content-Type": "application/json" } : {}),
        },
        body: body !== undefined ? JSON.stringify(body) : undefined,
      });
    } catch {
      throw new ApiError(0, "offline", "The DiskTree engine is not responding");
    }
    const text = await res.text();
    let data: any = null;
    try { data = text ? JSON.parse(text) : null; } catch { /* not JSON */ }
    if (!res.ok)
      throw new ApiError(res.status, data?.error ?? "http", data?.message ?? `Request failed (${res.status})`);
    return data as T;
  }

  health() { return this.request<Health>("GET", "/api/health"); }
  drives() { return this.request<Drive[]>("GET", "/api/drives"); }
  places() { return this.request<Place[]>("GET", "/api/places"); }

  /**
   * Scans a folder, or every fixed drive when `opts.wholePc` is set.
   * `fullRead`: the engine can read every folder (administrator), so
   * System Volume Information (restore points) is scanned too.
   */
  startScan(path: string, opts: { skipDevFolders?: boolean; wholePc?: boolean; fullRead?: boolean } = {}) {
    return this.request<ScanStatus>("POST", "/api/scan", undefined, {
      path,
      whole_pc: !!opts.wholePc,
      default_excludes: false,
      excludes: [
        // Unreadable without administrator rights. The Recycle Bin is always
        // scanned: it is space you can free.
        ...(opts.fullRead ? [] : ["System Volume Information"]),
        ...(opts.skipDevFolders ? ["node_modules", ".git"] : []),
      ],
    });
  }
  cancelScan() { return this.request<ScanStatus>("POST", "/api/scan/cancel"); }
  status() { return this.request<ScanStatus>("GET", "/api/scan/status"); }

  node(path: string, depth = 1, maxChildren = 200, minFraction = 0) {
    return this.request<NodeResponse>("GET", "/api/node", {
      path, depth, max_children: maxChildren, min_fraction: minFraction,
    });
  }
  topFiles(n = 50) { return this.request<FileItem[]>("GET", "/api/top-files", { n }); }
  topDirs(n = 50) { return this.request<DirItem[]>("GET", "/api/top-dirs", { n }); }
  extensions(n = 50) { return this.request<ExtensionStat[]>("GET", "/api/extensions", { n }); }
  search(q: string, limit = 100) { return this.request<SearchResponse>("GET", "/api/search", { q, limit }); }

  snapshots() { return this.request<Snapshot[]>("GET", "/api/snapshots"); }
  saveSnapshot(label: string) { return this.request<Snapshot>("POST", "/api/snapshots", undefined, { label }); }
  deleteSnapshot(id: number) { return this.request<{ deleted: boolean }>("DELETE", `/api/snapshots/${id}`); }
  diff(id: number, limit = 200) { return this.request<DiffResponse>("GET", "/api/diff", { snapshot: id, limit }); }

  reveal(path: string) { return this.request<{ opened: boolean }>("POST", "/api/reveal", undefined, { path }); }
}
