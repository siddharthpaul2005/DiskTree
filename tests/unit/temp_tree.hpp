#pragma once
// Test helper: builds a throw-away directory tree under the system temp dir.

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace disktree::test {

class TempTree {
public:
    TempTree() {
        static std::atomic<int> counter{0};
        auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = std::filesystem::temp_directory_path() /
                ("disktree_test_" + std::to_string(stamp) + "_" + std::to_string(counter++));
        std::filesystem::create_directories(root_);
    }
    ~TempTree() {
        std::error_code ec;
        std::filesystem::remove_all(root_, ec);
    }
    TempTree(const TempTree&) = delete;
    TempTree& operator=(const TempTree&) = delete;

    // Creates (or overwrites) a file of exactly `size` bytes; makes parent dirs.
    void file(const std::filesystem::path& rel, size_t size) const {
        auto p = root_ / rel;
        std::filesystem::create_directories(p.parent_path());
        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        std::string chunk(size, 'x');
        f.write(chunk.data(), std::streamsize(chunk.size()));
    }
    void dir(const std::filesystem::path& rel) const {
        std::filesystem::create_directories(root_ / rel);
    }
    void remove(const std::filesystem::path& rel) const {
        std::filesystem::remove_all(root_ / rel);
    }

    const std::filesystem::path& path() const { return root_; }

private:
    std::filesystem::path root_;
};

// The standard fixture used by several tests:
//   a.txt (100)  big.bin (5000)
//   sub1/c.txt (300)  sub1/d.MP4 (2000)  sub1/deep/e.txt (50)
//   sub2/f.log (700)
//   node_modules/x.js (9999)   <- skipped by the default excludes
//   empty/
inline void make_standard_tree(const TempTree& t) {
    t.file("a.txt", 100);
    t.file("big.bin", 5000);
    t.file("sub1/c.txt", 300);
    t.file("sub1/d.MP4", 2000);
    t.file("sub1/deep/e.txt", 50);
    t.file("sub2/f.log", 700);
    t.file("node_modules/x.js", 9999);
    t.dir("empty");
}

} // namespace disktree::test
