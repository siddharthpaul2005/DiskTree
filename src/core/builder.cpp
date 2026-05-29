#include "disktree/core/builder.hpp"

#include <stack>
#include <queue>
#include <algorithm>
#include <cassert>

namespace disktree {

void Builder::merge_ext(
    std::vector<std::pair<std::string_view, uint64_t>>& target,
    std::string_view ext,
    uint64_t size)
{
    for (auto& [e, s] : target) {
        if (e == ext) { s += size; return; }
    }
    target.emplace_back(ext, size);
}

void Builder::rollup(DirNode* root) {
    // Post-order iterative traversal using a stack.
    // We visit children BEFORE parents so parent sizes
    // are accumulated correctly.
    //
    // Stack stores (node, processed) pairs.
    // First visit: push children, mark for reprocessing.
    // Second visit: accumulate from children into this node.

    std::stack<std::pair<DirNode*, bool>> stk;
    stk.push({root, false});

    while (!stk.empty()) {
        auto& [node, processed] = stk.top();

        if (!processed) {
            processed = true;
            // Push all child dirs to process first
            for (auto* child : node->child_dirs)
                stk.push({child, false});
        } else {
            stk.pop();

            // Reset this node's stats before accumulating
            node->size       = 0;
            node->file_count = 0;
            node->dir_count  = 0;
            node->ext_sizes.clear();

            // Accumulate direct files
            for (auto* f : node->child_files) {
                node->size       += f->size;
                node->file_count += 1;
                node->newest_mtime = std::max(
                    node->newest_mtime, f->modified);
                merge_ext(node->ext_sizes, f->extension, f->size);
            }

            // Accumulate child directories
            for (auto* d : node->child_dirs) {
                node->size       += d->size;
                node->file_count += d->file_count;
                node->dir_count  += d->dir_count + 1;
                node->newest_mtime = std::max(
                    node->newest_mtime, d->newest_mtime);

                // Merge child's ext map into ours
                for (auto& [ext, sz] : d->ext_sizes)
                    merge_ext(node->ext_sizes, ext, sz);
            }

            // Sort ext map by size descending
            std::sort(node->ext_sizes.begin(), node->ext_sizes.end(),
                [](const auto& a, const auto& b){
                    return a.second > b.second;
                });
        }
    }
}

void Builder::sort_children(DirNode* root) {
    // BFS: sort each node's children by size descending
    std::queue<DirNode*> q;
    q.push(root);

    while (!q.empty()) {
        DirNode* node = q.front();
        q.pop();

        std::sort(node->child_dirs.begin(), node->child_dirs.end(),
            [](const DirNode* a, const DirNode* b){
                return a->size > b->size;
            });

        std::sort(node->child_files.begin(), node->child_files.end(),
            [](const FileNode* a, const FileNode* b){
                return a->size > b->size;
            });

        for (auto* child : node->child_dirs)
            q.push(child);
    }
}

void Builder::compute_percentages(DirNode* root) {
    // Top-down BFS: root is 100%, each child is size/parent_size
    root->pct_of_parent = 1.0f;

    std::queue<DirNode*> q;
    q.push(root);

    while (!q.empty()) {
        DirNode* node = q.front();
        q.pop();

        float parent_size = float(node->size);

        for (auto* d : node->child_dirs) {
            d->pct_of_parent = parent_size > 0.0f
                ? float(d->size) / parent_size
                : 0.0f;
            q.push(d);
        }

        for (auto* f : node->child_files) {
            f->pct_of_parent = parent_size > 0.0f
                ? float(f->size) / parent_size
                : 0.0f;
        }
    }
}

void Builder::build(ScanResult& result) {
    if (!result.root) return;

    rollup(result.root);

    // Update ScanResult totals from the rolled-up root
    result.total_size  = result.root->size;
    result.total_files = result.root->file_count;
    result.total_dirs  = result.root->dir_count;

    sort_children(result.root);
    compute_percentages(result.root);
}

} // namespace disktree