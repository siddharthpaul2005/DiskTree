// Copies the C++ engine (build/release/disktree.exe) to where Tauri's
// bundler expects a sidecar: src-tauri/binaries/disktree-<target-triple>.exe
import { copyFileSync, existsSync, mkdirSync } from "node:fs";
import { execSync } from "node:child_process";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const repo = resolve(here, "..", "..");
const ext = process.platform === "win32" ? ".exe" : "";
const src = join(repo, "build", "release", `disktree${ext}`);

if (!existsSync(src)) {
  console.error(`Engine not built: ${src}\nRun first:  powershell -ExecutionPolicy Bypass -File scripts\build.ps1`);
  process.exit(1);
}
const triple = execSync("rustc --print host-tuple").toString().trim();
const outDir = join(here, "..", "src-tauri", "binaries");
mkdirSync(outDir, { recursive: true });
const dest = join(outDir, `disktree-engine-${triple}${ext}`);
copyFileSync(src, dest);
console.log(`Sidecar ready: ${dest}`);
