#include "disktree/export/csv_export.hpp"
#include "disktree/utils/humanize.hpp"

#include <sstream>
#include <string_view>

namespace disktree::export_ {

// RFC 4180: wrap in quotes, double any embedded quotes
static std::string q(std::string_view v) {
    std::string out = "\"";
    for (char c : v) {
        if (c == '"') out += '"';
        out += c;
    }
    out += '"';
    return out;
}

std::string to_csv(const ScanResult& /*result*/, const Index& index) {
    std::ostringstream ss;

    // Header
    ss << "type,path,size_bytes,size_human,extension,pct_of_parent\n";

    // All files sorted by size desc
    for (const auto* f : index.top_files(index.total_files())) {
        ss << "file," << q(f->path) << ","
           << f->size << ","
           << q(utils::format_bytes(f->size)) << ","
           << q(f->extension) << ","
           << f->pct_of_parent << "\n";
    }

    // All dirs sorted by size desc
    for (const auto* d : index.top_dirs(index.total_dirs())) {
        ss << "dir," << q(d->path) << ","
           << d->size << ","
           << q(utils::format_bytes(d->size)) << ","
           << ","
           << d->pct_of_parent << "\n";
    }

    return ss.str();
}

} // namespace disktree::export_
