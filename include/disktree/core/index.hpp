#pragma once

#include "disktree/core/models.hpp"
#include <vector>
#include <span>
#include <string_view>
#include <unordered_map>

namespace disktree {

// ════════════════════════════════════════════════════════════════
// Index
// Built from a completed ScanResult in a single BFS pass.
// Provides fast queries over the entire tree without re-traversal.
//
// All returned spans and pointers point INTO the original tree.
// The Index is invalid if the ScanResult is destroyed.
// ════════════════════════════════════════════════════════════════
class Index {
public:
    struct ExtStat {
        std::string_view  ext;
        uint64_t          total_bytes = 0;
        uint64_t          file_count  = 0;
    };

    // Build the index from a completed (built) ScanResult
    void build(const ScanResult& result);

    // ── Queries ──────────────────────────────────────────────────

    // Top N files by size (pre-sorted, O(1) subspan)
    std::span<const FileNode* const> top_files(size_t n) const;

    // Top N directories by size
    std::span<const DirNode* const>  top_dirs(size_t n) const;

    // All extensions sorted by total bytes descending
    std::span<const ExtStat>         extensions() const;

    // Files matching a given extension (e.g. ".mp4")
    std::vector<const FileNode*>     files_by_ext(std::string_view ext) const;

    // Simple substring search on filename
    std::vector<const FileNode*>     search_files(std::string_view query) const;
    std::vector<const DirNode*>      search_dirs(std::string_view query) const;

    size_t total_files() const { return _files_by_size.size(); }
    size_t total_dirs()  const { return _dirs_by_size.size();  }

private:
    std::vector<const FileNode*>  _files_by_size;
    std::vector<const DirNode*>   _dirs_by_size;
    std::vector<ExtStat>          _ext_stats;

    // Extension → indices into _files_by_size for fast ext lookup
    std::unordered_map<std::string_view,
                       std::vector<const FileNode*>> _by_ext;
};

} // namespace disktree