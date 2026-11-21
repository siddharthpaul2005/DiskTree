import { afterEach, describe, expect, it, vi } from "vitest";
import {
  baseName, driveTitle, formatBytes, formatCount, formatDelta, formatDuration, formatPct,
  middleEllipsis, parentPath, formatAgo,
} from "./format";
import { categoryOf, shade, textOn, rankColor, PALETTE } from "./colors";
import { isNewer } from "./update";
import { Api, ApiError } from "./api";
import { itemKey, layoutPartition, nodeColor, topAncestor } from "./tree";
import type { NodeResponse } from "./types";

describe("format", () => {
  it("formats bytes with binary units", () => {
    expect(formatBytes(0)).toBe("0 B");
    expect(formatBytes(-5)).toBe("0 B");
    expect(formatBytes(512)).toBe("512 B");
    expect(formatBytes(1536)).toBe("1.50 KB");
    expect(formatBytes(10 * 1024 * 1024)).toBe("10.0 MB");
    expect(formatBytes(55 * 1024 ** 3)).toBe("55.0 GB");
    expect(formatBytes(300 * 1024 ** 3)).toBe("300 GB");
    expect(formatBytes(2 * 1024 ** 4)).toBe("2.00 TB");
  });
  it("formats signed deltas", () => {
    expect(formatDelta(0)).toBe("0 B");
    expect(formatDelta(2048)).toBe("+2.00 KB");
    expect(formatDelta(-2048)).toBe("\u22122.00 KB");
  });
  it("formats counts, percentages, durations", () => {
    expect(formatCount(1234567)).toBe("1,234,567");
    expect(formatPct(0)).toBe("0%");
    expect(formatPct(0.0004)).toBe("<0.1%");
    expect(formatPct(0.057)).toBe("5.7%");
    expect(formatPct(0.62)).toBe("62%");
    expect(formatPct(1)).toBe("100%");
    expect(formatDuration(1.67)).toBe("1.7 s");
    expect(formatDuration(42)).toBe("42 s");
    expect(formatDuration(185)).toBe("3 min 05 s");
  });
  it("formats relative time", () => {
    const now = 1_800_000_000;
    expect(formatAgo(now - 10, now)).toBe("just now");
    expect(formatAgo(now - 120, now)).toBe("2 minutes ago");
    expect(formatAgo(now - 3600, now)).toBe("1 hour ago");
    expect(formatAgo(now - 86400 * 3, now)).toBe("3 days ago");
    expect(formatAgo(0, now)).toBe("unknown");
  });
  it("handles Windows paths", () => {
    expect(baseName("C:\\")).toBe("C:\\");
    expect(baseName("C:\\Users\\me\\Downloads")).toBe("Downloads");
    expect(baseName("C:\\Users\\me\\")).toBe("me");
    expect(baseName("/home/me/x")).toBe("x");
    expect(parentPath("C:\\Users\\me")).toBe("C:\\Users");
    expect(parentPath("C:\\Users")).toBe("C:\\");
    expect(parentPath("C:\\")).toBe("");
  });
  it("shortens long text in the middle", () => {
    const s = "C:\\very\\long\\path\\that\\goes\\on\\and\\on\\file.txt";
    const m = middleEllipsis(s, 20);
    expect(m.length).toBe(20);
    expect(m.endsWith("file.txt")).toBe(true);
    expect(m).toContain("\u2026");
    expect(middleEllipsis("short", 20)).toBe("short");
  });
  it("names drives like Explorer", () => {
    expect(driveTitle("", "C:\\", "fixed")).toBe("Local Disk (C:)");
    expect(driveTitle("Data", "D:\\", "fixed")).toBe("Data (D:)");
    expect(driveTitle("", "E:\\", "removable")).toBe("USB Drive (E:)");
  });
});

describe("colors", () => {
  it("maps extensions to categories", () => {
    expect(categoryOf(".mp4").id).toBe("video");
    expect(categoryOf("JPG").id).toBe("image");
    expect(categoryOf(".vhdx").id).toBe("archive");
    expect(categoryOf(".exe").id).toBe("app");
    expect(categoryOf("").id).toBe("other");
    expect(categoryOf(".weird").id).toBe("other");
  });
  it("shades toward dark and picks readable text", () => {
    expect(shade("#ffffff", 0)).toBe("#ffffff");
    expect(shade("#ffffff", 2)).not.toBe("#ffffff");
    expect(textOn("#ffffff")).toBe("#0c0b09");
    expect(textOn("#0f172a")).toBe("#f5f5f4");
    expect(rankColor(PALETTE.length)).toBe(PALETTE[0]);
  });
});

describe("update check", () => {
  it("compares versions", () => {
    expect(isNewer("v1.2.0", "1.1.0")).toBe(true);
    expect(isNewer("1.10.0", "1.9.9")).toBe(true);
    expect(isNewer("v1.1.0", "1.1.0")).toBe(false);
    expect(isNewer("1.0.9", "1.1.0")).toBe(false);
    expect(isNewer("2.0.0-beta", "1.9.0")).toBe(true);
  });
});

describe("api client", () => {
  afterEach(() => vi.unstubAllGlobals());

  it("sends the token and query parameters", async () => {
    const fetchMock = vi.fn(async () => new Response(JSON.stringify([]), { status: 200 }));
    vi.stubGlobal("fetch", fetchMock);
    const api = new Api("http://127.0.0.1:5000", "secret");
    await api.node("C:\\Users", 2, 50, 0.01);
    const [url, init] = fetchMock.mock.calls[0] as unknown as [URL, RequestInit];
    expect(url.toString()).toContain("/api/node?");
    expect(url.searchParams.get("path")).toBe("C:\\Users");
    expect(url.searchParams.get("depth")).toBe("2");
    expect((init.headers as Record<string, string>)["X-DiskTree-Token"]).toBe("secret");
  });

  it("posts scan requests with excludes", async () => {
    const fetchMock = vi.fn(async () => new Response("{}", { status: 202 }));
    vi.stubGlobal("fetch", fetchMock);
    await new Api("http://x", "t").startScan("D:\\", { skipDevFolders: true });
    const init = (fetchMock.mock.calls[0] as unknown as [URL, RequestInit])[1];
    const body = JSON.parse(init.body as string);
    expect(init.method).toBe("POST");
    expect(body.path).toBe("D:\\");
    expect(body.excludes).toContain("node_modules");
    expect(body.default_excludes).toBe(false);
  });

  it("turns engine errors into ApiError", async () => {
    vi.stubGlobal("fetch", vi.fn(async () =>
      new Response(JSON.stringify({ error: "no_scan", message: "No scan yet" }), { status: 409 })));
    const err = await new Api("http://x", "t").topFiles().catch((e) => e);
    expect(err).toBeInstanceOf(ApiError);
    expect(err.status).toBe(409);
    expect(err.code).toBe("no_scan");
    expect(err.message).toBe("No scan yet");
  });

  it("reports an unreachable engine", async () => {
    vi.stubGlobal("fetch", vi.fn(async () => { throw new TypeError("Failed to fetch"); }));
    const err = await new Api("http://x", "t").health().catch((e) => e);
    expect(err.code).toBe("offline");
  });
});

describe("tree layout", () => {
  const data: NodeResponse = {
    type: "dir", name: "root", path: "R", size: 100, files: 3, dirs: 2, modified: 0, pct: 1, denied: false,
    breadcrumb: [{ name: "root", path: "R" }],
    children: [
      { type: "dir", name: "a", path: "R\\a", size: 60, files: 2, dirs: 0, modified: 0, pct: 0.6, denied: false,
        children: [
          { type: "file", name: "x", path: "R\\a\\x", size: 50, ext: ".mp4", modified: 0, pct: 0.83 },
          { type: "other", name: "1 smaller items", path: "R\\a", size: 10, count: 1, pct: 0.17 },
        ] },
      { type: "dir", name: "b", path: "R\\b", size: 30, files: 0, dirs: 0, modified: 0, pct: 0.3, denied: false, has_children: true },
      { type: "file", name: "y", path: "R\\y", size: 10, ext: "", modified: 0, pct: 0.1 },
    ],
  };

  it("sizes partitions by bytes", () => {
    const root = layoutPartition(data, 100, 3);
    expect(root.value).toBe(100);
    const [a, b, y] = root.children!;
    expect(a.x1 - a.x0).toBeCloseTo(60);
    expect(b.x1 - b.x0).toBeCloseTo(30);   // unexpanded folder still counts
    expect(y.x1 - y.x0).toBeCloseTo(10);
    expect(a.children!.length).toBe(2);
  });

  it("colours branches by their top-level rank", () => {
    const root = layoutPartition(data, 100, 3);
    const a = root.children![0];
    const x = a.children![0];
    expect(topAncestor(x)).toBe(a);
    expect(nodeColor(a)).toBe(PALETTE[0]);
    expect(nodeColor(root.children![1])).toBe(PALETTE[1]);
    expect(itemKey(a.children![1].data)).toBe("other:R\\a");
  });
});
