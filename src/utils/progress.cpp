#include "disktree/utils/progress.hpp"
#include "disktree/utils/humanize.hpp"

#include <chrono>

namespace disktree::utils {

ProgressReporter::ProgressReporter(const ProgressTracker& tracker,
                                   std::FILE* out,
                                   int interval_ms)
    : _tracker(tracker), _out(out), _interval_ms(interval_ms)
{
    _thread = std::thread([this] {
        std::unique_lock<std::mutex> lock(_mtx);
        while (!_stopping) {
            draw();
            _cv.wait_for(lock, std::chrono::milliseconds(_interval_ms),
                         [this] { return _stopping; });
        }
    });
}

ProgressReporter::~ProgressReporter() {
    stop();
}

void ProgressReporter::stop() {
    {
        std::lock_guard<std::mutex> lock(_mtx);
        if (_stopping) return;
        _stopping = true;
    }
    _cv.notify_all();
    if (_thread.joinable()) _thread.join();

    // Erase the progress line
    std::fprintf(_out, "\r%s\r", std::string(LINE_WIDTH, ' ').c_str());
    std::fflush(_out);
}

std::string ProgressReporter::format_line(const ScanProgress& p,
                                          const std::string& path) {
    std::string line = "  " + format_count(p.files_scanned) + " files  " +
                       format_bytes(p.bytes_seen) + "  ";

    // Show the tail of the current path in whatever space is left
    size_t room = line.size() < LINE_WIDTH ? LINE_WIDTH - line.size() : 0;
    if (room > 3) {
        if (path.size() <= room) {
            line += path;
        } else {
            // Cut on a UTF-8 boundary so we never print half a character
            size_t start = path.size() - (room - 3);
            while (start < path.size() &&
                   (static_cast<unsigned char>(path[start]) & 0xC0) == 0x80)
                ++start;
            line += "..." + path.substr(start);
        }
    }
    if (line.size() < LINE_WIDTH) line.append(LINE_WIDTH - line.size(), ' ');
    return line;
}

void ProgressReporter::draw() {
    auto line = format_line(_tracker.snapshot(), _tracker.current_path());
    std::fprintf(_out, "\r%s", line.c_str());
    std::fflush(_out);
}

} // namespace disktree::utils
