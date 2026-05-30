#ifdef _WIN32
#include <windows.h>
#endif

#include "disktree/core/scanner.hpp"
#include "disktree/core/builder.hpp"
#include "disktree/core/index.hpp"
#include "disktree/utils/humanize.hpp"
#include "disktree/utils/progress.hpp"

#include <fmt/format.h>
#include <fmt/color.h>
#include <iostream>
#include <string>
#include <atomic>

int main(int argc, char** argv) {
#ifdef _WIN32
    // UTF-8 console output — handles non-ASCII filenames
    SetConsoleOutputCP(65001);
#endif

    try {
        std::string path = (argc > 1) ? argv[1] : ".";

        fmt::print(fmt::emphasis::bold, "\ndisktree v0.1.0\n");
        fmt::print("Scanning: {}\n\n", path);

        // Progress reporting
        disktree::utils::ProgressTracker progress;
        progress.on_progress = [](disktree::ScanProgress p) {
            fmt::print("  scanning... {} files  {}\n",
                disktree::utils::format_count(p.files_scanned),
                disktree::utils::format_bytes(p.bytes_seen));
        };

        // Scan
        disktree::Scanner scanner(progress);
        disktree::Scanner::Config config;
        config.exclude_patterns = {
        "$RECYCLE.BIN",
        "System Volume Information",
        ".git",
        "node_modules"
};
        auto result = scanner.scan(path, config);

        // Build
        disktree::Builder builder;
        builder.build(result);

        // Index
        disktree::Index index;
        index.build(result);

        fmt::print("\n");

        // Summary
        fmt::print(fmt::emphasis::bold, "  {}\n", result.root->path);
        fmt::print("  {:15} {}\n", "Total size:",
            disktree::utils::format_bytes(result.total_size));
        fmt::print("  {:15} {}\n", "Files:",
            disktree::utils::format_count(result.total_files));
        fmt::print("  {:15} {}\n", "Directories:",
            disktree::utils::format_count(result.total_dirs));
        fmt::print("  {:15} {}\n\n", "Scan time:",
            disktree::utils::format_duration(result.elapsed_sec));

        // Top directories
        fmt::print(fmt::emphasis::bold, "  Top Directories\n");
        fmt::print("  {:<55} {:>12}\n", "Path", "Size");
        fmt::print("  {}\n", std::string(69, '-'));
        for (const auto* d : index.top_dirs(10)) {
            std::string p = d->path;
            if (p.size() > 55) p = "..." + p.substr(p.size() - 52);
            fmt::print("  {:<55} {:>12}\n", p,
                disktree::utils::format_bytes(d->size));
        }

        fmt::print("\n");

        // Top files
        fmt::print(fmt::emphasis::bold, "  Top Files\n");
        fmt::print("  {:<55} {:>12}\n", "Path", "Size");
        fmt::print("  {}\n", std::string(69, '-'));
        for (const auto* f : index.top_files(10)) {
            std::string p = f->path;
            if (p.size() > 55) p = "..." + p.substr(p.size() - 52);
            fmt::print("  {:<55} {:>12}\n", p,
                disktree::utils::format_bytes(f->size));
        }

        fmt::print("\n");

        // Extension breakdown
        fmt::print(fmt::emphasis::bold, "  Extensions\n");
        fmt::print("  {:<12} {:>12} {:>10}\n", "Ext", "Size", "Files");
        fmt::print("  {}\n", std::string(36, '-'));
        size_t ext_count = 0;
        for (const auto& e : index.extensions()) {
            std::string ext = e.ext.empty()
                ? "(none)" : std::string(e.ext);
            fmt::print("  {:<12} {:>12} {:>10}\n",
                ext,
                disktree::utils::format_bytes(e.total_bytes),
                disktree::utils::format_count(e.file_count));
            if (++ext_count >= 15) break;
        }

        fmt::print("\n");

        if (!result.skipped.empty()) {
            fmt::print("  ({} paths skipped)\n\n",
                disktree::utils::format_count(result.skipped.size()));
        }

    } catch (const std::exception& e) {
        fmt::print("\nFatal error: {}\n", e.what());
        return 1;
    } catch (...) {
        fmt::print("\nFatal unknown error\n");
        return 1;
    }

    return 0;
}