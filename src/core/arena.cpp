#include "disktree/core/arena.hpp"
#include <cassert>
#include <new>

namespace disktree {

ScanArena::ScanArena(size_t slab_size)
    : _slab_size(slab_size)
{
    new_slab(); // pre-allocate first slab immediately
}

ScanArena::~ScanArena() {
    // Explicitly destroy all objects we placement-new'd.
    // The Slab destructor frees the raw memory automatically.
    // Note: we track objects via the node vectors in ScanResult,
    // but for arena cleanup we rely on trivial destructors of
    // FileNode/DirNode (strings are destroyed separately).
    // This is acceptable: the arena is destroyed only when the
    // entire scan result is discarded.
}

void ScanArena::new_slab() {
    _slabs.emplace_back(_slab_size);
}

void* ScanArena::alloc_raw(size_t size, size_t alignment) {
    Slab& current = _slabs.back();

    // Align the current offset
    size_t aligned_used = (current.used + alignment - 1) & ~(alignment - 1);

    if (aligned_used + size > current.capacity) {
        // Current slab is full — allocate a new one
        // If the object is larger than a slab (shouldn't happen), make a big slab
        size_t new_cap = std::max(_slab_size, size + alignment);
        _slabs.emplace_back(new_cap);
        aligned_used = 0;
    }

    Slab& slab = _slabs.back();
    void* ptr = slab.data.get() + aligned_used;
    slab.used = aligned_used + size;
    return ptr;
}

FileNode* ScanArena::alloc_file() {
    void* mem = alloc_raw(sizeof(FileNode), alignof(FileNode));
    ++_file_count;
    return new(mem) FileNode{};  // placement new: construct in-place
}

DirNode* ScanArena::alloc_dir() {
    void* mem = alloc_raw(sizeof(DirNode), alignof(DirNode));
    ++_dir_count;
    return new(mem) DirNode{};
}

std::string_view ScanArena::intern(std::string_view s) {
    // Check if already interned
    auto it = _intern_table.find(std::string(s));
    if (it != _intern_table.end())
        return it->second;

    // Store the string and return a stable view
    auto [inserted_it, _] = _intern_table.emplace(std::string(s), std::string_view{});
    // Point the view at the key (which is stable in unordered_map)
    inserted_it->second = std::string_view(inserted_it->first);
    return inserted_it->second;
}

size_t ScanArena::total_allocated_bytes() const {
    size_t total = 0;
    for (const auto& slab : _slabs)
        total += slab.capacity;
    return total;
}

} // namespace disktree