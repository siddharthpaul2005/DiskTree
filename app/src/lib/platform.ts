// Bridges to the Tauri shell. In a plain browser (tests, dev without Tauri)
// the engine address comes from the URL: /?port=1234&token=abc

import { Api } from "./api";

export const isTauri = typeof window !== "undefined" && "__TAURI_INTERNALS__" in window;

interface EngineInfo { port: number; token: string; pid: number }

async function invoke<T>(cmd: string, args?: Record<string, unknown>): Promise<T> {
  const core = await import("@tauri-apps/api/core");
  return core.invoke<T>(cmd, args);
}

export async function connectEngine(): Promise<Api> {
  if (isTauri) {
    const info = await invoke<EngineInfo>("engine_info");
    return new Api(`http://127.0.0.1:${info.port}`, info.token);
  }
  const q = new URLSearchParams(window.location.search);
  const port = q.get("port");
  if (!port) throw new Error("Open this page from the DiskTree app, or add ?port=…&token=… to the address.");
  return new Api(`http://127.0.0.1:${port}`, q.get("token") ?? "");
}

export async function restartEngine(): Promise<void> {
  if (isTauri) await invoke("restart_engine");
}

/** True where "Restart as administrator" can work (the Windows desktop app). */
export const canElevate = isTauri && typeof navigator !== "undefined" && /Windows/i.test(navigator.userAgent);

/**
 * Restarts DiskTree with administrator rights (Windows asks first) and
 * rescans `rescan` once it is back. Resolves "cancelled" if the user says no;
 * otherwise this window closes.
 */
export async function restartAsAdmin(rescan?: string): Promise<"cancelled" | "restarting"> {
  if (!isTauri) throw new Error("Restarting as administrator works in the DiskTree app only.");
  try {
    await invoke("restart_as_admin", { rescan: rescan ?? null });
    return "restarting";
  } catch (e) {
    if (String(e) === "cancelled") return "cancelled";
    throw new Error(String(e));
  }
}

/** Folder to rescan right after a restart as administrator, if any. */
export async function takeStartupScan(): Promise<string | null> {
  if (!isTauri) return null;
  try { return await invoke<string | null>("take_startup_scan"); } catch { return null; }
}

export async function glassMode(): Promise<"mica" | "opaque"> {
  if (!isTauri) return "opaque";
  try { return await invoke<"mica" | "opaque">("glass_mode"); } catch { return "opaque"; }
}

export async function appVersion(): Promise<string> {
  if (!isTauri) return __APP_VERSION__;
  try { return await invoke<string>("app_version"); } catch { return __APP_VERSION__; }
}

/** Native folder picker; null when cancelled or unavailable. */
export async function pickFolder(): Promise<string | null> {
  if (!isTauri) return null;
  const { open } = await import("@tauri-apps/plugin-dialog");
  const r = await open({ directory: true, multiple: false, title: "Choose a folder to scan" });
  return typeof r === "string" ? r : null;
}

export async function openExternal(url: string): Promise<void> {
  if (isTauri) {
    const { openUrl } = await import("@tauri-apps/plugin-opener");
    await openUrl(url);
  } else {
    window.open(url, "_blank", "noopener");
  }
}
