#pragma once

#include "disktree/core/models.hpp"
#include "disktree/core/arena.hpp"
#include "disktree/utils/progress.hpp"
#include "disktree/utils/thread_pool.hpp"

#include <filesystem>
#include <vector>
#include <string>
#include <string_view>
#include <mutex>
#include <atomic>
#include <cstdint>

namespace disktree {

// ════════════════════════════════════════════════════════════════
// Scanner
// Walks a directory tree and builds the raw DirNode/FileNode tree.
// Sizes are NOT rolled up here — run Builder afterwards.
//
// Parallelism: directories down to PARALLEL_DEPTH_LIMIT levels below
// the root are each submitted to a thread pool as their own task;
// anything deeper is walked by that task with an explicit stack
// (iterative DFS — no recursion, so deep trees can't overflow).
// Each DirNode is only ever mutated by the task that owns it, so the
// tree itself needs no locking; the arena and error list are locked.
// ════════════════════════════════════════════════════════════════
class Scanner {
public:
    struct Config {
        bool     follow_symlinks    = false;
        uint32_t max_depth          = 0;    // 0 = unlimited
        uint64_t min_size           = 0;    // skip files smaller than this
        size_t   n_threads          = 0;    // 0 = std::thread::hardware_concurrency()
        // Names to skip (matched against the file/dir name, not the
        // full path). Case-insensitive; supports * and ? wildcards.
        std::vector<std::string> exclude_patterns;
    };

    // The names skipped when the user doesn't pass --no-default-excludes
    static std::vector<std::string> default_excludes();

    explicit Scanner(utils::ProgressTracker& progress);

    // Throws std::runtime_error if root doesn't exist or isn't a directory.
    ScanResult scan(const std::filesystem::path& root, Config config);

    // Case-insensitive glob match supporting '*' and '?'.
    static bool matches_pattern(std::string_view pattern, std::string_view name);

    static constexpr uint32_t PARALLEL_DEPTH_LIMIT = 3;

private:
    struct ScanContext {
        ScanArena&                        arena;
        const Config&                     config;
        utils::ProgressTracker&           progress;
        utils::ThreadPool&                pool;

        std::vector<ScanError>            errors;
        std::mutex                        errors_mtx;

        std::atomic<uint64_t>             total_files{0};
        std::atomic<uint64_t>             total_dirs{0};
        std::atomic<uint64_t>             total_bytes{0};
    };

    void scan_directory(const std::filesystem::path& path,
                        DirNode*                     node,
                        uint32_t                     depth,
                        ScanContext&                 ctx);

    static bool is_excluded(std::string_view name, const Config& config);

    utils::ProgressTracker& _progress;
};

} // namespace disktree
