#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX   // keep windows.h from defining min/max macros
#endif
#include <windows.h>
#include <io.h>
#define DT_ISATTY(f) _isatty(_fileno(f))
#else
#include <unistd.h>
#define DT_ISATTY(f) isatty(fileno(f))
#endif

#include "disktree/version.hpp"
#include "disktree/core/scanner.hpp"
#include "disktree/core/builder.hpp"
#include "disktree/core/index.hpp"
#include "disktree/core/snapshot.hpp"
#include "disktree/utils/humanize.hpp"
#include "disktree/utils/progress.hpp"
#include "disktree/export/json_export.hpp"
#include "disktree/export/csv_export.hpp"
#include "disktree/interfaces/tui/app.hpp"
#include "disktree/interfaces/web/server.hpp"

#include <CLI/CLI.hpp>
#include <fmt/format.h>
#include <fmt/color.h>

#include <algorithm>
#include <cctype>
#include <ctime>
#include <fstream>
#include <functional>
#include <optional>
#include <span>
#include <thread>
#include <string>
#include <vector>

namespace hu = disktree::utils;
using disktree::DirNode;
using disktree::FileNode;

// Colours only when writing to a terminal (and NO_COLOR isn't set), so
// `disktree D:\ > report.txt` produces clean text.
static bool g_color_out = true;
static bool g_color_err = true;
static fmt::text_style S(fmt::text_style ts) {
    return g_color_out ? ts : fmt::text_style{};
}

// ── Sorting ──────────────────────────────────────────────────────

enum class SortKey { Size, Name, Count, Modified };

static SortKey parse_sort(const std::string& s) {
    if (s == "name")     return SortKey::Name;
    if (s == "count")    return SortKey::Count;
    if (s == "modified") return SortKey::Modified;
    return SortKey::Size;
}

static const char* sort_label(SortKey k) {
    switch (k) {
        case SortKey::Name:     return "name";
        case SortKey::Count:    return "file count";
        case SortKey::Modified: return "last modified";
        default:                return "size";
    }
}

static bool iless(std::string_view a, std::string_view b) {
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
        [](char x, char y) {
            return std::tolower(static_cast<unsigned char>(x)) <
                   std::tolower(static_cast<unsigned char>(y));
        });
}

static std::function<bool(const DirNode*, const DirNode*)> dir_order(SortKey k) {
    switch (k) {
        case SortKey::Name:
            return [](const DirNode* a, const DirNode* b) { return iless(a->name, b->name); };
        case SortKey::Count:
            return [](const DirNode* a, const DirNode* b) {
                return a->file_count != b->file_count ? a->file_count > b->file_count
                                                      : a->size > b->size;
            };
        case SortKey::Modified:
            return [](const DirNode* a, const DirNode* b) {
                return a->newest_mtime != b->newest_mtime ? a->newest_mtime > b->newest_mtime
                                                          : a->size > b->size;
            };
        default:
            return [](const DirNode* a, const DirNode* b) { return a->size > b->size; };
    }
}

static std::function<bool(const FileNode*, const FileNode*)> file_order(SortKey k) {
    switch (k) {
        case SortKey::Name:
            return [](const FileNode* a, const FileNode* b) { return iless(a->name, b->name); };
        case SortKey::Modified:
            return [](const FileNode* a, const FileNode* b) {
                return a->modified != b->modified ? a->modified > b->modified
                                                  : a->size > b->size;
            };
        default:  // files have no "count": size order
            return [](const FileNode* a, const FileNode* b) { return a->size > b->size; };
    }
}

// Pick n items from a size-sorted list according to the sort key.
//   size / count / modified → the top n by that key
//   name                    → the n largest, listed alphabetically
template <typename T, typename Cmp>
static std::vector<const T*> pick_top(std::span<const T* const> by_size, size_t n,
                                      SortKey key, Cmp cmp) {
    std::vector<const T*> out(by_size.begin(), by_size.end());
    n = std::min(n, out.size());
    if (key == SortKey::Name) {
        out.resize(n);
        std::stable_sort(out.begin(), out.end(), cmp);
    } else if (key != SortKey::Size) {
        std::partial_sort(out.begin(), out.begin() + std::ptrdiff_t(n), out.end(), cmp);
        out.resize(n);
    } else {
        out.resize(n);
    }
    return out;
}

// ── Rendering helpers ────────────────────────────────────────────

static std::string fit_left(std::string p, size_t width) {
    if (p.size() > width) p = "..." + p.substr(p.size() - (width - 3));
    return p;
}

static std::string format_date(int64_t unix_time) {
    if (unix_time <= 0) return "-";
    std::time_t t = static_cast<std::time_t>(unix_time);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm);
    return buf;
}

constexpr int TREE_NAME_COL   = 49;  // width of the name column incl. tree lines
constexpr size_t TREE_MAX_DIRS  = 15;  // per directory, rest are summarised
constexpr size_t TREE_MAX_FILES = 10;

// Cut a UTF-8 string to at most `cols` code points, marking the cut with "~"
static std::string truncate_cols(std::string_view s, size_t cols) {
    size_t cps = 0, i = 0;
    for (; i < s.size(); ++i) {
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) {
            if (cps == cols) break;
            ++cps;
        }
    }
    if (i >= s.size()) return std::string(s);
    // Too long: keep cols-1 code points + "~"
    std::string out(s.substr(0, i));
    size_t j = out.size();
    while (j > 0 && (static_cast<unsigned char>(out[--j]) & 0xC0) == 0x80) {}
    out.resize(j);
    return out + "~";
}

// One tree row. `level` is the depth of the entry (1 = child of root);
// every level adds 4 columns of tree lines before the name.
static void print_row(const std::string& prefix, const char* connector, int level,
                      std::string_view name, const std::string& size, const std::string& pct) {
    size_t width = size_t(std::max(12, TREE_NAME_COL - 4 * level));
    fmt::print("  {}{}{:<{}} {:>12}  {:>6}\n", prefix, connector,
               truncate_cols(name, width), width, size, pct);
}

// Prints the entries below `node` (which sits at depth `node_depth`) like
// `tree -L depth_limit`: everything down to depth_limit levels (0 = all).
// Large directories show their biggest entries and summarise the rest.
static void render_children(const DirNode* node, int depth_limit, int node_depth,
                            const std::string& prefix, SortKey key) {
    if (depth_limit != 0 && node_depth + 1 > depth_limit) return;

    std::vector<const DirNode*>  dirs(node->child_dirs.begin(), node->child_dirs.end());
    std::vector<const FileNode*> files(node->child_files.begin(), node->child_files.end());

    // Builder sorted by size: cap first so we keep the biggest, then re-sort
    uint64_t hidden_bytes = 0;
    size_t   hidden = 0;
    if (dirs.size() > TREE_MAX_DIRS) {
        for (size_t i = TREE_MAX_DIRS; i < dirs.size(); ++i) hidden_bytes += dirs[i]->size;
        hidden += dirs.size() - TREE_MAX_DIRS;
        dirs.resize(TREE_MAX_DIRS);
    }
    if (files.size() > TREE_MAX_FILES) {
        for (size_t i = TREE_MAX_FILES; i < files.size(); ++i) hidden_bytes += files[i]->size;
        hidden += files.size() - TREE_MAX_FILES;
        files.resize(TREE_MAX_FILES);
    }
    if (key != SortKey::Size) {
        std::stable_sort(dirs.begin(), dirs.end(), dir_order(key));
        std::stable_sort(files.begin(), files.end(), file_order(key));
    }

    const int level = node_depth + 1;
    size_t total = dirs.size() + files.size() + (hidden ? 1 : 0);
    size_t idx   = 0;
    for (const DirNode* d : dirs) {
        bool last = (++idx == total);
        std::string label = std::string(d->name) + (d->scan_failed ? " (no access)" : "");
        print_row(prefix, last ? "└── " : "├── ", level, label,
                  hu::format_bytes(d->size), hu::format_pct(d->pct_of_parent));
        render_children(d, depth_limit, level, prefix + (last ? "    " : "│   "), key);
    }
    for (const FileNode* f : files) {
        bool last = (++idx == total);
        print_row(prefix, last ? "└── " : "├── ", level, f->name,
                  hu::format_bytes(f->size), hu::format_pct(f->pct_of_parent));
    }
    if (hidden) {
        float pct = node->size ? float(double(hidden_bytes) / double(node->size)) : 0.0f;
        print_row(prefix, "└── ", level,
                  "... " + hu::format_count(hidden) + " more",
                  hu::format_bytes(hidden_bytes), hu::format_pct(pct));
    }
}

static void render_summary(const disktree::ScanResult& result, size_t threads) {
    fmt::print("\n");
    fmt::print(S(fmt::emphasis::bold), "  {}\n", result.root->path);
    fmt::print("  {:18} {}\n", "Total size:",  hu::format_bytes(result.total_size));
    fmt::print("  {:18} {}\n", "Files:",       hu::format_count(result.total_files));
    fmt::print("  {:18} {}\n", "Directories:", hu::format_count(result.total_dirs));
    fmt::print("  {:18} {} ({} threads)\n\n", "Scan time:",
        hu::format_duration(result.elapsed_sec), threads);
}

static void render_top_files(const disktree::Index& index, size_t n, SortKey key) {
    auto files = pick_top<FileNode>(index.top_files(index.total_files()), n, key,
                                    file_order(key));
    fmt::print(S(fmt::emphasis::bold), "  Top {} Files (by {})\n", files.size(),
               key == SortKey::Count ? "size" : sort_label(key));
    fmt::print("  {:<60} {:>12}  {:>16}\n", "Path", "Size", "Modified");
    fmt::print("  {}\n", std::string(92, '-'));
    for (const auto* f : files)
        fmt::print("  {:<60} {:>12}  {:>16}\n", fit_left(f->path, 60),
            hu::format_bytes(f->size), format_date(f->modified));
    fmt::print("\n");
}

static void render_top_dirs(const disktree::Index& index, size_t n, SortKey key) {
    auto dirs = pick_top<DirNode>(index.top_dirs(index.total_dirs()), n, key,
                                  dir_order(key));
    fmt::print(S(fmt::emphasis::bold), "  Top {} Directories (by {})\n", dirs.size(),
               sort_label(key));
    fmt::print("  {:<60} {:>12} {:>10}\n", "Path", "Size", "Files");
    fmt::print("  {}\n", std::string(84, '-'));
    for (const auto* d : dirs)
        fmt::print("  {:<60} {:>12} {:>10}\n", fit_left(d->path, 60),
            hu::format_bytes(d->size), hu::format_count(d->file_count));
    fmt::print("\n");
}

static void render_extensions(const disktree::Index& index, size_t n) {
    auto exts = index.extensions();
    if (exts.empty()) return;
    fmt::print(S(fmt::emphasis::bold), "  Extensions\n");
    fmt::print("  {:<14} {:>12} {:>10}\n", "Ext", "Size", "Files");
    fmt::print("  {}\n", std::string(38, '-'));
    uint64_t biggest = std::max<uint64_t>(1, exts[0].total_bytes);
    size_t i = 0;
    for (const auto& e : exts) {
        std::string ext = e.ext.empty() ? "(none)" : std::string(e.ext);
        int bar_len = int(20.0 * double(e.total_bytes) / double(biggest));
        std::string bar(size_t(bar_len), '#');
        bar += std::string(size_t(20 - bar_len), ' ');
        fmt::print("  {:<14} {:>12} {:>10}  [{}]\n", ext,
            hu::format_bytes(e.total_bytes), hu::format_count(e.file_count), bar);
        if (++i >= n) break;
    }
    fmt::print("\n");
}

static void render_delta_list(const char* title, const std::vector<disktree::FileDelta>& list,
                              size_t n, fmt::text_style style) {
    if (list.empty()) return;
    fmt::print(S(fmt::emphasis::bold), "  {} ({})\n", title, hu::format_count(list.size()));
    fmt::print("  {:<56} {:>12} {:>12} {:>13}\n", "Path", "Before", "After", "Change");
    fmt::print("  {}\n", std::string(96, '-'));
    size_t shown = 0;
    for (const auto& d : list) {
        fmt::print("  {:<56} {:>12} {:>12} ", fit_left(d.path, 56),
            d.size_before < 0 ? "-" : hu::format_bytes(uint64_t(d.size_before)),
            d.size_after  < 0 ? "-" : hu::format_bytes(uint64_t(d.size_after)));
        fmt::print(S(style), "{:>13}\n", hu::format_signed_bytes(d.delta()));
        if (++shown >= n) break;
    }
    if (list.size() > n)
        fmt::print("  ... and {} more\n", hu::format_count(list.size() - n));
    fmt::print("\n");
}

static void render_diff(const disktree::Snapshot& before, const disktree::DiffResult& d,
                        size_t n) {
    auto sum = [](const std::vector<disktree::FileDelta>& v) {
        int64_t s = 0;
        for (const auto& x : v) s += x.delta();
        return s;
    };
    auto green = fmt::fg(fmt::color::light_green);
    auto red   = fmt::fg(fmt::color::indian_red);

    fmt::print(S(fmt::emphasis::bold), "  Changes since {} ({})\n",
               before.id, format_date(before.created_at));
    fmt::print("  {:18} ", "Net change:");
    fmt::print(S(d.net_delta > 0 ? red : green), "{}\n", hu::format_signed_bytes(d.net_delta));
    fmt::print("  {:18} {} files ({})\n", "Added:",
        hu::format_count(d.added.size()), hu::format_signed_bytes(sum(d.added)));
    fmt::print("  {:18} {} files ({})\n", "Removed:",
        hu::format_count(d.removed.size()), hu::format_signed_bytes(sum(d.removed)));
    fmt::print("  {:18} {} files ({})\n", "Grew:",
        hu::format_count(d.grown.size()), hu::format_signed_bytes(sum(d.grown)));
    fmt::print("  {:18} {} files ({})\n", "Shrank:",
        hu::format_count(d.shrunk.size()), hu::format_signed_bytes(sum(d.shrunk)));
    fmt::print("  {:18} {} files\n\n", "Unchanged:", hu::format_count(d.unchanged));

    if (d.added.empty() && d.removed.empty() && d.grown.empty() && d.shrunk.empty()) {
        fmt::print("  No changes.\n\n");
        return;
    }

    render_delta_list("Biggest changes by folder", d.dirs, n, fmt::emphasis::bold);
    render_delta_list("Added files",   d.added,   n, red);
    render_delta_list("Removed files", d.removed, n, green);
    render_delta_list("Grown files",   d.grown,   n, red);
    render_delta_list("Shrunk files",  d.shrunk,  n, green);
}

static void list_snapshots_cmd(const std::filesystem::path& dir) {
    auto snaps = disktree::list_snapshots(dir);
    fmt::print(S(fmt::emphasis::bold), "\n  Snapshots in {}\n", dir.string());
    if (snaps.empty()) {
        fmt::print("  (none yet - create one with: disktree <path> --snapshot)\n\n");
        return;
    }
    fmt::print("  {:<26} {:<17} {:>12} {:>10}  {}\n", "File", "Taken", "Size", "Files", "Scanned path");
    fmt::print("  {}\n", std::string(90, '-'));
    for (const auto& p : snaps) {
        try {
            auto s = disktree::load_snapshot(p);
            fmt::print("  {:<26} {:<17} {:>12} {:>10}  {}\n", p.filename().string(),
                format_date(s.created_at), hu::format_bytes(s.total_size),
                hu::format_count(s.total_files), s.scan_path);
        } catch (const std::exception& e) {
            fmt::print("  {:<26} (unreadable: {})\n", p.filename().string(), e.what());
        }
    }
    fmt::print("\n");
}

static std::string normalise_ext(std::string ext) {
    std::transform(ext.begin(), ext.end(), ext.begin(),
        [](unsigned char c) { return char(std::tolower(c)); });
    if (ext == "(none)" || ext == "none") return "";
    if (!ext.empty() && ext[0] != '.') ext = "." + ext;
    return ext;
}

// ── main ─────────────────────────────────────────────────────────

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    // Enable ANSI colours on older Windows consoles
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode))
        SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    HANDLE he = GetStdHandle(STD_ERROR_HANDLE);
    if (he != INVALID_HANDLE_VALUE && GetConsoleMode(he, &mode))
        SetConsoleMode(he, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#endif
    const char* no_color = std::getenv("NO_COLOR");
    bool colors_ok = !(no_color && *no_color);
    g_color_out = colors_ok && DT_ISATTY(stdout);
    g_color_err = colors_ok && DT_ISATTY(stderr);

    CLI::App app{"disktree - fast disk usage analyzer", "disktree"};
    app.set_version_flag("--version", std::string("disktree ") + DISKTREE_VERSION);
    app.get_formatter()->column_width(28);

    std::string path        = ".";
    int         depth       = 3;
    int         top_n       = 20;
    std::string sort_by     = "size";
    std::string min_size_str;
    std::string ext_filter;
    std::string export_fmt;
    std::string output_path;
    bool        no_tree     = false;
    bool        snapshot    = false;
    std::string diff_path;
    bool        list_snaps  = false;
    std::vector<std::string> excludes;
    bool        no_default_excludes = false;
    size_t      threads     = 0;
    bool        tui_mode    = false;
    bool        web_mode    = false;
    uint16_t    web_port    = 7821;
    bool        no_browser  = false;

    app.add_option("path", path, "Directory to scan")->default_val(".");
    app.add_option("-d,--depth", depth, "Tree display depth (0 = unlimited)")
       ->default_val(3)->check(CLI::NonNegativeNumber);
    app.add_option("-n,--top", top_n, "Number of top files/dirs to show")
       ->default_val(20)->check(CLI::PositiveNumber);
    app.add_option("-s,--sort", sort_by, "Order tree and top lists by: size|name|count|modified")
       ->default_val("size")
       ->check(CLI::IsMember({"size", "name", "count", "modified"}));
    app.add_option("--min-size", min_size_str, "Skip files smaller than this (e.g. 1MB, 500KB)");
    app.add_option("--ext", ext_filter, "Only list files with this extension (e.g. .mp4)");
    app.add_option("-x,--exclude", excludes,
       "Skip files/dirs with this name; * and ? allowed (repeatable)")
       ->allow_extra_args(false);   // one value per -x, so `-x foo D:\` keeps D:\ as the path
    app.add_flag("--no-default-excludes", no_default_excludes,
       "Also scan $RECYCLE.BIN, System Volume Information, .git, node_modules");
    app.add_option("-j,--threads", threads, "Scanner threads (0 = all CPU cores)")
       ->default_val(0);
    app.add_option("--export", export_fmt, "Export format: json|csv")
       ->check(CLI::IsMember({"json", "csv"}));
    app.add_option("-o,--output", output_path, "Export file path (default: disktree_export.<fmt>)");
    app.add_flag("--no-tree", no_tree, "Skip the tree view");
    app.add_flag("--snapshot", snapshot, "Save the scan to ~/.disktree/snapshots for later --diff");
    app.add_option("--diff", diff_path,
       "Compare against a snapshot file, or 'latest' for the newest snapshot of this path");
    app.add_flag("--list-snapshots", list_snaps, "List saved snapshots and exit");
    app.add_flag("--tui", tui_mode, "Launch the interactive terminal UI");
    app.add_flag("--web", web_mode, "Launch the web UI in your browser");
    app.add_option("--port", web_port, "Web UI port (next free port is used if taken)")
       ->default_val(7821);
    app.add_flag("--no-browser", no_browser, "With --web: don't open a browser automatically");

    CLI11_PARSE(app, argc, argv);

    try {
        auto snap_dir = disktree::default_snapshot_dir();
        if (list_snaps) {
            list_snapshots_cmd(snap_dir);
            return 0;
        }
        if (tui_mode && web_mode)
            throw std::runtime_error("Use either --tui or --web, not both");

        uint64_t min_size = 0;
        if (!min_size_str.empty()) {
            auto parsed = hu::parse_size(min_size_str);
            if (!parsed)
                throw std::runtime_error("Invalid --min-size '" + min_size_str +
                                         "' (examples: 500KB, 1.5MB, 2GB)");
            min_size = *parsed;
        }
        SortKey sort_key = parse_sort(sort_by);

        // Scanner config
        disktree::Scanner::Config config;
        config.min_size  = min_size;
        config.n_threads = threads;
        if (!no_default_excludes)
            config.exclude_patterns = disktree::Scanner::default_excludes();
        config.exclude_patterns.insert(config.exclude_patterns.end(),
                                       excludes.begin(), excludes.end());
        size_t used_threads = threads ? threads
                                      : std::max(1u, std::thread::hardware_concurrency());

        fmt::print(S(fmt::emphasis::bold), "\ndisktree v{}\n", DISKTREE_VERSION);
        fmt::print("Scanning: {}\n", path);

        disktree::utils::ProgressTracker progress;
        disktree::Scanner scanner(progress);
        disktree::ScanResult result;
        {
            std::optional<hu::ProgressReporter> reporter;
            if (DT_ISATTY(stderr)) reporter.emplace(progress);
            result = scanner.scan(path, config);
        }

        disktree::Builder builder;
        builder.build(result);

        disktree::Index index;
        index.build(result);

        // ── Diff target (resolved before a new snapshot is written) ──
        std::optional<disktree::Snapshot> diff_base;
        if (!diff_path.empty()) {
            std::filesystem::path p = diff_path;
            if (diff_path == "latest") {
                auto latest = disktree::latest_snapshot_for(snap_dir, result.root->path);
                if (!latest)
                    throw std::runtime_error("No snapshot of " + result.root->path +
                        " yet. Create one first with: disktree \"" + path + "\" --snapshot");
                p = *latest;
            } else if (!std::filesystem::exists(p) && std::filesystem::exists(snap_dir / p)) {
                p = snap_dir / p;   // allow just the file name
            }
            diff_base = disktree::load_snapshot(p);
        }

        // ── Export / snapshot ─────────────────────────────────────────
        auto do_exports = [&] {
            if (!export_fmt.empty()) {
                std::string content = export_fmt == "json"
                    ? disktree::export_::to_json(result, index)
                    : disktree::export_::to_csv(result, index);
                std::string out = output_path.empty()
                    ? "disktree_export." + export_fmt : output_path;
                std::ofstream f(out, std::ios::binary);
                if (!f) throw std::runtime_error("Cannot write to: " + out);
                f << content;
                fmt::print("  Exported to: {}\n", out);
            }
            if (snapshot) {
                auto saved = disktree::save_snapshot(disktree::snapshot_from_scan(result), snap_dir);
                fmt::print("  Snapshot saved: {}\n", saved.string());
            }
        };

        if (web_mode || tui_mode) {
            do_exports();
            if (web_mode) return disktree::web::run(result, index, web_port, !no_browser);
            return disktree::tui::run(result, index);
        }

        render_summary(result, used_threads);

        if (diff_base) {
            auto current = disktree::snapshot_from_scan(result);
            if (!diff_base->scan_path.empty() &&
                !std::equal(diff_base->scan_path.begin(), diff_base->scan_path.end(),
                            current.scan_path.begin(), current.scan_path.end(),
                            [](char a, char b) { return std::tolower((unsigned char)a) ==
                                                        std::tolower((unsigned char)b); }))
                fmt::print(S(fmt::fg(fmt::color::yellow)),
                    "  Note: snapshot was taken of {} but this scan is of {}\n\n",
                    diff_base->scan_path, current.scan_path);
            render_diff(*diff_base, disktree::diff_snapshots(*diff_base, current), size_t(top_n));
        } else if (!ext_filter.empty()) {
            std::string ext = normalise_ext(ext_filter);
            auto files = index.files_by_ext(ext);
            std::stable_sort(files.begin(), files.end(), file_order(sort_key));
            fmt::print(S(fmt::emphasis::bold), "  Files with extension '{}' ({})\n",
                ext.empty() ? "(none)" : ext, hu::format_count(files.size()));
            fmt::print("  {:<60} {:>12}  {:>16}\n", "Path", "Size", "Modified");
            fmt::print("  {}\n", std::string(92, '-'));
            size_t shown = 0;
            for (const auto* f : files) {
                if (shown++ >= size_t(top_n)) break;
                fmt::print("  {:<60} {:>12}  {:>16}\n", fit_left(f->path, 60),
                    hu::format_bytes(f->size), format_date(f->modified));
            }
            fmt::print("\n");
        } else {
            if (!no_tree) {
                fmt::print(S(fmt::emphasis::bold), "  Directory Tree (depth {}, by {})\n",
                           depth == 0 ? std::string("unlimited") : std::to_string(depth),
                           sort_label(sort_key));
                fmt::print("  {:<49} {:>12}  {:>6}\n", "Path", "Size", "%");
                fmt::print("  {}\n", std::string(70, '-'));
                fmt::print("  {:<49} {:>12}  {:>6}\n",
                    std::string(result.root->name).substr(0, 49),
                    hu::format_bytes(result.root->size), "100.0%");
                render_children(result.root, depth, 0, "", sort_key);
                fmt::print("\n");
            }
            render_top_dirs(index, size_t(top_n), sort_key);
            render_top_files(index, size_t(top_n), sort_key);
            render_extensions(index, 15);
        }

        if (!result.skipped.empty()) {
            fmt::print("  ({} paths skipped: access denied or unreadable)\n\n",
                hu::format_count(result.skipped.size()));
        }

        do_exports();
        if (!export_fmt.empty() || snapshot) fmt::print("\n");

    } catch (const std::exception& e) {
        fmt::print(stderr, g_color_err ? fmt::fg(fmt::color::red) : fmt::text_style{}, "\nError: {}\n", e.what());
        return 1;
    }

    return 0;
}
