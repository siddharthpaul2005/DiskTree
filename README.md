# 🌳 disktree

A fast, cross-platform disk usage analyzer — a C++ alternative to WizTree.

Scans your entire drive in seconds and shows you exactly what's eating your disk space, with three interfaces: a CLI, an interactive TUI, and a web UI with a live treemap.

![disktree web UI](assets/demo.png)

## Features

- **Blazing fast** — scans 230,000 files across 210 GB in ~10 seconds
- **Three interfaces** — CLI, interactive TUI, browser-based web UI
- **D3 treemap** — proportional visualization, click to drill down
- **Extension breakdown** — see exactly which file types dominate
- **Export** — JSON and CSV output for further analysis
- **Snapshots** — save scans and diff them later
- **Cross-platform** — Windows, macOS, Linux
- **Zero runtime deps** — single binary, no installer needed

## Quick Start

```bash
# Scan current directory
disktree .

# Scan a drive
disktree D:\

# Launch interactive TUI
disktree D:\ --tui

# Launch web UI (opens browser automatically)
disktree D:\ --web

# Export to JSON
disktree D:\ --export json --output scan.json

# Show only .mp4 files
disktree D:\ --ext .mp4

# Top 50 largest files, depth 5
disktree D:\ --top 50 --depth 5
```

## CLI Reference

disktree [OPTIONS] [path]
path                    Directory to scan (default: current directory)
-d, --depth INT         Tree display depth, 0 = unlimited (default: 3)
-n, --top INT           Number of top files/dirs to show (default: 20)
-s, --sort TEXT         Sort by: size|name|count|modified (default: size)
--min-size TEXT     Minimum file size, e.g. 1MB, 500KB
--ext TEXT          Filter by extension, e.g. .mp4
--export TEXT       Export format: json|csv
-o, --output TEXT       Output file path for export
--no-tree           Skip tree view
--snapshot          Save snapshot for later diffing
--diff TEXT         Diff against a saved snapshot
--tui               Launch interactive TUI
--web               Launch web UI in browser
--port INT          Web UI port (default: 7821)
--version           Show version
-h, --help              Show help

## TUI Controls

| Key | Action |
|-----|--------|
| `↑` `↓` / `j` `k` | Navigate |
| `Enter` | Drill into directory |
| `Backspace` | Go up |
| `/` | Filter |
| `Esc` | Clear filter |
| `PgUp` `PgDn` | Scroll fast |
| `Q` | Quit |

## Web UI

Launch with `--web`. Opens `http://localhost:7821` in your browser.

- Click any rectangle to drill into that directory
- `Backspace` or `Esc` to go up
- Hover for size and percentage details
- Extension breakdown in the sidebar

## Building from Source

**Requirements:**
- CMake 3.28+
- MSVC 2022 / GCC 14 / Clang 18
- vcpkg

```bash
git clone https://github.com/YOUR_USERNAME/disktree.git
cd disktree
cmake --preset dev
cmake --build --preset dev
```

**Dependencies** (managed by vcpkg):
- [CLI11](https://github.com/CLIUtils/CLI11) — argument parsing
- [fmt](https://github.com/fmtlib/fmt) — formatting
- [ftxui](https://github.com/ArthurSonzogni/FTXUI) — TUI
- [cpp-httplib](https://github.com/yhirose/cpp-httplib) — HTTP server
- [nlohmann/json](https://github.com/nlohmann/json) — JSON
- [Catch2](https://github.com/catchorg/Catch2) — testing
- [zstd](https://github.com/facebook/zstd) — compression

## Architecture
disktree/
├── include/disktree/
│   ├── core/          # Scanner, Builder, Index, models
│   ├── export/        # JSON, CSV exporters
│   ├── interfaces/    # TUI (ftxui), Web (cpp-httplib)
│   └── utils/         # ThreadPool, Progress, Humanize
├── src/               # Implementations
└── tests/             # Catch2 unit tests

**Data flow:**
Filesystem → Scanner → Builder → Index → Interface (CLI/TUI/Web)

- **Scanner** — iterative DFS, handles Unicode paths, permission errors
- **Builder** — bottom-up rollup of sizes/counts, sorts children
- **Index** — flat queryable structure, O(1) top-N queries
- **Arena** — all nodes allocated from a single memory pool

## License

MIT