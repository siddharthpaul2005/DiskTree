#include "disktree/core/snapshot.hpp"

#include <nlohmann/json.hpp>
#include <zstd.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iterator>
#include <queue>
#include <stdexcept>

namespace fs = std::filesystem;
using ojson = nlohmann::ordered_json;
using json  = nlohmann::json;

namespace disktree {

// ── Helpers ──────────────────────────────────────────────────────

static std::string path_to_str(const fs::path& p) {
    auto u8 = p.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

static fs::path str_to_path(const std::string& s) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}

// Lower-case, '/' → '\', no trailing separator: used to compare scan roots
static std::string normalise_root(std::string_view p) {
    std::string out;
    out.reserve(p.size());
    for (char c : p)
        out += (c == '/') ? '\\' : char(std::tolower(static_cast<unsigned char>(c)));
    while (out.size() > 1 && out.back() == '\\' && out[out.size() - 2] != ':')
        out.pop_back();
    return out;
}

static std::string relative_to(const std::string& full, const std::string& root) {
    if (full.size() >= root.size() && full.compare(0, root.size(), root) == 0) {
        size_t i = root.size();
        while (i < full.size() && (full[i] == '\\' || full[i] == '/')) ++i;
        return full.substr(i);
    }
    return full;
}

static std::string read_file(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot open snapshot: " + path_to_str(path));
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

// ── Build ────────────────────────────────────────────────────────

Snapshot snapshot_from_scan(const ScanResult& result) {
    Snapshot snap;
    snap.id          = "current scan";
    snap.scan_path   = result.root ? result.root->path : path_to_str(result.scan_path);
    snap.created_at  = std::chrono::duration_cast<std::chrono::seconds>(
                           result.scanned_at.time_since_epoch()).count();
    snap.total_size  = result.total_size;
    snap.total_files = result.total_files;
    snap.total_dirs  = result.total_dirs;
    if (!result.root) return snap;

    snap.files.reserve(size_t(result.total_files));
    std::queue<const DirNode*> q;
    q.push(result.root);
    while (!q.empty()) {
        const DirNode* d = q.front(); q.pop();
        for (const FileNode* f : d->child_files) {
            if (f->is_symlink) continue;
            snap.files[relative_to(f->path, snap.scan_path)] = {f->size, f->modified};
        }
        for (const DirNode* c : d->child_dirs) q.push(c);
    }
    return snap;
}

// ── JSON ─────────────────────────────────────────────────────────

std::string snapshot_to_json(const Snapshot& snap) {
    // ordered_json keeps the small header fields before the big array
    ojson j;
    j["format"]      = "disktree-snapshot";
    j["version"]     = Snapshot::FORMAT_VERSION;
    j["scan_path"]   = snap.scan_path;
    j["created_at"]  = snap.created_at;
    j["total_size"]  = snap.total_size;
    j["total_files"] = snap.total_files;
    j["total_dirs"]  = snap.total_dirs;

    // Sorted for deterministic output
    std::vector<const std::pair<const std::string, SnapshotEntry>*> entries;
    entries.reserve(snap.files.size());
    for (const auto& kv : snap.files) entries.push_back(&kv);
    std::sort(entries.begin(), entries.end(),
              [](auto* a, auto* b) { return a->first < b->first; });

    ojson files = ojson::array();
    for (auto* kv : entries)
        files.push_back(ojson::array({kv->first, kv->second.size, kv->second.modified}));
    j["files"] = std::move(files);

    return j.dump();
}

Snapshot snapshot_from_json(std::string_view text, std::string id) {
    json j;
    try {
        j = json::parse(text);
    } catch (const json::exception& e) {
        throw std::runtime_error(std::string("Snapshot is not valid JSON: ") + e.what());
    }
    if (!j.is_object())
        throw std::runtime_error("Snapshot is not a JSON object");

    if (!j.contains("format") || j.value("format", "") != "disktree-snapshot") {
        if (j.contains("tree") && j.contains("top_files"))
            throw std::runtime_error(
                "This snapshot was made by disktree 0.1 and only contains part of the "
                "file list, so it can't be diffed. Take a new one with --snapshot.");
        throw std::runtime_error("Not a disktree snapshot file");
    }
    int version = j.value("version", 0);
    if (version != Snapshot::FORMAT_VERSION)
        throw std::runtime_error("Unsupported snapshot version " + std::to_string(version));

    Snapshot snap;
    snap.id          = std::move(id);
    snap.scan_path   = j.value("scan_path", "");
    snap.created_at  = j.value("created_at", int64_t(0));
    snap.total_size  = j.value("total_size", uint64_t(0));
    snap.total_files = j.value("total_files", uint64_t(0));
    snap.total_dirs  = j.value("total_dirs", uint64_t(0));

    const auto& files = j.at("files");
    snap.files.reserve(files.size());
    for (const auto& e : files) {
        if (!e.is_array() || e.size() < 2)
            throw std::runtime_error("Malformed file entry in snapshot");
        SnapshotEntry entry;
        entry.size     = e[1].get<uint64_t>();
        entry.modified = e.size() > 2 ? e[2].get<int64_t>() : 0;
        snap.files.emplace(e[0].get<std::string>(), entry);
    }
    return snap;
}

// ── zstd ─────────────────────────────────────────────────────────

bool is_zstd(std::string_view data) {
    return data.size() >= 4 &&
           static_cast<unsigned char>(data[0]) == 0x28 &&
           static_cast<unsigned char>(data[1]) == 0xB5 &&
           static_cast<unsigned char>(data[2]) == 0x2F &&
           static_cast<unsigned char>(data[3]) == 0xFD;
}

std::string zstd_compress(std::string_view data, int level) {
    std::string out(ZSTD_compressBound(data.size()), '\0');
    size_t n = ZSTD_compress(out.data(), out.size(), data.data(), data.size(), level);
    if (ZSTD_isError(n))
        throw std::runtime_error(std::string("zstd compress failed: ") + ZSTD_getErrorName(n));
    out.resize(n);
    return out;
}

std::string zstd_decompress(std::string_view data) {
    ZSTD_DCtx* dctx = ZSTD_createDCtx();
    if (!dctx) throw std::runtime_error("zstd: out of memory");

    std::string out;
    std::string buf(ZSTD_DStreamOutSize(), '\0');
    ZSTD_inBuffer in{data.data(), data.size(), 0};
    size_t ret = 0;
    while (in.pos < in.size) {
        ZSTD_outBuffer o{buf.data(), buf.size(), 0};
        ret = ZSTD_decompressStream(dctx, &o, &in);
        if (ZSTD_isError(ret)) {
            ZSTD_freeDCtx(dctx);
            throw std::runtime_error(std::string("Corrupt snapshot (zstd): ") +
                                     ZSTD_getErrorName(ret));
        }
        out.append(buf.data(), o.pos);
    }
    // Flush anything still buffered
    while (ret != 0) {
        ZSTD_outBuffer o{buf.data(), buf.size(), 0};
        ret = ZSTD_decompressStream(dctx, &o, &in);
        if (ZSTD_isError(ret) || o.pos == 0) break;
        out.append(buf.data(), o.pos);
    }
    ZSTD_freeDCtx(dctx);
    if (ret != 0) throw std::runtime_error("Corrupt snapshot: truncated zstd data");
    return out;
}

// ── Files ────────────────────────────────────────────────────────

fs::path default_snapshot_dir() {
    const char* home = std::getenv("USERPROFILE");
    if (!home || !*home) home = std::getenv("HOME");
    fs::path base = (home && *home) ? str_to_path(home) : fs::current_path();
    return base / ".disktree" / "snapshots";
}

fs::path save_snapshot(const Snapshot& snap, const fs::path& dir) {
    fs::create_directories(dir);

    std::time_t t = static_cast<std::time_t>(snap.created_at ? snap.created_at
                                                              : std::time(nullptr));
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &tm);

    fs::path path = dir / (std::string(stamp) + ".json.zst");
    for (int i = 2; fs::exists(path); ++i)
        path = dir / (std::string(stamp) + "_" + std::to_string(i) + ".json.zst");

    std::string blob = zstd_compress(snapshot_to_json(snap));
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot write snapshot: " + path_to_str(path));
    f.write(blob.data(), std::streamsize(blob.size()));
    if (!f) throw std::runtime_error("Failed writing snapshot: " + path_to_str(path));
    return path;
}

Snapshot load_snapshot(const fs::path& path) {
    std::string raw = read_file(path);
    if (is_zstd(raw)) raw = zstd_decompress(raw);
    return snapshot_from_json(raw, path_to_str(path.filename()));
}

std::vector<fs::path> list_snapshots(const fs::path& dir) {
    std::vector<fs::path> out;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return out;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec)) continue;
        std::string name = path_to_str(e.path().filename());
        if (name.ends_with(".json.zst") || name.ends_with(".json"))
            out.push_back(e.path());
    }
    // Names are timestamps, so reverse lexical order = newest first
    std::sort(out.begin(), out.end(), [](const fs::path& a, const fs::path& b) {
        return a.filename() > b.filename();
    });
    return out;
}

std::optional<fs::path> latest_snapshot_for(const fs::path& dir, std::string_view scan_path) {
    std::string want = normalise_root(scan_path);
    for (const auto& p : list_snapshots(dir)) {
        try {
            Snapshot s = load_snapshot(p);
            if (normalise_root(s.scan_path) == want) return p;
        } catch (...) {
            // unreadable / legacy snapshot: skip it
        }
    }
    return std::nullopt;
}

// ── Diff ─────────────────────────────────────────────────────────

static std::string top_component(const std::string& rel) {
    size_t sep = rel.find_first_of("\\/");
    return sep == std::string::npos ? "(files in root)" : rel.substr(0, sep);
}

static void sort_by_magnitude(std::vector<FileDelta>& v) {
    std::sort(v.begin(), v.end(), [](const FileDelta& a, const FileDelta& b) {
        int64_t da = std::llabs(a.delta()), db = std::llabs(b.delta());
        return da != db ? da > db : a.path < b.path;
    });
}

DiffResult diff_snapshots(const Snapshot& before, const Snapshot& after) {
    auto t0 = std::chrono::steady_clock::now();

    DiffResult r;
    r.snapshot_a_id = before.id;
    r.snapshot_b_id = after.id;

    std::unordered_map<std::string, std::pair<int64_t, int64_t>> dir_totals; // before, after
    auto add_dir = [&](const std::string& rel, int64_t b, int64_t a) {
        auto& t = dir_totals[top_component(rel)];
        t.first += b; t.second += a;
    };

    for (const auto& [path, a] : after.files) {
        auto it = before.files.find(path);
        if (it == before.files.end()) {
            r.added.push_back({path, -1, int64_t(a.size)});
            add_dir(path, 0, int64_t(a.size));
            continue;
        }
        const SnapshotEntry& b = it->second;
        add_dir(path, int64_t(b.size), int64_t(a.size));
        if (a.size > b.size)      r.grown.push_back({path, int64_t(b.size), int64_t(a.size)});
        else if (a.size < b.size) r.shrunk.push_back({path, int64_t(b.size), int64_t(a.size)});
        else                      ++r.unchanged;
    }
    for (const auto& [path, b] : before.files) {
        if (!after.files.contains(path)) {
            r.removed.push_back({path, int64_t(b.size), -1});
            add_dir(path, int64_t(b.size), 0);
        }
    }

    for (const auto* list : {&r.added, &r.removed, &r.grown, &r.shrunk})
        for (const auto& d : *list) r.net_delta += d.delta();

    for (const auto& [name, t] : dir_totals)
        if (t.first != t.second)
            r.dirs.push_back({name, t.first, t.second});

    sort_by_magnitude(r.added);
    sort_by_magnitude(r.removed);
    sort_by_magnitude(r.grown);
    sort_by_magnitude(r.shrunk);
    sort_by_magnitude(r.dirs);

    r.elapsed_sec = std::chrono::duration<double>(
                        std::chrono::steady_clock::now() - t0).count();
    return r;
}

} // namespace disktree
