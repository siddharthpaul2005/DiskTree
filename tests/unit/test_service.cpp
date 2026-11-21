#include <catch2/catch_test_macros.hpp>

#include "disktree/core/snapshot_store.hpp"
#include "disktree/platform/drives.hpp"
#include "disktree/service/api_server.hpp"
#include "disktree/service/engine.hpp"
#include "temp_tree.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <future>
#include <thread>

using namespace disktree;
using namespace disktree::service;
using json = nlohmann::json;
using namespace std::chrono_literals;

namespace {

Engine::ScanRequest req_for(const std::filesystem::path& p) {
    Engine::ScanRequest r;
    r.path = p.string();
    r.threads = 4;
    return r;
}

// Engine + temp db + standard tree, scanned and finished
struct ScannedEngine {
    test::TempTree data;
    test::TempTree dbdir;
    std::unique_ptr<Engine> engine;

    ScannedEngine() {
        test::make_standard_tree(data);
        engine = std::make_unique<Engine>(Engine::Options{dbdir.path() / "t.db"});
        engine->start_scan(req_for(data.path()));
        REQUIRE(engine->wait_idle(30s));
        REQUIRE(engine->status()["state"] == "done");
    }
};

} // namespace

// ── SnapshotStore ────────────────────────────────────────────────

TEST_CASE("store: save, list, load, latest, remove", "[store]") {
    test::TempTree dir;
    SnapshotStore store(dir.path() / "sub" / "snap.db");   // creates parent dirs

    Snapshot a;
    a.scan_path = "D:\\Data";
    a.created_at = 1000;
    a.total_size = 30;
    a.files["x.bin"] = {10, 5};
    a.files["y\\z.txt"] = {20, 6};

    Snapshot b = a;
    b.created_at = 2000;
    b.scan_path = "d:/data/";            // same path, different spelling

    Snapshot c = a;
    c.scan_path = "E:\\Other";

    int64_t ia = store.save(a, "first");
    int64_t ib = store.save(b);
    int64_t ic = store.save(c);
    REQUIRE(ia != ib);

    auto all = store.list();
    REQUIRE(all.size() == 3);
    REQUIRE(all[0].created_at >= all[1].created_at);
    REQUIRE(store.list("D:\\DATA").size() == 2);
    REQUIRE(store.latest_for("D:\\Data") == ib);
    REQUIRE(store.latest_for("E:\\Other") == ic);
    REQUIRE_FALSE(store.latest_for("Z:\\").has_value());

    auto info = store.info(ia);
    REQUIRE(info);
    REQUIRE(info->label == "first");
    REQUIRE(info->total_size == 30);

    Snapshot back = store.load(ia);
    REQUIRE(back.files.size() == 2);
    REQUIRE(back.files.at("y\\z.txt").size == 20);
    REQUIRE_THROWS(store.load(9999));

    REQUIRE(store.remove(ia));
    REQUIRE_FALSE(store.remove(ia));
    REQUIRE(store.list().size() == 2);
}

TEST_CASE("store: data survives reopening", "[store]") {
    test::TempTree dir;
    auto path = dir.path() / "persist.db";
    {
        SnapshotStore s(path);
        Snapshot a;
        a.scan_path = "C:\\";
        a.files["f"] = {1, 1};
        s.save(a);
    }
    SnapshotStore s2(path);
    REQUIRE(s2.list().size() == 1);
}

// ── Engine ───────────────────────────────────────────────────────

TEST_CASE("engine: queries before any scan", "[engine]") {
    test::TempTree dbdir;
    Engine e({dbdir.path() / "t.db"});
    REQUIRE(e.status()["state"] == "idle");
    REQUIRE(e.status()["result"].is_null());
    REQUIRE_THROWS_AS(e.node("", 1, 10, 0), NoScanError);
    REQUIRE_THROWS_AS(e.top_files(5), NoScanError);
    REQUIRE(e.health()["version"] == DISKTREE_VERSION);
    REQUIRE(e.drives().is_array());
}

TEST_CASE("engine: rejects bad scan requests", "[engine]") {
    test::TempTree t;
    t.file("a.txt", 1);
    test::TempTree dbdir;
    Engine e({dbdir.path() / "t.db"});
    REQUIRE_THROWS_AS(e.start_scan(req_for(t.path() / "missing")), BadRequestError);
    REQUIRE_THROWS_AS(e.start_scan(req_for(t.path() / "a.txt")), BadRequestError);
    Engine::ScanRequest empty;
    REQUIRE_THROWS_AS(e.start_scan(empty), BadRequestError);
}

TEST_CASE("engine: scan, then query the result", "[engine]") {
    ScannedEngine s;
    Engine& e = *s.engine;

    auto st = e.status();
    REQUIRE(st["result"]["total_size"] == 8150);
    REQUIRE(st["result"]["total_files"] == 6);
    REQUIRE(st["running"] == false);

    SECTION("node: root with depth 2") {
        auto n = e.node("", 2, 100, 0.0);
        REQUIRE(n["size"] == 8150);
        REQUIRE(n["breadcrumb"].size() == 1);
        auto& kids = n["children"];
        // dirs and files merged by size: big.bin(5000), sub1(2350), sub2(700), a.txt(100), empty(0 -> dropped)
        REQUIRE(kids[0]["name"] == "big.bin");
        REQUIRE(kids[1]["name"] == "sub1");
        REQUIRE(kids[1]["children"].size() == 3);   // d.MP4, c.txt, deep
        REQUIRE(kids[1]["files"] == 3);
    }
    SECTION("node: by path, case-insensitive, with breadcrumb") {
        auto sub1 = (s.data.path() / "SUB1").string();
        auto n = e.node(sub1, 1, 100, 0.0);
        REQUIRE(n["name"] == "sub1");
        REQUIRE(n["breadcrumb"].size() == 2);
        REQUIRE_THROWS_AS(e.node((s.data.path() / "nope").string(), 1, 10, 0), NotFoundError);
    }
    SECTION("node: small children fold into 'other'") {
        auto n = e.node("", 1, 2, 0.0);              // only 2 entries allowed
        auto& kids = n["children"];
        REQUIRE(kids.size() == 3);
        REQUIRE(kids[2]["type"] == "other");
        REQUIRE(kids[2]["size"] == 700 + 100);       // sub2 + a.txt (empty dir is 0)
        auto m = e.node("", 1, 100, 0.05);           // < 5% of 8150 = 407 bytes folds
        REQUIRE(m["children"].back()["type"] == "other");
        REQUIRE(m["children"].back()["size"] == 100);
    }
    SECTION("lists and search") {
        REQUIRE(e.top_files(2)[0]["name"] == "big.bin");
        REQUIRE(e.top_dirs(1)[0]["name"] == "sub1");
        REQUIRE(e.extensions(10)[0]["ext"] == ".bin");
        auto r = e.search("MP4", 10);
        REQUIRE(r["files"].size() == 1);
        REQUIRE(e.search("sub", 10)["dirs"].size() == 2);
        REQUIRE(e.search("", 10)["files"].empty());
    }
    SECTION("snapshots and diff") {
        REQUIRE(e.list_snapshots().empty());
        auto saved = e.save_snapshot("before");
        int64_t id = saved["id"];
        auto list = e.list_snapshots();
        REQUIRE(list.size() == 1);
        REQUIRE(list[0]["matches_current"] == true);
        REQUIRE(list[0]["label"] == "before");

        s.data.file("sub2/new.iso", 9000);
        s.data.remove("a.txt");
        e.start_scan(req_for(s.data.path()));
        REQUIRE(e.wait_idle(30s));

        auto d = e.diff(id, 50);
        REQUIRE(d["same_path"] == true);
        REQUIRE(d["counts"]["added"] == 1);
        REQUIRE(d["counts"]["removed"] == 1);
        REQUIRE(d["net_delta"] == 9000 - 100);
        REQUIRE(d["dirs"][0]["path"] == "sub2");

        REQUIRE_THROWS_AS(e.diff(id + 100, 10), NotFoundError);
        e.delete_snapshot(id);
        REQUIRE(e.list_snapshots().empty());
        REQUIRE_THROWS_AS(e.delete_snapshot(id), NotFoundError);
    }
}

TEST_CASE("engine: a second scan is refused while one runs; cancel works", "[engine]") {
    test::TempTree t;
    for (int a = 0; a < 40; ++a)
        for (int b = 0; b < 40; ++b)
            t.file("d" + std::to_string(a) + "/f" + std::to_string(b) + ".dat", 10);
    test::TempTree dbdir;
    Engine e({dbdir.path() / "t.db"});

    e.start_scan(req_for(t.path()));
    // Either still running (then a second start must fail) or already done
    try {
        e.start_scan(req_for(t.path()));
        REQUIRE(e.status()["state"] != "scanning");   // only allowed if the first finished
    } catch (const BadRequestError&) {
        SUCCEED("second scan refused while running");
    }
    e.cancel_scan();
    REQUIRE(e.wait_idle(30s));
    auto state = e.status()["state"].get<std::string>();
    REQUIRE((state == "cancelled" || state == "done"));
}

// ── HTTP API ─────────────────────────────────────────────────────

namespace {

struct RunningServer {
    test::TempTree dbdir;
    Engine engine{Engine::Options{dbdir.path() / "t.db"}};
    std::promise<int> port_promise;
    std::thread th;
    int port = 0;

    explicit RunningServer(std::string token) {
        ServerOptions o;
        o.token = std::move(token);
        o.allowed_origins = default_allowed_origins();
        o.print_ready = false;
        o.on_ready = [this](int p) { port_promise.set_value(p); };
        th = std::thread([this, o] { run_api_server(engine, o); });
        port = port_promise.get_future().get();
    }
    ~RunningServer() {
        httplib::Client c("127.0.0.1", port);
        c.Post("/api/shutdown", httplib::Headers{{"X-DiskTree-Token", "secret"}}, "", "application/json");
        th.join();
    }
};

} // namespace

TEST_CASE("api: auth, CORS and a full scan over HTTP", "[api]") {
    RunningServer srv("secret");
    httplib::Client c("127.0.0.1", srv.port);
    httplib::Headers auth{{"X-DiskTree-Token", "secret"}};

    SECTION("token is required") {
        REQUIRE(c.Get("/api/health")->status == 401);
        REQUIRE(c.Get("/api/health", httplib::Headers{{"X-DiskTree-Token", "wrong"}})->status == 401);
        auto ok = c.Get("/api/health", auth);
        REQUIRE(ok->status == 200);
        REQUIRE(json::parse(ok->body)["name"] == "disktree");
        REQUIRE(c.Get("/api/health?token=secret")->status == 200);
    }
    SECTION("only known origins") {
        httplib::Headers evil{{"X-DiskTree-Token", "secret"}, {"Origin", "https://evil.example"}};
        REQUIRE(c.Get("/api/health", evil)->status == 403);

        httplib::Headers app{{"X-DiskTree-Token", "secret"}, {"Origin", "http://tauri.localhost"}};
        auto r = c.Get("/api/health", app);
        REQUIRE(r->status == 200);
        REQUIRE(r->get_header_value("Access-Control-Allow-Origin") == "http://tauri.localhost");

        auto pre = c.Options("/api/scan", httplib::Headers{{"Origin", "http://tauri.localhost"}});
        REQUIRE(pre->status == 204);
    }
    SECTION("scan lifecycle") {
        REQUIRE(c.Get("/api/node", auth)->status == 409);   // no scan yet

        test::TempTree t;
        test::make_standard_tree(t);
        json body = {{"path", t.path().string()}};
        auto start = c.Post("/api/scan", auth, body.dump(), "application/json");
        REQUIRE(start->status == 202);

        REQUIRE(srv.engine.wait_idle(30s));
        auto st = json::parse(c.Get("/api/scan/status", auth)->body);
        REQUIRE(st["state"] == "done");

        auto node = c.Get("/api/node?depth=1", auth);
        REQUIRE(node->status == 200);
        REQUIRE(json::parse(node->body)["size"] == 8150);

        REQUIRE(c.Get("/api/node?depth=abc", auth)->status == 400);
        REQUIRE(c.Post("/api/scan", auth, "{not json", "application/json")->status == 400);
        json bad = {{"path", (t.path() / "missing").string()}};
        REQUIRE(c.Post("/api/scan", auth, bad.dump(), "application/json")->status == 400);

        auto snap = c.Post("/api/snapshots", auth, R"({"label":"x"})", "application/json");
        REQUIRE(snap->status == 201);
        int64_t id = json::parse(snap->body)["id"];
        auto diff = c.Get(("/api/diff?snapshot=" + std::to_string(id)).c_str(), auth);
        REQUIRE(diff->status == 200);
        REQUIRE(json::parse(diff->body)["net_delta"] == 0);
        REQUIRE(c.Delete(("/api/snapshots/" + std::to_string(id)).c_str(), auth)->status == 200);
        REQUIRE(c.Delete(("/api/snapshots/" + std::to_string(id)).c_str(), auth)->status == 404);

        json reveal = {{"path", (t.path() / "missing.txt").string()}};
        REQUIRE(c.Post("/api/reveal", auth, reveal.dump(), "application/json")->status == 404);
    }
}

TEST_CASE("platform: known user folders exist", "[service][places]") {
    auto places = disktree::platform::list_places();
    for (const auto& p : places) {
        INFO(p.id << " -> " << p.path);
        REQUIRE(!p.name.empty());
        REQUIRE(std::filesystem::is_directory(std::filesystem::u8path(p.path)));
    }
#ifdef _WIN32
    // Every Windows account has a profile folder
    REQUIRE(std::any_of(places.begin(), places.end(), [](const auto& p) { return p.id == "profile"; }));
#endif
}
