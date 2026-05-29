#pragma once

#include "disktree/core/models.hpp"
#include <atomic>
#include <functional>
#include <string>
#include <mutex>

namespace disktree::utils {

// ════════════════════════════════════════════════════════════════
// ProgressTracker
// Thread-safe progress reporting from scanner worker threads
// to the UI thread.
//
// Scanner workers update atomic counters directly (lock-free).
// The UI thread calls snapshot() periodically to get a
// consistent view for display.
//
// Current path is stored separately with a mutex because
// std::string is not atomically updatable.
// ════════════════════════════════════════════════════════════════
class ProgressTracker {
public:
    // Called by scanner worker threads — lock-free
    void add_file(uint64_t size) {
        _files.fetch_add(1, std::memory_order_relaxed);
        _bytes.fetch_add(size, std::memory_order_relaxed);
    }

    void add_dir() {
        _dirs.fetch_add(1, std::memory_order_relaxed);
    }

    void set_current_path(const std::string& path) {
        std::lock_guard<std::mutex> lock(_path_mtx);
        _current_path = path;
    }

    void set_phase(ScanPhase phase) {
        _phase.store(phase, std::memory_order_relaxed);
    }

    // Called by UI thread — takes a consistent snapshot
    ScanProgress snapshot() const {
        ScanProgress p;
        p.files_scanned = _files.load(std::memory_order_relaxed);
        p.dirs_scanned  = _dirs.load(std::memory_order_relaxed);
        p.bytes_seen    = _bytes.load(std::memory_order_relaxed);
        p.phase         = _phase.load(std::memory_order_relaxed);
        return p;
    }

    // Optional callback: called every N files from worker threads
    // Must be thread-safe if set
    std::function<void(ScanProgress)> on_progress;

    void maybe_emit(uint64_t every_n = 5000) {
        uint64_t files = _files.load(std::memory_order_relaxed);
        if (on_progress && (files % every_n == 0)) {
            on_progress(snapshot());
        }
    }

private:
    std::atomic<uint64_t>  _files{0};
    std::atomic<uint64_t>  _dirs{0};
    std::atomic<uint64_t>  _bytes{0};
    std::atomic<ScanPhase> _phase{ScanPhase::Scanning};

    mutable std::mutex     _path_mtx;
    std::string            _current_path;
};

} // namespace disktree::utils