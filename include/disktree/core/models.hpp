#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <chrono>
#include <filesystem>
#include <unordered_map>

namespace disktree {

// ════════════════════════════════════════════════════════════════
// Forward declarations
// ════════════════════════════════════════════════════════════════
struct FileNode;
struct DirNode;
struct ScanResult;
class  ScanArena;

// ════════════════════════════════════════════════════════════════
// FileNode
// Represents a single regular file on disk.
// Allocated from ScanArena — never construct directly with new.
// ════════════════════════════════════════════════════════════════
struct FileNode {
    // ── Identity ────────────────────────────────────────────────
    std::string       path;       // full absolute path, owned
    std::string_view  name;       // filename only, view into path
    std::string_view  extension;  // e.g. ".cpp", "", view into arena intern table

    // ── Stats ───────────────────────────────────────────────────
    uint64_t          size      = 0;  // bytes
    int64_t           modified  = 0;  // unix timestamp (mtime)
    uint64_t          inode     = 0;  // for hardlink detection
    uint32_t          device_id = 0;  // paired with inode for uniqueness

    // ── Flags ───────────────────────────────────────────────────
    bool              is_symlink  = false;
    bool              is_hardlink = false;  // same inode seen before

    // ── Computed during percentage pass ─────────────────────────
    float             pct_of_parent = 0.0f; // 0.0–1.0

    // Non-copyable: these live in an arena, copying makes no sense
    FileNode()                           = default;
    FileNode(const FileNode&)            = delete;
    FileNode& operator=(const FileNode&) = delete;
    FileNode(FileNode&&)                 = default;
};

// ════════════════════════════════════════════════════════════════
// DirNode
// Represents a directory and ALL of its descendants recursively.
// size, file_count, dir_count are fully rolled up (include all
// descendants, not just direct children).
// Allocated from ScanArena — never construct directly with new.
// ════════════════════════════════════════════════════════════════
struct DirNode {
    // ── Identity ────────────────────────────────────────────────
    std::string       path;
    std::string_view  name;       // view into path

    // ── Rolled-up stats (set by Builder, not Scanner) ───────────
    uint64_t          size        = 0;  // total bytes, ALL descendants
    uint64_t          file_count  = 0;  // total files, ALL descendants
    uint64_t          dir_count   = 0;  // total subdirs, ALL descendants
    int64_t           newest_mtime= 0;  // most recent mtime in subtree
    uint64_t          inode       = 0;
    uint32_t          device_id   = 0;

    // ── Tree structure ──────────────────────────────────────────
    uint32_t          depth       = 0;    // 0 = scan root
    DirNode*          parent      = nullptr;
    float             pct_of_parent = 0.0f;

    // Children sorted by size desc AFTER builder runs.
    // Dirs before files within each sorted order.
    std::vector<DirNode*>   child_dirs;
    std::vector<FileNode*>  child_files;

    // ── Extension breakdown ─────────────────────────────────────
    // Vector of (extension, total_bytes) sorted by bytes desc.
    // string_views point into the arena intern table.
    std::vector<std::pair<std::string_view, uint64_t>> ext_sizes;

    // ── Error state ─────────────────────────────────────────────
    bool              scan_failed = false;  // permission denied etc.

    DirNode()                          = default;
    DirNode(const DirNode&)            = delete;
    DirNode& operator=(const DirNode&) = delete;
    DirNode(DirNode&&)                 = default;
};

// ════════════════════════════════════════════════════════════════
// ScanError
// Represents a path that could not be scanned.
// ════════════════════════════════════════════════════════════════
enum class SkipReason {
    Permission,   // access denied
    NotFound,     // disappeared during scan
    IOError,      // other I/O error
    Cycle,        // symlink cycle detected
    MaxDepth,     // excluded by depth limit
    Pattern,      // excluded by user pattern
};

struct ScanError {
    std::filesystem::path path;
    SkipReason            reason;
    std::string           detail;   // OS error message
};

// ════════════════════════════════════════════════════════════════
// ScanProgress
// Emitted periodically during scanning for UI progress updates.
// Must be trivially copyable — sent through a lock-free ring buffer.
// ════════════════════════════════════════════════════════════════
enum class ScanPhase {
    Scanning,   // filesystem walk in progress
    Building,   // tree rollup in progress
    Indexing,   // index construction
    Done,
};

struct ScanProgress {
    uint64_t    files_scanned  = 0;
    uint64_t    dirs_scanned   = 0;
    uint64_t    bytes_seen     = 0;
    ScanPhase   phase          = ScanPhase::Scanning;
    // Note: no std::string here — must stay cheap to copy
    // Current path shown via separate atomic<string*> in scanner
};

// ════════════════════════════════════════════════════════════════
// ScanResult
// The complete output of a scan.
// Owns the ScanArena — destroying ScanResult frees ALL nodes.
// ════════════════════════════════════════════════════════════════
struct ScanResult {
    DirNode*                              root        = nullptr;
    std::filesystem::path                 scan_path;
    std::chrono::system_clock::time_point scanned_at;
    double                                elapsed_sec = 0.0;

    // Convenience totals (same as root->size etc. but explicit)
    uint64_t    total_size  = 0;
    uint64_t    total_files = 0;
    uint64_t    total_dirs  = 0;

    std::vector<ScanError>  skipped;
    std::string             platform;   // "windows", "linux", "macos"

    // Arena owns ALL FileNode and DirNode memory.
    // Must be last — destroyed after root pointer is gone.
    std::unique_ptr<ScanArena> arena;

    // Non-copyable: owns unique resources
    ScanResult()                               = default;
    ScanResult(const ScanResult&)              = delete;
    ScanResult& operator=(const ScanResult&)   = delete;
    ScanResult(ScanResult&&)                   = default;
    ScanResult& operator=(ScanResult&&)        = default;
};

// ════════════════════════════════════════════════════════════════
// TreemapRect
// Output of the squarified treemap layout algorithm.
// Used by both the TUI widget and the web API.
// ════════════════════════════════════════════════════════════════
struct TreemapRect {
    const void*   node       = nullptr;  // DirNode* or FileNode*, type-erased
    bool          is_dir     = false;
    float         x          = 0.0f;    // normalized 0.0–1.0
    float         y          = 0.0f;
    float         w          = 0.0f;
    float         h          = 0.0f;
    uint32_t      depth      = 0;
    uint32_t      color_rgb  = 0xAAAAAA; // precomputed
    std::string_view label;              // name, view into node's path
};

// ════════════════════════════════════════════════════════════════
// DiffResult
// Output of comparing two ScanResults / Snapshots.
// ════════════════════════════════════════════════════════════════
struct FileDelta {
    std::string   path;
    int64_t       size_before = 0;   // -1 if file was added
    int64_t       size_after  = 0;   // -1 if file was removed

    int64_t delta() const {
        if (size_before < 0) return size_after;
        if (size_after  < 0) return -size_before;
        return size_after - size_before;
    }
};

struct DiffResult {
    std::string             snapshot_a_id;
    std::string             snapshot_b_id;
    std::vector<FileDelta>  added;      // new files
    std::vector<FileDelta>  removed;    // deleted files
    std::vector<FileDelta>  grown;      // existing files that got bigger
    std::vector<FileDelta>  shrunk;     // existing files that got smaller
    int64_t                 net_delta   = 0;  // positive = disk grew
    double                  elapsed_sec = 0.0;
};

} // namespace disktree