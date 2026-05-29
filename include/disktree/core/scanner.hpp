#pragma once

#include "disktree/core/models.hpp"
#include "disktree/core/arena.hpp"
#include "disktree/utils/progress.hpp"
#include "disktree/utils/thread_pool.hpp"

#include <filesystem>
#include <vector>
#include <string>
#include <unordered_set>
#include <mutex>
#include <atomic>
#include <cstdint>
#include <functional>
#include <future>

namespace disktree {

class Scanner {
public:
    struct Config {
        bool     follow_symlinks    = false;
        uint32_t max_depth          = 0;
        uint64_t min_size           = 0;
        size_t   n_threads          = 8;
        std::vector<std::string> exclude_patterns;
    };

    explicit Scanner(utils::ProgressTracker& progress);

    ScanResult scan(const std::filesystem::path& root,
                    Config config );

private:
    struct ScanContext {
        ScanArena&                        arena;
        const Config&                     config;
        utils::ProgressTracker&           progress;
        utils::ThreadPool&                pool;       // single shared pool

        std::unordered_set<uint64_t>      seen_inodes;
        std::mutex                        seen_mtx;

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

    bool is_excluded(const std::filesystem::path& path,
                     const Config& config);

    static uint64_t inode_key(uint64_t inode, uint32_t device);

    utils::ProgressTracker& _progress;

    static constexpr uint32_t PARALLEL_DEPTH_LIMIT = 3;
};

} // namespace disktree