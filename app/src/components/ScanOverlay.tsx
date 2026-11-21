import { useState } from "react";
import { useApp } from "../state";
import { formatBytes, formatCount, formatDuration } from "../lib/format";

export function ScanOverlay() {
  const { status, cancelScan, notify } = useApp();
  const [cancelling, setCancelling] = useState(false);
  if (!status) return null;
  const building = status.state === "building";

  return (
    <div className="overlay" role="dialog" aria-modal="true" aria-labelledby="scan-title">
      <div className="glass dialog" style={{ width: "min(640px, calc(100vw - 48px))" }}>
        <div style={{ display: "flex", alignItems: "center", gap: 20 }}>
          <svg className="scan-ring" viewBox="0 0 64 64" aria-hidden>
            <defs>
              <linearGradient id="ringGrad" x1="0" y1="0" x2="1" y2="1">
                <stop offset="0" stopColor="#ffe08a" />
                <stop offset="1" stopColor="#e62429" />
              </linearGradient>
            </defs>
            <circle className="track" cx="32" cy="32" r="26" />
            <circle className="arc" cx="32" cy="32" r="26" />
          </svg>
          <div style={{ minWidth: 0 }}>
            <h2 id="scan-title">{building ? "Organising results…" : "Scanning"}</h2>
            <div className="muted ellipsis selectable" style={{ marginTop: 4, fontWeight: 600 }} title={status.scan_path}>
              {status.scan_path}
            </div>
          </div>
        </div>

        <div className="scan-counters">
          <div className="scan-counter"><div className="v">{formatCount(status.files)}</div><div className="l">Files found</div></div>
          <div className="scan-counter"><div className="v">{formatCount(status.dirs)}</div><div className="l">Folders</div></div>
          <div className="scan-counter"><div className="v">{formatBytes(status.bytes)}</div><div className="l">Total size</div></div>
          <div className="scan-counter"><div className="v">{formatDuration(status.elapsed)}</div><div className="l">Elapsed</div></div>
        </div>

        <div className="scan-path" title={status.current_path}>
          {building ? "Sorting folders by size and building the index…" : status.current_path || "Starting…"}
        </div>

        <div className="dialog-foot">
          <span className="hint" style={{ marginRight: "auto", alignSelf: "center" }}>
            Read-only: nothing on your disk is changed.
          </span>
          <button
            className="btn danger"
            disabled={cancelling || building}
            onClick={async () => {
              setCancelling(true);
              try { await cancelScan(); } catch (e: any) { notify(e.message, true); }
              setCancelling(false);
            }}
          >
            {cancelling ? "Stopping…" : "Cancel scan"}
          </button>
        </div>
      </div>
    </div>
  );
}
