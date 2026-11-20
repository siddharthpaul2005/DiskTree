#include <catch2/catch_test_macros.hpp>

#include "disktree/core/arena.hpp"
#include "disktree/core/builder.hpp"
#include "disktree/core/index.hpp"
#include "disktree/core/scanner.hpp"
#include "disktree/export/csv_export.hpp"
#include "disktree/export/json_export.hpp"
#include "disktree/utils/humanize.hpp"
#include "disktree/utils/progress.hpp"
#include "disktree/utils/thread_pool.hpp"
#include "temp_tree.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <sstream>
#include <thread>
#include <vector>

using namespace disktree;

// ── Exporters ────────────────────────────────────────────────────

TEST_CASE("export: CSV and JSON", "[export]") {
    disktree::test::TempTree t;
    disktree::test::make_standard_tree(t);

    utils::ProgressTracker progress;
    Scanner scanner(progress);
    Scanner::Config cfg;
    cfg.exclude_patterns = Scanner::default_excludes();
    auto result = scanner.scan(t.path(), cfg);
    Builder{}.build(result);
    Index index;
    index.build(result);

    SECTION("CSV has a header plus one row per file and dir") {
        std::string csv = export_::to_csv(result, index);
        std::istringstream in(csv);
        std::string line;
        std::vector<std::string> lines;
        while (std::getline(in, line)) lines.push_back(line);

        REQUIRE(lines[0] == "type,path,size_bytes,size_human,extension,pct_of_parent");
        REQUIRE(lines.size() == 1 + 6 + 4);
        REQUIRE(lines[1].starts_with("file,\""));
        REQUIRE(lines[1].find("big.bin\",5000,") != std::string::npos);
    }

    SECTION("JSON parses and carries totals") {
        auto j = nlohmann::json::parse(export_::to_json(result, index));
        REQUIRE(j["total_size"] == 8150);
        REQUIRE(j["total_files"] == 6);
        REQUIRE(j["top_files"][0]["size"] == 5000);
        REQUIRE(j["tree"]["type"] == "dir");
        REQUIRE(j["extensions"].size() == 4);   // .bin .mp4 .log .txt
    }
}

// ── humanize: parse_size / format_signed_bytes ───────────────────

TEST_CASE("parse_size", "[humanize]") {
    REQUIRE(utils::parse_size("500")      == 500u);
    REQUIRE(utils::parse_size("500B")     == 500u);
    REQUIRE(utils::parse_size("1KB")      == 1024u);
    REQUIRE(utils::parse_size("1.5kb")    == 1536u);
    REQUIRE(utils::parse_size(" 2 MB ")   == 2u * 1024 * 1024);
    REQUIRE(utils::parse_size("1G")       == 1024ull * 1024 * 1024);
    REQUIRE(utils::parse_size("3TiB")     == 3ull << 40);
    REQUIRE_FALSE(utils::parse_size("").has_value());
    REQUIRE_FALSE(utils::parse_size("MB").has_value());
    REQUIRE_FALSE(utils::parse_size("12XB").has_value());
    REQUIRE_FALSE(utils::parse_size("1.2.3MB").has_value());
}

TEST_CASE("format_signed_bytes", "[humanize]") {
    REQUIRE(utils::format_signed_bytes(0)     == "0 B");
    REQUIRE(utils::format_signed_bytes(1536)  == "+1.50 KB");
    REQUIRE(utils::format_signed_bytes(-1024) == "-1.00 KB");
    REQUIRE(utils::format_signed_bytes(-5)    == "-5 B");
}

// ── ThreadPool ───────────────────────────────────────────────────

TEST_CASE("thread pool: results and nested submits", "[threads]") {
    utils::ThreadPool pool(4);
    REQUIRE(pool.thread_count() == 4);

    auto f = pool.submit([](int a, int b) { return a + b; }, 2, 3);
    REQUIRE(f.get() == 5);

    std::atomic<int> count{0};
    for (int i = 0; i < 50; ++i) {
        pool.submit([&] {
            // Tasks may spawn tasks (the scanner does this)
            for (int k = 0; k < 10; ++k)
                pool.submit([&] { count.fetch_add(1); });
            count.fetch_add(1);
        });
    }
    pool.wait_all();
    REQUIRE(count.load() == 50 * 11);

    // wait_all with nothing pending returns immediately
    pool.wait_all();
}

// ── Arena ────────────────────────────────────────────────────────

TEST_CASE("arena: concurrent allocation and interning", "[arena]") {
    ScanArena arena(4096);   // tiny slabs → many slab switches
    std::vector<std::thread> threads;
    std::vector<std::vector<FileNode*>> per_thread(4);
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&, t] {
            for (int i = 0; i < 500; ++i) {
                FileNode* f = arena.alloc_file();
                f->path = "file_" + std::to_string(t) + "_" + std::to_string(i);
                f->size = uint64_t(i);
                f->extension = arena.intern(i % 2 ? ".a" : ".b");
                per_thread[size_t(t)].push_back(f);
            }
        });
    }
    for (auto& th : threads) th.join();

    REQUIRE(arena.file_node_count() == 2000);
    for (int t = 0; t < 4; ++t)
        for (int i = 0; i < 500; ++i) {
            const FileNode* f = per_thread[size_t(t)][size_t(i)];
            REQUIRE(f->size == uint64_t(i));
            REQUIRE(f->path == "file_" + std::to_string(t) + "_" + std::to_string(i));
        }
    // Interned strings share storage
    REQUIRE(arena.intern(".a").data() == arena.intern(".a").data());
}

// ── Progress line ────────────────────────────────────────────────

TEST_CASE("progress line formatting", "[progress]") {
    ScanProgress p;
    p.files_scanned = 1234;
    p.bytes_seen    = 2048;

    auto line = utils::ProgressReporter::format_line(p, "C:\\short");
    REQUIRE(line.size() == utils::ProgressReporter::LINE_WIDTH);
    REQUIRE(line.find("1,234 files") != std::string::npos);
    REQUIRE(line.find("2.00 KB") != std::string::npos);
    REQUIRE(line.find("C:\\short") != std::string::npos);

    std::string long_path(300, 'z');
    auto cut = utils::ProgressReporter::format_line(p, long_path);
    REQUIRE(cut.size() == utils::ProgressReporter::LINE_WIDTH);
    REQUIRE(cut.find("...") != std::string::npos);
}
