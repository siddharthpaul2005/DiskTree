#pragma once

#include "disktree/core/models.hpp"

#include <vector>
#include <string>
#include <string_view>
#include <unordered_map>
#include <memory>
#include <cstddef>

namespace disktree {

// ════════════════════════════════════════════════════════════════
// ScanArena
// Custom arena allocator for FileNode and DirNode objects.
//
// Why an arena?
//   A 500k file scan creates 500k FileNodes and ~50k DirNodes.
//   Allocating each with `new` = 550,000 malloc() calls = slow.
//   An arena pre-allocates large slabs and bumps a pointer.
//   Result: allocation is O(1) amortized, cache-friendly layout,
//   and freeing everything is one operation (destroy the arena).
//
// How it works:
//   We maintain a vector of fixed-size slabs (chunks of raw memory).
//   Objects are placement-new'd into the current slab.
//   When a slab fills up, we allocate a new one.
//   No individual deallocation — entire arena freed at once.
// ════════════════════════════════════════════════════════════════
class ScanArena {
public:
    explicit ScanArena(size_t slab_size = 4 * 1024 * 1024); // 4MB slabs
    ~ScanArena();

    // Allocate and default-construct a FileNode
    // Returns a pointer into arena memory — do NOT delete this pointer
    FileNode* alloc_file();

    // Allocate and default-construct a DirNode
    DirNode*  alloc_dir();

    // String interning: stores a string once, returns a stable string_view.
    // Used for extensions (.cpp, .py, etc.) — maybe 200 unique values
    // across 500k files. Without interning we'd store ".cpp" 40,000 times.
    std::string_view intern(std::string_view s);

    // Stats for debugging
    size_t total_allocated_bytes() const;
    size_t file_node_count()       const { return _file_count; }
    size_t dir_node_count()        const { return _dir_count;  }

    // Non-copyable: owns raw memory
    ScanArena(const ScanArena&)            = delete;
    ScanArena& operator=(const ScanArena&) = delete;

private:
    struct Slab {
        std::unique_ptr<std::byte[]> data;
        size_t                       used = 0;
        size_t                       capacity;

        explicit Slab(size_t cap)
            : data(std::make_unique<std::byte[]>(cap))
            , capacity(cap) {}
    };

    void* alloc_raw(size_t size, size_t alignment);
    void  new_slab();

    size_t                    _slab_size;
    std::vector<Slab>         _slabs;
    std::unordered_map<std::string, std::string_view> _intern_table;

    size_t _file_count = 0;
    size_t _dir_count  = 0;
};

} // namespace disktree