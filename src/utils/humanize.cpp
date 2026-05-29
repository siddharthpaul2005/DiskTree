#include "disktree/utils/humanize.hpp"

#include <fmt/format.h>
#include <cmath>
#include <cassert>

namespace disktree::utils {

std::string format_bytes(uint64_t bytes) {
    // Binary unit thresholds
    constexpr uint64_t KB = 1024ULL;
    constexpr uint64_t MB = 1024ULL * KB;
    constexpr uint64_t GB = 1024ULL * MB;
    constexpr uint64_t TB = 1024ULL * GB;

    if (bytes < KB)
        return fmt::format("{} B", bytes);
    if (bytes < MB)
        return fmt::format("{:.2f} KB", double(bytes) / double(KB));
    if (bytes < GB)
        return fmt::format("{:.2f} MB", double(bytes) / double(MB));
    if (bytes < TB)
        return fmt::format("{:.2f} GB", double(bytes) / double(GB));
    return     fmt::format("{:.2f} TB", double(bytes) / double(TB));
}

std::string format_count(uint64_t count) {
    // Build string right-to-left, inserting commas every 3 digits
    std::string s = std::to_string(count);
    int insert_pos = int(s.size()) - 3;
    while (insert_pos > 0) {
        s.insert(insert_pos, ",");
        insert_pos -= 3;
    }
    return s;
}

std::string format_duration(double seconds) {
    if (seconds < 60.0)
        return fmt::format("{:.2f}s", seconds);
    int mins = int(seconds) / 60;
    int secs = int(seconds) % 60;
    return fmt::format("{}m {}s", mins, secs);
}

std::string format_pct(float fraction) {
    // Clamp to [0, 1] — floating point arithmetic can produce tiny negatives
    float clamped = std::fmax(0.0f, std::fmin(1.0f, fraction));
    return fmt::format("{:.1f}%", clamped * 100.0f);
}

} // namespace disktree::utils