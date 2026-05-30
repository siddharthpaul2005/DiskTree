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
  :root {
    --bg-main: #0b0c10;
    --bg-panel: #141518;
    --bg-hover: #1f2025;
    --border: #24262b;
    --text-main: #e2e8f0;
    --text-muted: #94a3b8;
    --accent: #3b82f6;
    --success: #10b981;
    --warning: #f59e0b;
    --font-ui: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
    --font-mono: Consolas, "Courier New", monospace;
  }

  * { margin: 0; padding: 0; box-sizing: border-box; }

  body {
    font-family: var(--font-ui);
    background: var(--bg-main);
    color: var(--text-main);
    height: 100vh;
    display: flex;
    flex-direction: column;
    overflow: hidden;
  }

  /* Header */
  #header {
    background: var(--bg-panel);
    border-bottom: 1px solid var(--border);
    padding: 8px 16px;
    display: flex;
    align-items: center;
    gap: 16px;
    flex-shrink: 0;
    height: 48px;
  }

  #header h1 {
    font-size: 16px;
    font-weight: 600;
    color: var(--text-main);
    display: flex;
    align-items: center;
    gap: 8px;
    margin-right: 16px;
  }

  #header .path {
    font-size: 13px;
    color: var(--text-main);
    font-family: var(--font-mono);
    flex: 1;
    background: #000;
    padding: 4px 8px;
    border-radius: 4px;
    border: 1px solid var(--border);
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }

  .stats-group {
    display: flex;
    gap: 16px;
    font-size: 12px;
  }

  .stat {
    display: flex;
    align-items: center;
    gap: 6px;
    color: var(--text-muted);
  }
  
  .stat-val {
    color: var(--text-main);
    font-family: var(--font-mono);
    font-weight: 500;
  }

  /* Toolbar / Breadcrumb */
  #toolbar {
    background: var(--bg-panel);
    border-bottom: 1px solid var(--border);
    padding: 6px 16px;
    font-size: 13px;
    color: var(--text-muted);
    flex-shrink: 0;
    display: flex;
    align-items: center;
    gap: 4px;
  }

  #toolbar span.segment {
    cursor: pointer;
    padding: 4px 6px;
    border-radius: 4px;
  }

  #toolbar span.segment:hover { 
    background: var(--bg-hover);
    color: var(--text-main);
  }
  
  #toolbar .separator {
    color: var(--border);
  }

  /* Main Area */
  #main {
    display: flex;
    flex: 1;
    overflow: hidden;
  }

  #treemap-container {
    flex: 1;
    position: relative;
    overflow: hidden;
    background: var(--bg-main);
  }

  #treemap-container svg {
    width: 100%;
    height: 100%;
    display: block;
  }

  .node rect {
    stroke: var(--bg-main);
    stroke-width: 1px;
    cursor: pointer;
    transition: filter 0.1s;
  }

  .node rect:hover { 
    filter: brightness(1.2);
    stroke: #ffffff;
    stroke-width: 1px;
  }

  .node text {
    fill: #ffffff;
    font-size: 12px;
    font-family: var(--font-ui);
    pointer-events: none;
    text-shadow: 0 1px 2px rgba(0,0,0,0.8);
  }

  .node text.size-label {
    font-size: 11px;
    fill: rgba(255,255,255,0.8);
    font-family: var(--font-mono);
  }

  /* Sidebar */
  #sidebar {
    width: 340px;
    background: var(--bg-panel);
    border-left: 1px solid var(--border);
    display: flex;
    flex-direction: column;
    overflow: hidden;
    flex-shrink: 0;
  }

  .sidebar-header {
    padding: 8px 12px;
    font-size: 11px;
    font-weight: 600;
    color: var(--text-muted);
    text-transform: uppercase;
    letter-spacing: 0.05em;
    border-bottom: 1px solid var(--border);
    background: var(--bg-main);
  }

  #file-list {
    overflow-y: auto;
    flex: 1;
  }

  #file-list::-webkit-scrollbar,
  #ext-panel::-webkit-scrollbar { width: 8px; }
  #file-list::-webkit-scrollbar-track,
  #ext-panel::-webkit-scrollbar-track { background: var(--bg-panel); }
  #file-list::-webkit-scrollbar-thumb,
  #ext-panel::-webkit-scrollbar-thumb { background: #333; border-radius: 4px; border: 2px solid var(--bg-panel); }

  .file-row {
    display: flex;
    align-items: center;
    padding: 6px 12px;
    font-size: 12px;
    cursor: pointer;
    border-bottom: 1px solid var(--border);
  }

  .file-row:hover { 
    background: var(--bg-hover); 
  }

  .file-row .icon { 
    margin-right: 8px; 
    color: var(--text-muted);
    display: flex;
    align-items: center;
  }
  
  .file-row .name { 
    flex: 1;
    overflow: hidden; 
    text-overflow: ellipsis; 
    white-space: nowrap; 
    color: var(--text-main); 
  }
  
  .file-row .size { 
    color: var(--text-muted); 
    font-family: var(--font-mono);
    text-align: right;
    min-width: 60px;
  }

  /* Extensions */
  #ext-wrapper {
    display: flex;
    flex-direction: column;
    height: 250px;
    border-top: 1px solid var(--border);
  }

  #ext-panel {
    overflow-y: auto;
    padding: 8px 12px;
    flex: 1;
  }

  .ext-row {
    display: flex;
    align-items: center;
    margin-bottom: 6px;
    font-size: 11px;
  }

  .ext-name { 
    width: 60px; 
    color: var(--text-main); 
    font-family: var(--font-mono); 
  }
  .ext-bar-bg { 
    flex: 1; 
    height: 14px; 
    background: var(--bg-main); 
    margin: 0 8px; 
    border: 1px solid var(--border);
  }
  .ext-bar { 
    height: 100%; 
    background: var(--accent); 
  }
  .ext-size { 
    width: 60px; 
    text-align: right; 
    color: var(--text-muted); 
    font-family: var(--font-mono);
  }

  /* Tooltip */
  #tooltip {
    position: fixed;
    background: #000;
    border: 1px solid #333;
    padding: 8px 12px;
    font-size: 12px;
    pointer-events: none;
    display: none;
    z-index: 100;
    box-shadow: 0 4px 12px rgba(0,0,0,0.5);
    color: #fff;
    white-space: nowrap;
  }

  #tooltip .tt-name { font-weight: 600; margin-bottom: 4px; }
  #tooltip .tt-meta { font-family: var(--font-mono); color: #ccc; }

  /* Footer Note */
  #footer-note {
    padding: 12px;
    font-size: 11px;
    color: var(--text-muted);
    text-align: center;
    border-top: 1px solid var(--border);
    background: var(--bg-panel);
    flex-shrink: 0;
    line-height: 1.4;
  }

  /* Loading */
  #loading {
    position: fixed;
    inset: 0;
    background: var(--bg-main);
    display: flex;
    align-items: center;
    justify-content: center;
    font-size: 14px;
    color: var(--text-muted);
    z-index: 200;
  }
</style>
</head>
<body>

<div id="loading">Scanning Directory...</div>

<div id="header" style="display:none">
  <h1>🌳 disktree</h1>
  <div class="path" id="header-path"></div>
  <div class="stats-group">
    <div class="stat">Size: <span class="stat-val" id="stat-size"></span></div>
    <div class="stat">Files: <span class="stat-val" id="stat-files"></span></div>
    <div class="stat">Time: <span class="stat-val" id="stat-time"></span></div>
  </div>
</div>

<div id="toolbar" style="display:none"></div>

<div id="main" style="display:none">
  <div id="treemap-container"></div>
  <div id="sidebar">
    <div class="sidebar-header">Contents</div>
    <div id="file-list"></div>
    <div id="ext-wrapper">
      <div class="sidebar-header">Extensions</div>
      <div id="ext-panel">
        <div id="ext-bars"></div>
      </div>
    </div>
    <div id="footer-note">
      Thanks for using disktree! If you spot any discrepancies or have feedback, please let me know so I can fix it and make this even better for you. ❤️
      - Siddharth Paul
      </div>
  </div>
</div>

<div id="tooltip">
  <div class="tt-name" id="tt-name"></div>
  <div class="tt-meta">
    <span id="tt-size"></span> (<span id="tt-pct"></span>)
  </div>
</div>

<script>
// State
let rootData = null;
let currentNode = null;
let navStack = [];
let extensions = [];

// Professional Categorical Palette (Tableau 10 inspired)
const profColors = [
  '#4e79a7', '#f28e2c', '#e15759', '#76b7b2', '#59a14f', 
  '#edc949', '#af7aa1', '#ff9da7', '#9c755f', '#bab0ab'
];
const colorScale = d3.scaleOrdinal(profColors);

function getColor(d, i) {
  return colorScale(i % profColors.length);
}

function fmtBytes(b) {
  if (b < 1024) return b + ' B';
  if (b < 1024*1024) return (b/1024).toFixed(2) + ' KB';
  if (b < 1024*1024*1024) return (b/1024/1024).toFixed(2) + ' MB';
  return (b/1024/1024/1024).toFixed(2) + ' GB';
}

function fmtPct(p) { return (p * 100).toFixed(1) + '%'; }

// Icons SVG
const iconFolder = '<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M22 19a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h5l2 3h9a2 2 0 0 1 2 2z"></path></svg>';
const iconFile = '<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M13 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V9z"></path><polyline points="13 2 13 9 20 9"></polyline></svg>';

function renderTreemap(node) {
  currentNode = node;
  const container = document.getElementById('treemap-container');
  container.innerHTML = '';

  const W = container.clientWidth;
  const H = container.clientHeight;
  if (!W || !H) return;

  const children = [
    ...(node.children || []).filter(c => c.type === 'dir'),
    ...(node.children || []).filter(c => c.type === 'file'),
  ];

  if (children.length === 0) {
    container.innerHTML = '<div style="padding:40px;color:#666;text-align:center;font-size:13px;">Empty Directory</div>';
    return;
  }

  const hier = d3.hierarchy({ children })
    .sum(d => d.size || 0)
    .sort((a, b) => b.value - a.value);

  d3.treemap()
    .size([W, H])
    .paddingOuter(2)
    .paddingInner(1)
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
    .on('mousemove', (e, d) => {
      tooltip.style.display = 'block';
      tooltip.style.left = (e.clientX + 16) + 'px';
      tooltip.style.top  = (e.clientY + 16) + 'px';
      
      const rect = tooltip.getBoundingClientRect();
      if (rect.right > window.innerWidth) {
        tooltip.style.left = (e.clientX - rect.width - 16) + 'px';
      }
      if (rect.bottom > window.innerHeight) {
        tooltip.style.top = (e.clientY - rect.height - 16) + 'px';
      }

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
    if (w < 40 || h < 25) return;

    const g = d3.select(this);
    const name = d.data.name;
    const label = name.length > Math.floor(w/7) ? name.substr(0, Math.floor(w/7)) + '…' : name;

    g.append('text')
      .attr('x', 4).attr('y', 14)
      .text(label);

    if (h > 30) {
      g.append('text')
        .attr('class', 'size-label')
        .attr('x', 4).attr('y', 26)
        .text(fmtBytes(d.data.size));
    }
  });

  renderSidebar(node);
  updateBreadcrumb();
}

function drillInto(node) {
  if (!node.children || node.children.length === 0) {
    document.getElementById('loading').style.display = 'flex';
    fetch('/api/node?path=' + encodeURIComponent(node.path))
      .then(r => r.json())
      .then(data => {
        document.getElementById('loading').style.display = 'none';
        navStack.push(currentNode);
        renderTreemap(data);
      })
      .catch(e => {
        document.getElementById('loading').style.display = 'none';
        alert('Error loading directory: ' + e.message);
      });
  } else {
    navStack.push(currentNode);
    renderTreemap(node);
  }
}

function goUp() {
  if (navStack.length > 0) {
    const parent = navStack.pop();
    renderTreemap(parent);
  }
}

function updateBreadcrumb() {
  const bc = document.getElementById('toolbar');
  const parts = [];
  const nodes = [...navStack, currentNode];
  
  parts.push('<span class="segment" onclick="goToIndex(0)">[Root]</span>');
  for (let i = 1; i < nodes.length; i++) {
    parts.push('<span class="separator">›</span>');
    parts.push('<span class="segment" onclick="goToIndex(' + i + ')">' + nodes[i].name + '</span>');
  }

  bc.innerHTML = parts.join('');
}

function goToIndex(i) {
  const nodes = [...navStack, currentNode];
  const target = nodes[i];
  navStack = nodes.slice(0, i);
  renderTreemap(target);
}

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
    
    row.innerHTML =
      '<div class="icon">' + (item.type === 'dir' ? iconFolder : iconFile) + '</div>' +
      '<div class="name" title="' + item.path + '">' + item.name + '</div>' +
      '<div class="size">' + fmtBytes(item.size) + '</div>';

    if (item.type === 'dir') {
      row.onclick = () => drillInto(item);
    }

    list.appendChild(row);
  });
}

function renderExtensions(exts) {
  const container = document.getElementById('ext-bars');
  container.innerHTML = '';
  if (!exts.length) {
    container.innerHTML = '<div style="color:var(--text-muted);font-size:11px;padding:8px;">No files</div>';
    return;
  }

  const maxBytes = exts[0].bytes;
  exts.slice(0, 15).forEach(e => {
    const pct = (e.bytes / maxBytes) * 100;
    const row = document.createElement('div');
    row.className = 'ext-row';
    
    row.innerHTML =
      '<div class="ext-name" title="' + e.ext + '">' + (e.ext || '(none)') + '</div>' +
      '<div class="ext-bar-bg"><div class="ext-bar" style="width:' + pct + '%"></div></div>' +
      '<div class="ext-size">' + fmtBytes(e.bytes) + '</div>';
      
    container.appendChild(row);
  });
}

async function boot() {
  try {
    const res  = await fetch('/api/scan');
    const data = await res.json();

    rootData   = data.tree;
    extensions = data.extensions || [];

    document.getElementById('header-path').textContent = data.scan_path;
    document.getElementById('header-path').title = data.scan_path;
    document.getElementById('stat-size').textContent   = fmtBytes(data.total_size);
    document.getElementById('stat-files').textContent  = data.total_files.toLocaleString();
    document.getElementById('stat-time').textContent   = data.elapsed_sec.toFixed(2) + 's';

    document.getElementById('loading').style.display  = 'none';
    document.getElementById('header').style.display   = 'flex';
    document.getElementById('toolbar').style.display  = 'flex';
    document.getElementById('main').style.display     = 'flex';

    renderExtensions(extensions);
    renderTreemap(rootData);

  } catch(e) {
    document.getElementById('loading').textContent = 'Error loading data: ' + e.message;
  }
}

window.addEventListener('resize', () => {
  if (currentNode) renderTreemap(currentNode);
});

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