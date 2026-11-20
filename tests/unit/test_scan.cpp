#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "disktree/core/scanner.hpp"
#include "disktree/core/builder.hpp"
#include "disktree/core/index.hpp"
#include "temp_tree.hpp"

#include <algorithm>
#include <string>
#include <vector>

using namespace disktree;
using disktree::test::TempTree;

namespace {

struct Scanned {
    utils::ProgressTracker progress;
    ScanResult             result;
    Index                  index;
};

std::unique_ptr<Scanned> scan_and_build(const std::filesystem::path& root,
                                        Scanner::Config cfg) {
    auto s = std::make_unique<Scanned>();
    Scanner scanner(s->progress);
    s->result = scanner.scan(root, cfg);
    Builder{}.build(s->result);
    s->index.build(s->result);
    return s;
}

Scanner::Config default_cfg(size_t threads = 4) {
    Scanner::Config c;
    c.exclude_patterns = Scanner::default_excludes();
    c.n_threads = threads;
    return c;
}

const DirNode* child_dir(const DirNode* d, std::string_view name) {
    for (auto* c : d->child_dirs) if (c->name == name) return c;
    return nullptr;
}

} // namespace

TEST_CASE("scan: totals with default excludes", "[scan]") {
    TempTree t;
    disktree::test::make_standard_tree(t);
    auto s = scan_and_build(t.path(), default_cfg());

    REQUIRE(s->result.total_files == 6);
    REQUIRE(s->result.total_size  == 100 + 5000 + 300 + 2000 + 50 + 700);
    REQUIRE(s->result.total_dirs  == 4);   // sub1, sub1/deep, sub2, empty
    REQUIRE(s->result.skipped.empty());
    REQUIRE(child_dir(s->result.root, "node_modules") == nullptr);
}

TEST_CASE("scan: rollup, ordering and percentages", "[scan][builder]") {
    TempTree t;
    disktree::test::make_standard_tree(t);
    auto s = scan_and_build(t.path(), default_cfg());
    const DirNode* root = s->result.root;

    // child dirs sorted by size desc: sub1 (2350) > sub2 (700) > empty (0)
    REQUIRE(root->child_dirs.size() == 3);
    REQUIRE(root->child_dirs[0]->name == "sub1");
    REQUIRE(root->child_dirs[1]->name == "sub2");
    REQUIRE(root->child_dirs[2]->name == "empty");

    const DirNode* sub1 = root->child_dirs[0];
    REQUIRE(sub1->size == 2350);
    REQUIRE(sub1->file_count == 3);
    REQUIRE(sub1->dir_count == 1);
    REQUIRE_THAT(sub1->pct_of_parent, Catch::Matchers::WithinAbs(2350.0 / 8150.0, 1e-5));

    REQUIRE(root->child_files[0]->name == "big.bin");
    REQUIRE(root->pct_of_parent == 1.0f);
}

TEST_CASE("scan: index queries", "[scan][index]") {
    TempTree t;
    disktree::test::make_standard_tree(t);
    auto s = scan_and_build(t.path(), default_cfg());

    auto top = s->index.top_files(2);
    REQUIRE(top.size() == 2);
    REQUIRE(top[0]->name == "big.bin");
    REQUIRE(top[1]->name == "d.MP4");

    // Extensions are lower-cased
    REQUIRE(s->index.files_by_ext(".mp4").size() == 1);
    REQUIRE(s->index.files_by_ext(".MP4").empty());
    REQUIRE(s->index.files_by_ext(".txt").size() == 3);

    auto exts = s->index.extensions();
    REQUIRE(exts[0].ext == ".bin");
    REQUIRE(exts[0].total_bytes == 5000);

    REQUIRE(s->index.top_dirs(1)[0]->name == "sub1");
    REQUIRE(s->index.search_files("f.l").size() == 1);
    REQUIRE(s->index.total_dirs() == 4);
}

TEST_CASE("scan: exclusion options", "[scan]") {
    TempTree t;
    disktree::test::make_standard_tree(t);

    SECTION("no default excludes includes node_modules") {
        Scanner::Config c;
        c.n_threads = 2;
        auto s = scan_and_build(t.path(), c);
        REQUIRE(s->result.total_files == 7);
        REQUIRE(child_dir(s->result.root, "node_modules") != nullptr);
    }
    SECTION("wildcard pattern") {
        auto c = default_cfg();
        c.exclude_patterns.push_back("*.LOG");
        auto s = scan_and_build(t.path(), c);
        REQUIRE(s->result.total_files == 5);
        REQUIRE(s->index.files_by_ext(".log").empty());
    }
    SECTION("excluding a directory name skips its subtree") {
        auto c = default_cfg();
        c.exclude_patterns.push_back("sub1");
        auto s = scan_and_build(t.path(), c);
        REQUIRE(s->result.total_files == 3);
    }
    SECTION("min_size") {
        auto c = default_cfg();
        c.min_size = 400;
        auto s = scan_and_build(t.path(), c);
        REQUIRE(s->result.total_files == 3);   // big.bin, d.MP4, f.log
    }
    SECTION("max_depth") {
        auto c = default_cfg();
        c.max_depth = 1;
        auto s = scan_and_build(t.path(), c);
        const DirNode* sub1 = child_dir(s->result.root, "sub1");
        REQUIRE(sub1 != nullptr);
        REQUIRE(sub1->child_dirs.empty());
        REQUIRE(s->result.total_files == 5);  // deep/e.txt not reached
    }
}

TEST_CASE("scan: parallel and single-threaded scans agree", "[scan][threads]") {
    TempTree t;
    // Wide and deep enough that several pool tasks run at once
    for (int a = 0; a < 6; ++a)
        for (int b = 0; b < 5; ++b)
            for (int c = 0; c < 4; ++c)
                t.file("d" + std::to_string(a) + "/e" + std::to_string(b) + "/f" +
                       std::to_string(c) + "/file" + std::to_string(c) + ".dat",
                       size_t(10 + a * 100 + b * 10 + c));

    auto one  = scan_and_build(t.path(), default_cfg(1));
    auto many = scan_and_build(t.path(), default_cfg(8));

    REQUIRE(one->result.total_files == 120);
    REQUIRE(many->result.total_files == one->result.total_files);
    REQUIRE(many->result.total_size  == one->result.total_size);
    REQUIRE(many->result.total_dirs  == one->result.total_dirs);

    auto paths = [](const Index& idx) {
        std::vector<std::string> v;
        for (auto* f : idx.top_files(idx.total_files())) v.push_back(f->path);
        return v;
    };
    REQUIRE(paths(one->index) == paths(many->index));
}

TEST_CASE("scan: progress counters", "[scan][progress]") {
    TempTree t;
    disktree::test::make_standard_tree(t);
    utils::ProgressTracker progress;
    Scanner scanner(progress);
    auto result = scanner.scan(t.path(), default_cfg());
    auto snap = progress.snapshot();
    REQUIRE(snap.files_scanned == 6);
    REQUIRE(snap.bytes_seen == 8150);
    REQUIRE(snap.dirs_scanned == 5);   // root + 4
}

TEST_CASE("scan: bad paths throw", "[scan]") {
    TempTree t;
    t.file("plain.txt", 1);
    utils::ProgressTracker progress;
    Scanner scanner(progress);
    REQUIRE_THROWS(scanner.scan(t.path() / "does-not-exist", default_cfg()));
    REQUIRE_THROWS(scanner.scan(t.path() / "plain.txt", default_cfg()));
}

TEST_CASE("matches_pattern", "[scan][pattern]") {
    REQUIRE(Scanner::matches_pattern("node_modules", "node_modules"));
    REQUIRE(Scanner::matches_pattern("NODE_MODULES", "node_modules"));
    REQUIRE(Scanner::matches_pattern("*.log", "server.LOG"));
    REQUIRE(Scanner::matches_pattern("*", "anything"));
    REQUIRE(Scanner::matches_pattern("file?.txt", "file1.txt"));
    REQUIRE(Scanner::matches_pattern("*cache*", "npm-cache-v2"));
    REQUIRE_FALSE(Scanner::matches_pattern("*.log", "log.txt"));
    REQUIRE_FALSE(Scanner::matches_pattern("file?.txt", "file12.txt"));
    REQUIRE_FALSE(Scanner::matches_pattern(".git", ".github"));
}
