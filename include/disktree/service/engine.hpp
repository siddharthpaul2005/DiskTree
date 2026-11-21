#pragma once

#include "disktree/core/builder.hpp"
#include "disktree/core/index.hpp"
#include "disktree/core/scanner.hpp"
#include "disktree/core/snapshot.hpp"
#include "disktree/core/snapshot_store.hpp"
#include "disktree/utils/progress.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace disktree::service {

// Thrown by queries when there is no finished scan to answer from
struct NoScanError : std::runtime_error {
    NoScanError() : std::runtime_error("No scan results yet. Start a scan first.") {}
};
// Thrown for bad input (unknown path, bad id, ...) -> HTTP 400/404
struct NotFoundError : std::runtime_error { using std::runtime_error::runtime_error; };
struct BadRequestError : std::runtime_error { using std::runtime_error::runtime_error; };

// ════════════════════════════════════════════════════════════════
// Engine
// Everything the desktop app needs, independent of HTTP: runs scans on
// a background thread, keeps the latest finished result, and answers
// queries about it as JSON. The API server (api_server.hpp) is a thin
// layer that maps routes onto these methods, and tests call them
// directly.
//
// Concurrency: one scan at a time. Queries read the current result
// under a shared lock; a finished scan swaps in its result under an
// exclusive lock, so readers never see a half-built tree.
// ════════════════════════════════════════════════════════════════
class Engine {
public:
    struct Options {
        std::filesystem::path db_path;      // snapshot database
    };

    struct ScanRequest {
        std::string              path;
        std::vector<std::string> excludes;            // in addition to defaults
        bool                     default_excludes = true;
        size_t                   threads = 0;          // 0 = all cores
        bool                     whole_pc = false;     // every fixed drive, under "This PC"
    };

    // Name of the synthetic root of a whole-PC scan
    static constexpr const char* kWholePc = "This PC";

    explicit Engine(Options opts);
    ~Engine();   // cancels a running scan and waits for it

    Engine(const Engine&)            = delete;
    Engine& operator=(const Engine&) = delete;

    nlohmann::json health() const;
    nlohmann::json drives() const;
    nlohmann::json places() const;   // Desktop, Downloads, Documents, ... that exist

    // Starts a scan. Throws BadRequestError if the path is invalid or a
    // scan is already running.
    void start_scan(const ScanRequest& req);
    void cancel_scan();
    nlohmann::json status() const;

    // For tests and shutdown: true if the engine became idle in time
    bool wait_idle(std::chrono::milliseconds timeout) const;

    // ── Queries on the latest finished scan (throw NoScanError) ──
    // A directory and its children down to `depth` levels. Per directory,
    // at most `max_children` entries are listed, and entries smaller than
    // `min_fraction` of the requested directory are folded into one
    // "other" entry, so the response stays small for any drive.
    nlohmann::json node(const std::string& path, int depth,
                        size_t max_children, double min_fraction) const;
    nlohmann::json top_files(size_t n) const;
    nlohmann::json top_dirs(size_t n) const;
    nlohmann::json extensions(size_t n) const;
    nlohmann::json search(const std::string& query, size_t limit) const;

    // ── Snapshots ────────────────────────────────────────────────
    nlohmann::json list_snapshots() const;
    nlohmann::json save_snapshot(const std::string& label);   // of the current scan
    void           delete_snapshot(int64_t id);
    nlohmann::json diff(int64_t snapshot_id, size_t limit) const;

private:
    struct Current {
        // Per-drive results of a whole-PC scan; they own the memory that
        // `result` points into, so they are declared first (destroyed last).
        std::vector<ScanResult>                      parts;
        ScanResult                                   result;
        Index                                        index;
        std::unordered_map<std::string, const DirNode*> by_path;  // normalised path
        // Space on the drive(s) this scan covers in full (a drive root or
        // the whole PC); null for a folder scan. Read when the scan ends.
        nlohmann::json                               volume;
    };

    void run_scan(ScanRequest req);
    std::shared_ptr<const Current> current_or_throw() const;

    Options                                  _opts;
    std::unique_ptr<SnapshotStore>           _store;
    bool                                     _elevated  = false;   // running as administrator
    bool                                     _full_read = false;   // backup privilege on

    // scan state (guarded by _state_mtx)
    mutable std::mutex                       _state_mtx;
    mutable std::condition_variable          _state_cv;
    std::string                              _state = "idle";  // idle|scanning|building|done|cancelled|error
    std::string                              _error;
    std::string                              _scan_path;
    std::chrono::steady_clock::time_point    _started;
    double                                   _last_elapsed = 0.0;
    std::shared_ptr<utils::ProgressTracker>  _progress;
    std::atomic<bool>                        _cancel{false};
    std::thread                              _worker;

    // latest finished result
    mutable std::shared_mutex                _result_mtx;
    std::shared_ptr<const Current>           _current;
};

} // namespace disktree::service
