#pragma once

#include "disktree/core/models.hpp"

namespace disktree {

// ════════════════════════════════════════════════════════════════
// Builder
// Takes the raw tree produced by Scanner and:
//   1. Rolls up sizes, file counts, dir counts bottom-up
//   2. Sorts children by size descending
//   3. Computes pct_of_parent top-down
//   4. Builds extension maps per directory
//
// All passes are iterative (not recursive) to handle arbitrarily
// deep trees without stack overflow.
// ════════════════════════════════════════════════════════════════
class Builder {
public:
    // Run all build passes on the tree rooted at result.root
    // Modifies the tree in-place.
    void build(ScanResult& result);

private:
    void rollup(DirNode* root);
    void sort_children(DirNode* root);
    void compute_percentages(DirNode* root);
    void merge_ext(std::vector<std::pair<std::string_view,
                   uint64_t>>& target,
                   std::string_view ext,
                   uint64_t size);
};

} // namespace disktree