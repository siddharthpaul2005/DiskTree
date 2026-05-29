#include "disktree/core/index.hpp"

#include <queue>
#include <algorithm>
#include <unordered_map>

namespace disktree {

void Index::build(const ScanResult& result) {
    if (!result.root) return;

    _files_by_size.clear();
    _dirs_by_size.clear();
    _ext_stats.clear();
    _by_ext.clear();

    // Single BFS pass over the entire tree
    std::queue<const DirNode*> q;
    q.push(result.root);

    std::unordered_map<std::string_view, ExtStat> ext_map;

    while (!q.empty()) {
        const DirNode* node = q.front();
        q.pop();

        // Don't add root to dirs list (it's the scan target itself)
        if (node != result.root)
            _dirs_by_size.push_back(node);

        // Collect all files in this directory
        for (const FileNode* f : node->child_files) {
            _files_by_size.push_back(f);

            // Build extension stats
            auto& stat = ext_map[f->extension];
            stat.ext         = f->extension;
            stat.total_bytes += f->size;
            stat.file_count  += 1;

            // Group by extension for fast lookup
            _by_ext[f->extension].push_back(f);
        }

        for (const DirNode* d : node->child_dirs)
            q.push(d);
    }

    // Sort files by size descending
    std::sort(_files_by_size.begin(), _files_by_size.end(),
        [](const FileNode* a, const FileNode* b){
            return a->size > b->size;
        });

    // Sort dirs by size descending
    std::sort(_dirs_by_size.begin(), _dirs_by_size.end(),
        [](const DirNode* a, const DirNode* b){
            return a->size > b->size;
        });

    // Build ext_stats vector sorted by total bytes
    _ext_stats.reserve(ext_map.size());
    for (auto& [ext, stat] : ext_map)
        _ext_stats.push_back(stat);

    std::sort(_ext_stats.begin(), _ext_stats.end(),
        [](const ExtStat& a, const ExtStat& b){
            return a.total_bytes > b.total_bytes;
        });
}

std::span<const FileNode* const> Index::top_files(size_t n) const {
    size_t count = std::min(n, _files_by_size.size());
    return std::span<const FileNode* const>(_files_by_size.data(), count);
}

std::span<const DirNode* const> Index::top_dirs(size_t n) const {
    size_t count = std::min(n, _dirs_by_size.size());
    return std::span<const DirNode* const>(_dirs_by_size.data(), count);
}

std::span<const Index::ExtStat> Index::extensions() const {
    return std::span<const Index::ExtStat>(_ext_stats.data(), _ext_stats.size());
}

std::vector<const FileNode*> Index::files_by_ext(std::string_view ext) const {
    auto it = _by_ext.find(ext);
    if (it == _by_ext.end()) return {};
    return it->second;
}

std::vector<const FileNode*> Index::search_files(std::string_view query) const {
    std::vector<const FileNode*> results;
    for (const FileNode* f : _files_by_size) {
        if (f->name.find(query) != std::string_view::npos)
            results.push_back(f);
    }
    return results;
}

std::vector<const DirNode*> Index::search_dirs(std::string_view query) const {
    std::vector<const DirNode*> results;
    for (const DirNode* d : _dirs_by_size) {
        if (d->name.find(query) != std::string_view::npos)
            results.push_back(d);
    }
    return results;
}

} // namespace disktree