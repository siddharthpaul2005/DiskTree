import { defineConfig } from "@playwright/test";
import { mkdirSync, rmSync, writeFileSync, existsSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

// End-to-end: the real C++ engine + the real UI (Vite) in Microsoft Edge.
// The engine scans a small generated folder whose sizes are known exactly.

const ENGINE_PORT = 47901;
const TOKEN = "e2e-token";
const repo = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const engine = join(repo, "build", "release", process.platform === "win32" ? "disktree.exe" : "disktree");

if (!process.env.E2E_FIXTURE) {
  const root = join(tmpdir(), `disktree-e2e-${process.pid}`);
  rmSync(root, { recursive: true, force: true });
  const file = (rel: string, size: number) => {
    const p = join(root, rel);
    mkdirSync(join(p, ".."), { recursive: true });
    writeFileSync(p, Buffer.alloc(size, 7));
  };
  file("Videos/holiday-2025.mp4", 4_000_000);
  file("Videos/clips/intro.mov", 1_000_000);
  file("Games/big-world/data.pak", 3_000_000);
  file("Games/big-world/save.dat", 20_000);
  file("Documents/report-final.pdf", 600_000);
  file("Documents/notes.txt", 4_000);
  file("Downloads/ubuntu-test.iso", 2_000_000);
  file("readme.md", 1_000);
  process.env.E2E_FIXTURE = root;
  process.env.E2E_DB = join(tmpdir(), `disktree-e2e-${process.pid}.db`);
}
if (!existsSync(engine)) throw new Error(`Build the engine first: ${engine}`);

export const E2E = { port: ENGINE_PORT, token: TOKEN };

export default defineConfig({
  testDir: "e2e",
  timeout: 60_000,
  fullyParallel: false,
  workers: 1,
  reporter: [["list"]],
  use: {
    baseURL: `http://127.0.0.1:1420/?port=${ENGINE_PORT}&token=${TOKEN}`,
    channel: "msedge",
    viewport: { width: 1400, height: 880 },
    screenshot: "only-on-failure",
  },
  webServer: [
    {
      command: `"${engine}" serve --port ${ENGINE_PORT} --token ${TOKEN} --db "${process.env.E2E_DB}"`,
      url: `http://127.0.0.1:${ENGINE_PORT}/api/health`,
      reuseExistingServer: false,
      timeout: 20_000,
    },
    {
      command: "npm run dev",
      url: "http://127.0.0.1:1420",
      reuseExistingServer: true,
      timeout: 60_000,
    },
  ],
});
