#pragma once

#include "disktree/core/snapshot.hpp"

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct sqlite3;

namespace disktree {

// Summary row for one stored snapshot (without the file list)
struct SnapshotInfo {
    int64_t     id          = 0;
    std::string scan_path;
    std::string label;
    int64_t     created_at  = 0;   // unix seconds
    uint64_t    total_size  = 0;
    uint64_t    total_files = 0;
    uint64_t    total_dirs  = 0;
};

// ════════════════════════════════════════════════════════════════
// SnapshotStore
// Snapshots kept in one SQLite database (default:
// %LOCALAPPDATA%\DiskTree\disktree.db) so the CLI and the desktop app
// share them. Each row holds the summary columns plus the snapshot's
// zstd-compressed JSON, so list() never touches the big file lists.
//
// Thread-safe: every call takes an internal mutex.
// ════════════════════════════════════════════════════════════════
class SnapshotStore {
public:
    // Opens (creating if needed) the database. Throws std::runtime_error.
    explicit SnapshotStore(const std::filesystem::path& db_path);
    ~SnapshotStore();

    SnapshotStore(const SnapshotStore&)            = delete;
    SnapshotStore& operator=(const SnapshotStore&) = delete;

    static std::filesystem::path default_path();

    // Returns the new snapshot id
    int64_t save(const Snapshot& snap, std::string_view label = "");

    // Newest first. With a scan_path, only snapshots of that path
    // (compared case-insensitively, ignoring a trailing separator).
    std::vector<SnapshotInfo> list(std::string_view scan_path = "") const;

    std::optional<SnapshotInfo> info(int64_t id) const;

    // Throws std::runtime_error if the id doesn't exist
    Snapshot load(int64_t id) const;

    std::optional<int64_t> latest_for(std::string_view scan_path) const;

    // Returns false if no such id
    bool remove(int64_t id);

    const std::filesystem::path& path() const { return _path; }

private:
    void exec(const char* sql);

    std::filesystem::path _path;
    sqlite3*              _db = nullptr;
    mutable std::mutex    _mtx;
};

} // namespace disktree
