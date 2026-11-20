#include <catch2/catch_test_macros.hpp>

#include "disktree/core/scanner.hpp"
#include "disktree/core/builder.hpp"
#include "disktree/core/snapshot.hpp"
#include "temp_tree.hpp"

#include <algorithm>

using namespace disktree;
using disktree::test::TempTree;

namespace {

Snapshot scan_snapshot(const std::filesystem::path& root) {
    utils::ProgressTracker progress;
    Scanner scanner(progress);
    Scanner::Config cfg;
    cfg.exclude_patterns = Scanner::default_excludes();
    auto result = scanner.scan(root, cfg);
    Builder{}.build(result);
    return snapshot_from_scan(result);
}

std::string rel(std::string_view a, std::string_view b) {
    return (std::filesystem::path(a) / b).string();
}

bool has_path(const std::vector<FileDelta>& v, const std::string& p) {
    return std::any_of(v.begin(), v.end(), [&](const FileDelta& d) { return d.path == p; });
}

} // namespace

TEST_CASE("snapshot: built from scan with relative paths", "[snapshot]") {
    TempTree t;
    disktree::test::make_standard_tree(t);
    auto snap = scan_snapshot(t.path());

    REQUIRE(snap.files.size() == 6);
    REQUIRE(snap.total_size == 8150);
    REQUIRE(snap.files.contains("big.bin"));
    REQUIRE(snap.files.contains(rel("sub1", "c.txt")));
    REQUIRE(snap.files.at(rel("sub1", "d.MP4")).size == 2000);
    REQUIRE(snap.created_at > 0);
}

TEST_CASE("snapshot: JSON and zstd round trip", "[snapshot]") {
    TempTree t;
    disktree::test::make_standard_tree(t);
    auto snap = scan_snapshot(t.path());

    auto json = snapshot_to_json(snap);
    auto back = snapshot_from_json(json, "x");
    REQUIRE(back.id == "x");
    REQUIRE(back.scan_path == snap.scan_path);
    REQUIRE(back.total_size == snap.total_size);
    REQUIRE(back.files.size() == snap.files.size());
    for (const auto& [p, e] : snap.files) {
        REQUIRE(back.files.at(p).size == e.size);
        REQUIRE(back.files.at(p).modified == e.modified);
    }

    auto packed = zstd_compress(json);
    REQUIRE(is_zstd(packed));
    REQUIRE_FALSE(is_zstd(json));
    REQUIRE(zstd_decompress(packed) == json);
    REQUIRE_THROWS(zstd_decompress(packed.substr(0, packed.size() / 2)));
}

TEST_CASE("snapshot: save, list, load, latest", "[snapshot]") {
    TempTree data;
    disktree::test::make_standard_tree(data);
    TempTree store;

    auto snap  = scan_snapshot(data.path());
    auto first = save_snapshot(snap, store.path());
    auto second = save_snapshot(snap, store.path());   // same second → _2 suffix
    REQUIRE(first != second);
    REQUIRE(std::filesystem::exists(first));
    REQUIRE(first.string().ends_with(".json.zst"));

    auto listed = list_snapshots(store.path());
    REQUIRE(listed.size() == 2);

    auto loaded = load_snapshot(first);
    REQUIRE(loaded.files.size() == snap.files.size());
    REQUIRE(loaded.id == first.filename().string());

    REQUIRE(latest_snapshot_for(store.path(), snap.scan_path).has_value());
    REQUIRE_FALSE(latest_snapshot_for(store.path(), "Z:\\nowhere").has_value());
}

TEST_CASE("snapshot: rejects bad and legacy files", "[snapshot]") {
    REQUIRE_THROWS(snapshot_from_json("not json"));
    REQUIRE_THROWS(snapshot_from_json(R"({"hello": 1})"));
    // disktree 0.1 wrote the export JSON (tree + top_files) as its snapshot
    REQUIRE_THROWS(snapshot_from_json(R"({"tree": {}, "top_files": []})"));
    REQUIRE_THROWS(snapshot_from_json(
        R"({"format":"disktree-snapshot","version":99,"files":[]})"));
}

TEST_CASE("diff: detects added, removed, grown, shrunk", "[diff]") {
    TempTree t;
    disktree::test::make_standard_tree(t);
    auto before = scan_snapshot(t.path());

    t.file("sub1/c.txt", 1300);      // grew by 1000
    t.file("big.bin", 4000);         // shrank by 1000
    t.remove("sub2/f.log");          // removed 700
    t.file("sub2/new.iso", 9000);    // added 9000
    t.file("newdir/n.txt", 10);      // added 10

    auto after = scan_snapshot(t.path());
    auto d = diff_snapshots(before, after);

    REQUIRE(d.added.size() == 2);
    REQUIRE(d.added[0].path == rel("sub2", "new.iso"));   // biggest first
    REQUIRE(d.added[0].size_before == -1);
    REQUIRE(has_path(d.added, rel("newdir", "n.txt")));

    REQUIRE(d.removed.size() == 1);
    REQUIRE(d.removed[0].path == rel("sub2", "f.log"));
    REQUIRE(d.removed[0].delta() == -700);

    REQUIRE(d.grown.size() == 1);
    REQUIRE(d.grown[0].delta() == 1000);
    REQUIRE(d.shrunk.size() == 1);
    REQUIRE(d.shrunk[0].path == "big.bin");

    REQUIRE(d.unchanged == 3);       // a.txt, d.MP4, deep/e.txt
    REQUIRE(d.net_delta == 9000 + 10 - 700 + 1000 - 1000);

    // Per top-level folder: sub2 +8300, newdir +10, sub1 +1000, root files -1000
    REQUIRE(d.dirs.size() == 4);
    REQUIRE(d.dirs[0].path == "sub2");
    REQUIRE(d.dirs[0].delta() == 8300);
    REQUIRE(has_path(d.dirs, "(files in root)"));
}

TEST_CASE("diff: identical snapshots", "[diff]") {
    TempTree t;
    disktree::test::make_standard_tree(t);
    auto a = scan_snapshot(t.path());
    auto d = diff_snapshots(a, a);
    REQUIRE(d.added.empty());
    REQUIRE(d.removed.empty());
    REQUIRE(d.grown.empty());
    REQUIRE(d.shrunk.empty());
    REQUIRE(d.dirs.empty());
    REQUIRE(d.net_delta == 0);
    REQUIRE(d.unchanged == 6);
}
