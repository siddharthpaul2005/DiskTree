#pragma once

#include "disktree/service/engine.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace disktree::service {

// ════════════════════════════════════════════════════════════════
// Local HTTP API for the desktop app (`disktree serve`).
//
// - Listens on 127.0.0.1 only.
// - If `token` is set, every /api request must carry it, either as the
//   X-DiskTree-Token header or a ?token= query parameter; otherwise 401.
//   This stops other local programs and web pages from using the API.
// - CORS: responses allow only the origins in `allowed_origins`.
// - Prints one line to stdout once listening:
//     {"event":"ready","port":<port>,"pid":<pid>}
//   which is how the desktop app learns the port.
// - With parent_pid set, the server exits when that process exits, so the
//   engine never outlives the app.
//
// Routes (JSON in/out):
//   GET    /api/health                 GET  /api/drives
//   POST   /api/scan {path,...}        POST /api/scan/cancel
//   GET    /api/scan/status
//   GET    /api/node?path=&depth=&max_children=&min_fraction=
//   GET    /api/top-files?n=           GET  /api/top-dirs?n=
//   GET    /api/extensions?n=          GET  /api/search?q=&limit=
//   GET    /api/snapshots              POST /api/snapshots {label}
//   DELETE /api/snapshots/<id>         GET  /api/diff?snapshot=<id>&limit=
//   POST   /api/reveal {path}          (show the item in Explorer)
//   POST   /api/shutdown
// ════════════════════════════════════════════════════════════════
struct ServerOptions {
    uint16_t                 port = 0;            // 0 = any free port
    std::string              token;               // empty = no auth (tests only)
    std::vector<std::string> allowed_origins;
    uint32_t                 parent_pid = 0;      // 0 = don't watch
    // Called with the bound port before the ready line is printed
    std::function<void(int)> on_ready;
    bool                     print_ready = true;
};

// Blocks until shutdown. Returns a process exit code.
int run_api_server(Engine& engine, const ServerOptions& opts);

// Default origins of the Tauri webview and the Vite dev server
std::vector<std::string> default_allowed_origins();

} // namespace disktree::service
