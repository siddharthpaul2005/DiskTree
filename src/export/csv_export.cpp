#include "disktree/export/csv_export.hpp"
#include "disktree/utils/humanize.hpp"

#include <sstream>
#include <queue>

namespace disktree::export_ {

std::string to_csv(const ScanResult& result, const Index& index) {
    std::ostringstream ss;

    // Header
    ss << "type,path,size_bytes,size_human,extension,pct_of_parent\n";

    // All files sorted by size desc
    for (const auto* f : index.top_files(index.total_files())) {
        ss << "file,\"" << f->path << "\","
           << f->size << ","
           << "\"" << utils::format_bytes(f->size) << "\","
           << "\"" << std::string(f->extension) << "\","
           << f->pct_of_parent << "\n";
    }

    // All dirs sorted by size desc
    for (const auto* d : index.top_dirs(index.total_dirs())) {
        ss << "dir,\"" << d->path << "\","
           << d->size << ","
           << "\"" << utils::format_bytes(d->size) << "\","
           << ","
           << d->pct_of_parent << "\n";
    }

    return ss.str();
}

} // namespace disktree::export_