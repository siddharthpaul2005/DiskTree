#include "disktree/utils/humanize.hpp"

#include <fmt/format.h>
#include <cmath>
#include <cassert>
#include <cctype>
#include <charconv>
#include <string>

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

std::string format_signed_bytes(int64_t bytes) {
    if (bytes == 0) return "0 B";
    uint64_t mag = bytes < 0 ? uint64_t(0) - uint64_t(bytes) : uint64_t(bytes);
    return (bytes < 0 ? "-" : "+") + format_bytes(mag);
}

std::optional<uint64_t> parse_size(std::string_view text) {
    auto is_space = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
    while (!text.empty() && is_space(text.front())) text.remove_prefix(1);
    while (!text.empty() && is_space(text.back()))  text.remove_suffix(1);
    if (text.empty()) return std::nullopt;

    // Split number / unit
    size_t i = 0;
    while (i < text.size() &&
           (std::isdigit(static_cast<unsigned char>(text[i])) || text[i] == '.'))
        ++i;
    if (i == 0) return std::nullopt;

    double value = 0.0;
    std::string num(text.substr(0, i));
    try {
        size_t used = 0;
        value = std::stod(num, &used);
        if (used != num.size()) return std::nullopt;
    } catch (...) {
        return std::nullopt;
    }

    std::string unit;
    for (char c : text.substr(i))
        if (!is_space(c)) unit += char(std::toupper(static_cast<unsigned char>(c)));

    // Normalise: "KIB" / "KB" / "K" → "K", "B" / "" → ""
    if (unit.size() >= 3 && unit.ends_with("IB")) unit.resize(unit.size() - 2);
    else if (unit.size() >= 2 && unit.back() == 'B') unit.pop_back();
    else if (unit == "B") unit.clear();

    double mult = 1.0;
    if      (unit.empty()) mult = 1.0;
    else if (unit == "K")  mult = 1024.0;
    else if (unit == "M")  mult = 1024.0 * 1024.0;
    else if (unit == "G")  mult = 1024.0 * 1024.0 * 1024.0;
    else if (unit == "T")  mult = 1024.0 * 1024.0 * 1024.0 * 1024.0;
    else return std::nullopt;

    double bytes = value * mult;
    if (bytes < 0.0 || bytes > 1.8e19) return std::nullopt;
    return static_cast<uint64_t>(bytes + 0.5);
}

} // namespace disktree::utils