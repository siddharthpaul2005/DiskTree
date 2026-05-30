#include "disktree/interfaces/web/server.hpp"
#include "disktree/utils/humanize.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <fmt/format.h>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif
#include <queue>
#include <string>

using json = nlohmann::json;
namespace hu = disktree::utils;

namespace disktree::web {

// ── JSON builders ─────────────────────────────────────────────────

static json dir_to_json(const DirNode* node, int depth = 0, int max_depth = 3) {
    json j;
    j["name"]       = std::string(node->name);
    j["path"]       = node->path;
    j["size"]       = node->size;
    j["file_count"] = node->file_count;
    j["dir_count"]  = node->dir_count;
    j["pct"]        = node->pct_of_parent;
    j["type"]       = "dir";
    j["size_str"]   = hu::format_bytes(node->size);

    if (depth < max_depth) {
        json children = json::array();
        for (const auto* d : node->child_dirs)
            children.push_back(dir_to_json(d, depth + 1, max_depth));
        for (const auto* f : node->child_files) {
            json fj;
            fj["name"]     = std::string(f->name);
            fj["path"]     = f->path;
            fj["size"]     = f->size;
            fj["size_str"] = hu::format_bytes(f->size);
            fj["ext"]      = std::string(f->extension);
            fj["pct"]      = f->pct_of_parent;
            fj["type"]     = "file";
            children.push_back(fj);
        }
        j["children"] = children;
    } else {
        j["children"] = json::array();
    }
    return j;
}

// ── Embedded HTML ─────────────────────────────────────────────────
// The entire frontend in one string — no files needed on disk

static std::string get_html() {
    return R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>disktree</title>
<script src="https://cdnjs.cloudflare.com/ajax/libs/d3/7.8.5/d3.min.js"></script>
<style>
  * { margin: 0; padding: 0; box-sizing: border-box; }

  body {
    font-family: 'Segoe UI', system-ui, sans-serif;
    background: #0a0a0f;
    color: #e0e0e0;
    height: 100vh;
    display: flex;
    flex-direction: column;
    overflow: hidden;
  }

  #header {
    background: #12121e;
    border-bottom: 1px solid #2a2a4a;
    padding: 12px 20px;
    display: flex;
    align-items: center;
    gap: 16px;
    flex-shrink: 0;
  }

  #header h1 {
    font-size: 18px;
    font-weight: 700;
    color: #50a0ff;
    letter-spacing: 0.05em;
  }

  #header .path {
    font-size: 13px;
    color: #888;
    font-family: monospace;
    flex: 1;
  }

  #header .stat {
    font-size: 12px;
    padding: 3px 10px;
    border-radius: 12px;
    background: #1a1a2e;
    color: #64c864;
    border: 1px solid #2a4a2a;
  }

  #breadcrumb {
    background: #0e0e1a;
    padding: 8px 20px;
    font-size: 12px;
    color: #666;
    flex-shrink: 0;
    border-bottom: 1px solid #1a1a2e;
  }

  #breadcrumb span {
    cursor: pointer;
    color: #50a0ff;
  }

  #breadcrumb span:hover { text-decoration: underline; }

  #main {
    display: flex;
    flex: 1;
    overflow: hidden;
  }

  #treemap-container {
    flex: 1;
    position: relative;
    overflow: hidden;
  }

  #treemap-container svg {
    width: 100%;
    height: 100%;
  }

  .node rect {
    stroke: #0a0a0f;
    stroke-width: 1.5px;
    cursor: pointer;
    transition: opacity 0.15s;
  }

  .node rect:hover { opacity: 0.8; }

  .node text {
    fill: white;
    font-size: 11px;
    pointer-events: none;
    font-family: 'Segoe UI', sans-serif;
  }

  .node text.size-label {
    font-size: 10px;
    fill: rgba(255,255,255,0.7);
  }

  #sidebar {
    width: 340px;
    background: #0e0e1a;
    border-left: 1px solid #1a1a2e;
    display: flex;
    flex-direction: column;
    overflow: hidden;
    flex-shrink: 0;
  }

  #sidebar-header {
    padding: 12px 16px;
    background: #12121e;
    border-bottom: 1px solid #1a1a2e;
    font-size: 12px;
    font-weight: 600;
    color: #50a0ff;
    text-transform: uppercase;
    letter-spacing: 0.08em;
  }

  #file-list {
    overflow-y: auto;
    flex: 1;
  }

  #file-list::-webkit-scrollbar { width: 4px; }
  #file-list::-webkit-scrollbar-track { background: #0a0a0f; }
  #file-list::-webkit-scrollbar-thumb { background: #2a2a4a; border-radius: 2px; }

  .file-row {
    display: flex;
    align-items: center;
    padding: 6px 16px;
    border-bottom: 1px solid #111122;
    font-size: 12px;
    cursor: pointer;
    transition: background 0.1s;
  }

  .file-row:hover { background: #1a1a2e; }

  .file-row .icon { margin-right: 8px; font-size: 14px; }
  .file-row .name { flex: 1; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; color: #ccc; }
  .file-row .size { color: #64c864; font-size: 11px; margin-left: 8px; white-space: nowrap; }
  .file-row .pct  { color: #c8a040; font-size: 10px; margin-left: 6px; white-space: nowrap; }

  #ext-panel {
    padding: 12px 16px;
    border-top: 1px solid #1a1a2e;
    max-height: 200px;
    overflow-y: auto;
    flex-shrink: 0;
  }

  #ext-panel h3 {
    font-size: 11px;
    font-weight: 600;
    color: #50a0ff;
    text-transform: uppercase;
    letter-spacing: 0.08em;
    margin-bottom: 8px;
  }

  .ext-row {
    display: flex;
    align-items: center;
    margin-bottom: 5px;
    font-size: 11px;
  }

  .ext-name { width: 55px; color: #888; font-family: monospace; }
  .ext-bar-bg { flex: 1; height: 6px; background: #1a1a2e; border-radius: 3px; margin: 0 8px; }
  .ext-bar { height: 6px; border-radius: 3px; background: #50a0ff; transition: width 0.3s; }
  .ext-size { width: 70px; text-align: right; color: #64c864; }

  #tooltip {
    position: fixed;
    background: #1a1a2e;
    border: 1px solid #2a2a4a;
    border-radius: 6px;
    padding: 8px 12px;
    font-size: 12px;
    pointer-events: none;
    display: none;
    z-index: 100;
    max-width: 280px;
    box-shadow: 0 4px 20px rgba(0,0,0,0.5);
  }

  #tooltip .tt-name { font-weight: 600; color: #fff; margin-bottom: 4px; }
  #tooltip .tt-size { color: #64c864; }
  #tooltip .tt-pct  { color: #c8a040; }

  #loading {
    position: fixed;
    inset: 0;
    background: #0a0a0f;
    display: flex;
    align-items: center;
    justify-content: center;
    font-size: 18px;
    color: #50a0ff;
    z-index: 200;
  }

  .spinner {
    width: 32px; height: 32px;
    border: 3px solid #1a1a2e;
    border-top-color: #50a0ff;
    border-radius: 50%;
    animation: spin 0.8s linear infinite;
    margin-right: 16px;
  }

  @keyframes spin { to { transform: rotate(360deg); } }
</style>
</head>
<body>

<div id="loading">
  <div class="spinner"></div>
  Loading scan data...
</div>

<div id="header" style="display:none">
  <h1>🌳 disktree</h1>
  <div class="path" id="header-path"></div>
  <div class="stat" id="stat-size"></div>
  <div class="stat" id="stat-files"></div>
  <div class="stat" id="stat-time"></div>
</div>

<div id="breadcrumb" style="display:none"></div>

<div id="main" style="display:none">
  <div id="treemap-container"></div>
  <div id="sidebar">
    <div id="sidebar-header">Contents</div>
    <div id="file-list"></div>
    <div id="ext-panel">
      <h3>Extensions</h3>
      <div id="ext-bars"></div>
    </div>
  </div>
</div>

<div id="tooltip">
  <div class="tt-name" id="tt-name"></div>
  <div class="tt-size" id="tt-size"></div>
  <div class="tt-pct"  id="tt-pct"></div>
</div>

<script>
// ── State ──────────────────────────────────────────────────────────
let rootData = null;
let currentNode = null;
let navStack = [];
let extensions = [];

// ── Color scale ───────────────────────────────────────────────────
const colorScale = d3.scaleOrdinal([
  '#1e4d8c','#2d6a4f','#6d3a1e','#4a1e6d',
  '#1e5c6d','#6d5c1e','#3d1e6d','#1e6d4a',
  '#5c1e4a','#2d4a6d','#4a6d2d','#6d2d4a'
]);

function getColor(d, i) {
  return colorScale(i % 12);
}

// ── Format helpers ────────────────────────────────────────────────
function fmtBytes(b) {
  if (b < 1024) return b + ' B';
  if (b < 1024*1024) return (b/1024).toFixed(2) + ' KB';
  if (b < 1024*1024*1024) return (b/1024/1024).toFixed(2) + ' MB';
  return (b/1024/1024/1024).toFixed(2) + ' GB';
}

function fmtPct(p) { return (p * 100).toFixed(1) + '%'; }

// ── Treemap renderer ──────────────────────────────────────────────
function renderTreemap(node) {
  currentNode = node;
  const container = document.getElementById('treemap-container');
  container.innerHTML = '';

  const W = container.clientWidth;
  const H = container.clientHeight;
  if (!W || !H) return;

  // Build D3 hierarchy from children
  const children = [
    ...(node.children || []).filter(c => c.type === 'dir'),
    ...(node.children || []).filter(c => c.type === 'file'),
  ];

  if (children.length === 0) {
    container.innerHTML = '<div style="padding:40px;color:#666;text-align:center">No subdirectories</div>';
    return;
  }

  const hier = d3.hierarchy({ children })
    .sum(d => d.size || 0)
    .sort((a, b) => b.value - a.value);

  d3.treemap()
    .size([W, H])
    .paddingOuter(3)
    .paddingInner(2)
    .round(true)(hier);

  const svg = d3.select('#treemap-container')
    .append('svg')
    .attr('width', W)
    .attr('height', H);

  const tooltip = document.getElementById('tooltip');

  const cell = svg.selectAll('g')
    .data(hier.leaves())
    .join('g')
    .attr('class', 'node')
    .attr('transform', d => `translate(${d.x0},${d.y0})`);

  cell.append('rect')
    .attr('width',  d => Math.max(0, d.x1 - d.x0))
    .attr('height', d => Math.max(0, d.y1 - d.y0))
    .attr('fill',   (d, i) => getColor(d, i))
    .attr('rx', 2)
    .on('mousemove', (e, d) => {
      tooltip.style.display = 'block';
      tooltip.style.left = (e.clientX + 14) + 'px';
      tooltip.style.top  = (e.clientY + 14) + 'px';
      document.getElementById('tt-name').textContent = d.data.name;
      document.getElementById('tt-size').textContent = fmtBytes(d.data.size);
      document.getElementById('tt-pct').textContent  = fmtPct(d.data.pct);
    })
    .on('mouseleave', () => { tooltip.style.display = 'none'; })
    .on('click', (e, d) => {
      if (d.data.type === 'dir') drillInto(d.data);
    });

  // Labels
  cell.each(function(d) {
    const w = d.x1 - d.x0;
    const h = d.y1 - d.y0;
    if (w < 40 || h < 30) return;

    const g = d3.select(this);
    const name = d.data.name;
    const label = name.length > Math.floor(w/7) ? name.substr(0, Math.floor(w/7)) + '…' : name;

    g.append('text')
      .attr('x', 5).attr('y', 16)
      .text(label);

    if (h > 36) {
      g.append('text')
        .attr('class', 'size-label')
        .attr('x', 5).attr('y', 30)
        .text(fmtBytes(d.data.size));
    }
  });

  // Update sidebar
  renderSidebar(node);
  updateBreadcrumb();
}

// ── Drill into directory ──────────────────────────────────────────
function drillInto(node) {
  if (!node.children || node.children.length === 0) {
    // Fetch deeper data
    fetch('/api/node?path=' + encodeURIComponent(node.path))
      .then(r => r.json())
      .then(data => {
        navStack.push(currentNode);
        renderTreemap(data);
      });
  } else {
    navStack.push(currentNode);
    renderTreemap(node);
  }
}

// ── Go up ─────────────────────────────────────────────────────────
function goUp() {
  if (navStack.length > 0) {
    const parent = navStack.pop();
    renderTreemap(parent);
  }
}

// ── Breadcrumb ────────────────────────────────────────────────────
function updateBreadcrumb() {
  const bc = document.getElementById('breadcrumb');
  const parts = [];

  // Build path from nav stack + current
  const nodes = [...navStack, currentNode];
  parts.push('<span onclick="goToIndex(0)">root</span>');
  for (let i = 1; i < nodes.length; i++) {
    const idx = i;
    parts.push('<span onclick="goToIndex(' + idx + ')">' +
               nodes[i].name + '</span>');
  }

  bc.innerHTML = parts.join(' › ');
}

function goToIndex(i) {
  const nodes = [...navStack, currentNode];
  const target = nodes[i];
  navStack = nodes.slice(0, i);
  renderTreemap(target);
}

// ── Sidebar ───────────────────────────────────────────────────────
function renderSidebar(node) {
  const list = document.getElementById('file-list');
  list.innerHTML = '';

  const items = [
    ...(node.children || []).filter(c => c.type === 'dir'),
    ...(node.children || []).filter(c => c.type === 'file'),
  ];

  items.forEach(item => {
    const row = document.createElement('div');
    row.className = 'file-row';

    const icon = item.type === 'dir' ? '📁' : '📄';
    const name = item.name;
    const size = fmtBytes(item.size);
    const pct  = fmtPct(item.pct);

    row.innerHTML =
      '<span class="icon">' + icon + '</span>' +
      '<span class="name" title="' + item.path + '">' + name + '</span>' +
      '<span class="size">' + size + '</span>' +
      '<span class="pct">' + pct + '</span>';

    if (item.type === 'dir') {
      row.onclick = () => drillInto(item);
    }

    list.appendChild(row);
  });
}

// ── Extension bars ────────────────────────────────────────────────
function renderExtensions(exts) {
  const container = document.getElementById('ext-bars');
  container.innerHTML = '';
  if (!exts.length) return;

  const maxBytes = exts[0].bytes;
  exts.slice(0, 12).forEach(e => {
    const pct = (e.bytes / maxBytes) * 100;
    const row = document.createElement('div');
    row.className = 'ext-row';
    row.innerHTML =
      '<div class="ext-name">' + e.ext + '</div>' +
      '<div class="ext-bar-bg"><div class="ext-bar" style="width:' + pct + '%"></div></div>' +
      '<div class="ext-size">' + fmtBytes(e.bytes) + '</div>';
    container.appendChild(row);
  });
}

// ── Boot ──────────────────────────────────────────────────────────
async function boot() {
  try {
    const res  = await fetch('/api/scan');
    const data = await res.json();

    rootData   = data.tree;
    extensions = data.extensions || [];

    // Header
    document.getElementById('header-path').textContent = data.scan_path;
    document.getElementById('stat-size').textContent   = fmtBytes(data.total_size);
    document.getElementById('stat-files').textContent  = data.total_files.toLocaleString() + ' files';
    document.getElementById('stat-time').textContent   = data.elapsed_sec.toFixed(2) + 's';

    document.getElementById('loading').style.display  = 'none';
    document.getElementById('header').style.display   = '';
    document.getElementById('breadcrumb').style.display = '';
    document.getElementById('main').style.display     = '';

    renderExtensions(extensions);
    renderTreemap(rootData);

  } catch(e) {
    document.getElementById('loading').textContent = 'Error loading data: ' + e.message;
  }
}

// Handle resize
window.addEventListener('resize', () => {
  if (currentNode) renderTreemap(currentNode);
});

// Keyboard nav
window.addEventListener('keydown', e => {
  if (e.key === 'Backspace' || e.key === 'Escape') goUp();
});

boot();
</script>
</body>
</html>)HTML";
}

// ── Server ────────────────────────────────────────────────────────

int run(const ScanResult& result, const Index& index, uint16_t port) {
    httplib::Server srv;

    // ── GET / — serve the frontend ────────────────────────────────
    srv.Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(get_html(), "text/html");
    });

    // ── GET /api/scan — full scan data ────────────────────────────
    srv.Get("/api/scan", [&](const httplib::Request&,
                              httplib::Response& res) {
        json j;
        j["scan_path"]   = result.scan_path.string();
        j["total_size"]  = result.total_size;
        j["total_files"] = result.total_files;
        j["total_dirs"]  = result.total_dirs;
        j["elapsed_sec"] = result.elapsed_sec;

        // Extensions
        json exts = json::array();
        for (const auto& e : index.extensions()) {
            json ej;
            ej["ext"]   = std::string(e.ext.empty() ? "(none)" : e.ext);
            ej["bytes"] = e.total_bytes;
            ej["count"] = e.file_count;
            exts.push_back(ej);
        }
        j["extensions"] = exts;

        // Tree (depth 3 for initial load)
        j["tree"] = dir_to_json(result.root, 0, 3);

        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(j.dump(), "application/json");
    });

    // ── GET /api/node — drill into a specific path ─────────────────
    srv.Get("/api/node", [&](const httplib::Request& req,
                              httplib::Response& res) {
        auto path_param = req.get_param_value("path");

        // Find the node by path — BFS
        const DirNode* found = nullptr;
        std::queue<const DirNode*> q;
        q.push(result.root);
        while (!q.empty() && !found) {
            const DirNode* n = q.front(); q.pop();
            if (n->path == path_param) { found = n; break; }
            for (const auto* d : n->child_dirs) q.push(d);
        }

        if (!found) {
            res.status = 404;
            res.set_content("{\"error\":\"not found\"}", "application/json");
            return;
        }

        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(dir_to_json(found, 0, 2).dump(),
                        "application/json");
    });

    // ── GET /api/top-files ────────────────────────────────────────
    srv.Get("/api/top-files", [&](const httplib::Request& req,
                                   httplib::Response& res) {
        int n = 50;
        if (req.has_param("n"))
            n = std::stoi(req.get_param_value("n"));

        json arr = json::array();
        for (const auto* f : index.top_files(n)) {
            json fj;
            fj["path"]     = f->path;
            fj["name"]     = std::string(f->name);
            fj["size"]     = f->size;
            fj["size_str"] = hu::format_bytes(f->size);
            fj["ext"]      = std::string(f->extension);
            arr.push_back(fj);
        }
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(arr.dump(), "application/json");
    });

    // ── GET /ping — keepalive ─────────────────────────────────────
    srv.Get("/ping", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("pong", "text/plain");
    });

    // Open browser
    std::string url = fmt::format("http://localhost:{}", port);
    fmt::print("\n  disktree web UI\n");
    fmt::print("  Open: {}\n", url);
    fmt::print("  Press Ctrl+C to stop\n\n");

#ifdef _WIN32
    ShellExecuteA(nullptr, "open", url.c_str(),
                  nullptr, nullptr, SW_SHOWNORMAL);
#elif __APPLE__
    system(("open " + url).c_str());
#else
    system(("xdg-open " + url).c_str());
#endif

    fmt::print("  Server running...\n");
    srv.listen("localhost", port);

    return 0;
}

} // namespace disktree::web