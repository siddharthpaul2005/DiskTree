#include "disktree/core/arena.hpp"
#include <algorithm>
#include <new>

namespace disktree {

ScanArena::ScanArena(size_t slab_size)
    : _slab_size(slab_size)
{
    new_slab(); // pre-allocate first slab immediately
}

ScanArena::~ScanArena() {
    // Nodes were placement-new'd into raw slab memory, so their
    // destructors never run on their own. Run them explicitly to free
    // the heap memory owned by each node's path string and child
    // vectors. The slabs themselves are freed by their unique_ptrs.
    for (FileNode* f : _files) f->~FileNode();
    for (DirNode*  d : _dirs)  d->~DirNode();
}

void ScanArena::new_slab() {
    _slabs.emplace_back(_slab_size);
}

void* ScanArena::alloc_raw(size_t size, size_t alignment) {
    Slab& current = _slabs.back();

    // Align the current offset
    size_t aligned_used = (current.used + alignment - 1) & ~(alignment - 1);

    if (aligned_used + size > current.capacity) {
        // Current slab is full — allocate a new one.
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
    std::lock_guard lock(_mtx);
    void* mem = alloc_raw(sizeof(FileNode), alignof(FileNode));
    ++_file_count;
    auto* f = new(mem) FileNode{};  // placement new: construct in-place
    _files.push_back(f);
    return f;
}

DirNode* ScanArena::alloc_dir() {
    std::lock_guard lock(_mtx);
    void* mem = alloc_raw(sizeof(DirNode), alignof(DirNode));
    ++_dir_count;
    auto* d = new(mem) DirNode{};
    _dirs.push_back(d);
    return d;
}

std::string_view ScanArena::intern(std::string_view s) {
    std::lock_guard lock(_mtx);

    auto it = _intern_table.find(std::string(s));
    if (it != _intern_table.end())
        return it->second;

    // Store the string and return a view of the key, which has a
    // stable address for the lifetime of the unordered_map node.
    auto [inserted_it, _] = _intern_table.emplace(std::string(s), std::string_view{});
    inserted_it->second = std::string_view(inserted_it->first);
    return inserted_it->second;
}

size_t ScanArena::total_allocated_bytes() const {
    std::lock_guard lock(_mtx);
    size_t total = 0;
    for (const auto& slab : _slabs)
        total += slab.capacity;
    return total;
}

} // namespace disktree
