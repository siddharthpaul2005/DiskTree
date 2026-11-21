#pragma once

#include "disktree/core/models.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace disktree {

// ════════════════════════════════════════════════════════════════
// Snapshot
// A flat record of every file in a scan (relative path → size,
// mtime) so two scans taken at different times can be diffed.
//
// On disk: zstd-compressed JSON, ~/.disktree/snapshots/<timestamp>.json.zst
//   {
//     "format": "disktree-snapshot", "version": 2,
//     "scan_path": "D:\\", "created_at": 1763600000,
//     "total_size": ..., "total_files": ..., "total_dirs": ...,
//     "files": [ ["Games\\foo.bin", 1234, 1763500000], ... ]
//   }
// Paths are stored relative to scan_path so a snapshot can be
// compared against a later scan of the same directory.
// ════════════════════════════════════════════════════════════════
struct SnapshotEntry {
    uint64_t size     = 0;
    int64_t  modified = 0;
};

struct Snapshot {
    static constexpr int FORMAT_VERSION = 2;

    std::string id;            // file name, or "current scan"
    std::string scan_path;
    int64_t     created_at  = 0;  // unix seconds
    uint64_t    total_size  = 0;
    uint64_t    total_files = 0;
    uint64_t    total_dirs  = 0;

    std::unordered_map<std::string, SnapshotEntry> files;
};

// Lower-case, '/' -> '\', no trailing separator (except "c:\\").
// Used to decide whether two scans are of the same path.
std::string normalise_scan_path(std::string_view path);

// Build a snapshot from a completed (Builder-processed) scan
Snapshot snapshot_from_scan(const ScanResult& result);

// JSON (de)serialisation. from_json throws std::runtime_error on
// malformed input or on legacy v1 snapshots (which lack a full file list).
std::string snapshot_to_json(const Snapshot& snap);
Snapshot    snapshot_from_json(std::string_view json_text, std::string id = "");

// zstd helpers. decompress throws std::runtime_error on corrupt data.
std::string zstd_compress(std::string_view data, int level = 3);
std::string zstd_decompress(std::string_view data);
bool        is_zstd(std::string_view data);

// ~/.disktree/snapshots (USERPROFILE on Windows, HOME elsewhere)
std::filesystem::path default_snapshot_dir();

// Writes <dir>/<YYYYmmdd_HHMMSS>.json.zst, creating dir if needed.
std::filesystem::path save_snapshot(const Snapshot& snap,
                                    const std::filesystem::path& dir);

// Reads a .json.zst or plain .json snapshot
Snapshot load_snapshot(const std::filesystem::path& path);

// Snapshot files in dir, newest first
std::vector<std::filesystem::path> list_snapshots(const std::filesystem::path& dir);

// Newest snapshot in dir whose scan_path equals scan_path
// (case-insensitive), or nullopt.
std::optional<std::filesystem::path>
latest_snapshot_for(const std::filesystem::path& dir, std::string_view scan_path);

// ════════════════════════════════════════════════════════════════
// Diff
// Compares two snapshots. Lists in the result are sorted by
// |delta| descending. `dirs` holds per-top-level-entry totals
// (the first path component under the scan root) that changed.
// ════════════════════════════════════════════════════════════════
DiffResult diff_snapshots(const Snapshot& before, const Snapshot& after);

} // namespace disktree
