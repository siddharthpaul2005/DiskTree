#ifdef _WIN32
#include <windows.h>
#endif

#include "disktree/core/scanner.hpp"
#include "disktree/core/builder.hpp"
#include "disktree/core/index.hpp"
#include "disktree/utils/humanize.hpp"
#include "disktree/utils/progress.hpp"
#include "disktree/export/json_export.hpp"
#include "disktree/export/csv_export.hpp"

#include <CLI/CLI.hpp>
#include <fmt/format.h>
#include <fmt/color.h>

#include <iostream>
#include <fstream>
#include <string>
#include <algorithm>

// ── Rendering helpers ────────────────────────────────────────────

static void render_tree(const disktree::DirNode* node,
                        int depth_limit,
                        int cur,
                        const std::string& prefix,
                        bool is_last)
{
    if (depth_limit > 0 && cur > depth_limit) return;

    std::string connector = is_last ? "└── " : "├── ";
    std::string child_prefix = prefix + (is_last ? "    " : "│   ");

    fmt::print("  {}{}{:<45} {:>12}  {:>6}\n",
        prefix, connector,
        std::string(node->name).substr(0, 45),
        disktree::utils::format_bytes(node->size),
        disktree::utils::format_pct(node->pct_of_parent));

    // Show child dirs
    size_t total = node->child_dirs.size() + node->child_files.size();
    size_t idx   = 0;

    for (const auto* d : node->child_dirs) {
        bool last = (++idx == total);
        render_tree(d, depth_limit, cur + 1, child_prefix, last);
    }

    // Show child files at leaf level
    if (depth_limit == 0 || cur == depth_limit) {
        for (const auto* f : node->child_files) {
            bool last = (++idx == total);
            std::string fc = last ? "└── " : "├── ";
            fmt::print("  {}{}{:<45} {:>12}  {:>6}\n",
                child_prefix, fc,
                std::string(f->name).substr(0, 45),
                disktree::utils::format_bytes(f->size),
                disktree::utils::format_pct(f->pct_of_parent));
        }
    }
}

static void render_summary(const disktree::ScanResult& result) {
    fmt::print("\n");
    fmt::print(fmt::emphasis::bold, "  {}\n", result.root->path);
    fmt::print("  {:18} {}\n", "Total size:",
        disktree::utils::format_bytes(result.total_size));
    fmt::print("  {:18} {}\n", "Files:",
        disktree::utils::format_count(result.total_files));
    fmt::print("  {:18} {}\n", "Directories:",
        disktree::utils::format_count(result.total_dirs));
    fmt::print("  {:18} {}\n\n", "Scan time:",
        disktree::utils::format_duration(result.elapsed_sec));
}

static void render_top_files(const disktree::Index& index, size_t n) {
    fmt::print(fmt::emphasis::bold, "  Top {} Files\n", n);
    fmt::print("  {:<60} {:>12}\n", "Path", "Size");
    fmt::print("  {}\n", std::string(74, '-'));
    size_t i = 0;
    for (const auto* f : index.top_files(n)) {
        std::string p = f->path;
        if (p.size() > 60) p = "..." + p.substr(p.size() - 57);
        fmt::print("  {:<60} {:>12}\n", p,
            disktree::utils::format_bytes(f->size));
        if (++i >= n) break;
    }
    fmt::print("\n");
}

static void render_top_dirs(const disktree::Index& index, size_t n) {
    fmt::print(fmt::emphasis::bold, "  Top {} Directories\n", n);
    fmt::print("  {:<60} {:>12}\n", "Path", "Size");
    fmt::print("  {}\n", std::string(74, '-'));
    size_t i = 0;
    for (const auto* d : index.top_dirs(n)) {
        std::string p = d->path;
        if (p.size() > 60) p = "..." + p.substr(p.size() - 57);
        fmt::print("  {:<60} {:>12}\n", p,
            disktree::utils::format_bytes(d->size));
        if (++i >= n) break;
    }
    fmt::print("\n");
}

static void render_extensions(const disktree::Index& index, size_t n) {
    fmt::print(fmt::emphasis::bold, "  Extensions\n");
    fmt::print("  {:<14} {:>12} {:>10}\n", "Ext", "Size", "Files");
    fmt::print("  {}\n", std::string(38, '-'));
    size_t i = 0;
    for (const auto& e : index.extensions()) {
        std::string ext = e.ext.empty() ? "(none)" : std::string(e.ext);

        // Bar: 20 chars wide
        int bar_len = int(20.0 * double(e.total_bytes) /
                          double(index.extensions()[0].total_bytes));
        std::string bar(bar_len, '#');
        bar += std::string(20 - bar_len, ' ');

        fmt::print("  {:<14} {:>12} {:>10}  [{}]\n",
            ext,
            disktree::utils::format_bytes(e.total_bytes),
            disktree::utils::format_count(e.file_count),
            bar);
        if (++i >= n) break;
    }
    fmt::print("\n");
}

// ── main ─────────────────────────────────────────────────────────

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(65001);
#endif

    CLI::App app{"disktree — fast disk usage analyzer", "disktree"};
    app.set_version_flag("--version", "0.1.0");

    // Arguments
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

    app.add_option("path", path, "Directory to scan")
       ->default_val(".");
    app.add_option("-d,--depth", depth,
       "Tree display depth (0=unlimited)")
       ->default_val(3);
    app.add_option("-n,--top", top_n,
       "Number of top files/dirs to show")
       ->default_val(20);
    app.add_option("-s,--sort", sort_by,
       "Sort by: size|name|count|modified")
       ->default_val("size")
       ->check(CLI::IsMember({"size","name","count","modified"}));
    app.add_option("--min-size", min_size_str,
       "Minimum file size to include (e.g. 1MB, 500KB)");
    app.add_option("--ext", ext_filter,
       "Filter: only show files with this extension (e.g. .mp4)");
    app.add_option("--export", export_fmt,
       "Export format: json|csv")
       ->check(CLI::IsMember({"json","csv"}));
    app.add_option("-o,--output", output_path,
       "Output file path for export");
    app.add_flag("--no-tree", no_tree,
       "Skip tree view, show only top files/dirs");
    app.add_flag("--snapshot", snapshot,
       "Save scan as a snapshot for later diffing");
    app.add_option("--diff", diff_path,
       "Diff against a saved snapshot file");

    CLI11_PARSE(app, argc, argv);

    try {
        // Parse min-size
        uint64_t min_size = 0;
        if (!min_size_str.empty()) {
            // Simple parser: number + unit
            double val = std::stod(min_size_str);
            std::string unit = min_size_str;
            unit.erase(0, unit.find_first_not_of("0123456789."));
            if      (unit == "KB" || unit == "kb") min_size = uint64_t(val * 1024);
            else if (unit == "MB" || unit == "mb") min_size = uint64_t(val * 1024 * 1024);
            else if (unit == "GB" || unit == "gb") min_size = uint64_t(val * 1024 * 1024 * 1024);
            else                                    min_size = uint64_t(val);
        }

        fmt::print(fmt::emphasis::bold, "\ndisktree v0.1.0\n");
        fmt::print("Scanning: {}\n\n", path);

        // Progress
        disktree::utils::ProgressTracker progress;
        progress.on_progress = [](disktree::ScanProgress p) {
            if (p.files_scanned == 0) return;
            fmt::print("  {} files  {}          \r",
                disktree::utils::format_count(p.files_scanned),
                disktree::utils::format_bytes(p.bytes_seen));
            std::cout.flush();
        };

        // Scan
        disktree::Scanner scanner(progress);
        disktree::Scanner::Config config;
        config.min_size = min_size;
        config.exclude_patterns = {
            "$RECYCLE.BIN", "System Volume Information",
            ".git", "node_modules"
        };

        auto result = scanner.scan(path, config);

        fmt::print("\n");

        // Build + Index
        disktree::Builder builder;
        builder.build(result);

        disktree::Index index;
        index.build(result);

        // Apply sort
        // (tree is already sorted by size from builder;
        //  other sorts reorder the index queries)

        // Summary
        render_summary(result);

        // Tree view
        if (!no_tree && !ext_filter.empty() == false) {
            fmt::print(fmt::emphasis::bold,
                "  Directory Tree (depth {})\n", depth);
            fmt::print("  {:<49} {:>12}  {:>6}\n",
                "Path", "Size", "%");
            fmt::print("  {}\n", std::string(65, '-'));
            fmt::print("  {:<49} {:>12}  {:>6}\n",
                std::string(result.root->name).substr(0, 49),
                disktree::utils::format_bytes(result.root->size),
                "100.0%");
            for (size_t i = 0; i < result.root->child_dirs.size(); ++i) {
                bool last = (i == result.root->child_dirs.size() - 1)
                            && result.root->child_files.empty();
                render_tree(result.root->child_dirs[i],
                            depth, 1, "", last);
            }
            fmt::print("\n");
        }

        // Extension filter mode
        if (!ext_filter.empty()) {
            auto files = index.files_by_ext(ext_filter);
            fmt::print(fmt::emphasis::bold,
                "  Files with extension '{}' ({})\n",
                ext_filter,
                disktree::utils::format_count(files.size()));
            fmt::print("  {:<60} {:>12}\n", "Path", "Size");
            fmt::print("  {}\n", std::string(74, '-'));
            size_t shown = 0;
            for (const auto* f : files) {
                std::string p = f->path;
                if (p.size() > 60) p = "..." + p.substr(p.size() - 57);
                fmt::print("  {:<60} {:>12}\n", p,
                    disktree::utils::format_bytes(f->size));
                if (++shown >= size_t(top_n)) break;
            }
            fmt::print("\n");
        } else {
            render_top_dirs(index, top_n);
            render_top_files(index, top_n);
            render_extensions(index, 15);
        }

        if (!result.skipped.empty()) {
            fmt::print("  ({} paths skipped)\n\n",
                disktree::utils::format_count(result.skipped.size()));
        }

        // Export
        if (!export_fmt.empty()) {
            std::string content;
            std::string default_ext;

            if (export_fmt == "json") {
                content     = disktree::export_::to_json(result, index);
                default_ext = ".json";
            } else if (export_fmt == "csv") {
                content     = disktree::export_::to_csv(result, index);
                default_ext = ".csv";
            }

            std::string out = output_path.empty()
                ? "disktree_export" + default_ext
                : output_path;

            std::ofstream f(out);
            if (!f) throw std::runtime_error("Cannot write to: " + out);
            f << content;
            fmt::print("  Exported to: {}\n\n", out);
        }

        // Snapshot
        if (snapshot) {
            auto snap = disktree::export_::save_snapshot(result, index);
            fmt::print("  Snapshot saved: {}\n\n", snap.string());
        }

        // Diff
        if (!diff_path.empty()) {
            auto snap_str = disktree::export_::load_snapshot(diff_path);
            fmt::print(fmt::emphasis::bold, "  Diff against: {}\n", diff_path);
            fmt::print("  (Full diff engine coming in next release)\n\n");
        }

    } catch (const std::exception& e) {
        fmt::print(fmt::fg(fmt::color::red),
            "\nError: {}\n", e.what());
        return 1;
    }

    return 0;
}