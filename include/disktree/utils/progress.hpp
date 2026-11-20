#pragma once

#include "disktree/core/models.hpp"
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>

namespace disktree::utils {

// ════════════════════════════════════════════════════════════════
// ProgressTracker
// Thread-safe progress counters written by scanner worker threads
// and read by whoever displays progress.
//
// Workers update atomic counters directly (lock-free). The current
// path is a std::string, so it is guarded by a mutex instead.
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

    // Called by the display thread
    ScanProgress snapshot() const {
        ScanProgress p;
        p.files_scanned = _files.load(std::memory_order_relaxed);
        p.dirs_scanned  = _dirs.load(std::memory_order_relaxed);
        p.bytes_seen    = _bytes.load(std::memory_order_relaxed);
        p.phase         = _phase.load(std::memory_order_relaxed);
        return p;
    }

    std::string current_path() const {
        std::lock_guard<std::mutex> lock(_path_mtx);
        return _current_path;
    }

private:
    std::atomic<uint64_t>  _files{0};
    std::atomic<uint64_t>  _dirs{0};
    std::atomic<uint64_t>  _bytes{0};
    std::atomic<ScanPhase> _phase{ScanPhase::Scanning};

    mutable std::mutex     _path_mtx;
    std::string            _current_path;
};

// ════════════════════════════════════════════════════════════════
// ProgressReporter
// RAII live progress line. Starts a background thread that redraws
//   "  123,456 files  12.3 GB  ...\current\path"
// on a single console line every `interval_ms`, so the scanner's
// worker threads never print anything themselves. stop() (or the
// destructor) erases the line.
// ════════════════════════════════════════════════════════════════
class ProgressReporter {
public:
    explicit ProgressReporter(const ProgressTracker& tracker,
                              std::FILE* out = stderr,
                              int interval_ms = 120);
    ~ProgressReporter();

    void stop();

    ProgressReporter(const ProgressReporter&)            = delete;
    ProgressReporter& operator=(const ProgressReporter&) = delete;

    // Width of the progress line in characters (exposed for tests).
    static constexpr size_t LINE_WIDTH = 100;

    // Builds the text shown for a snapshot (no \r, padded to LINE_WIDTH).
    static std::string format_line(const ScanProgress& p, const std::string& path);

private:
    void draw();

    const ProgressTracker&  _tracker;
    std::FILE*              _out;
    int                     _interval_ms;
    std::mutex              _mtx;
    std::condition_variable _cv;
    bool                    _stopping = false;
    std::thread             _thread;
};

} // namespace disktree::utils
