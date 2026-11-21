#include "disktree/service/engine.hpp"
#include "disktree/core/arena.hpp"
#include "disktree/platform/drives.hpp"
#include "disktree/version.hpp"

#include <algorithm>
#include <cctype>
#include <queue>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace disktree::service {

namespace {

std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = char(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

fs::path u8path(const std::string& s) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}

json file_json(const FileNode* f) {
    return {{"type", "file"}, {"name", std::string(f->name)}, {"path", f->path},
            {"size", f->size}, {"ext", std::string(f->extension)},
            {"modified", f->modified}, {"pct", f->pct_of_parent}};
}

json dir_summary(const DirNode* d) {
    return {{"type", "dir"}, {"name", std::string(d->name)}, {"path", d->path},
            {"size", d->size}, {"files", d->file_count}, {"dirs", d->dir_count},
            {"modified", d->newest_mtime}, {"pct", d->pct_of_parent},
            {"denied", d->scan_failed}};
}

// One directory + children down to `depth` levels (see Engine::node)
json dir_tree(const DirNode* d, int depth, size_t max_children, uint64_t min_size) {
    json j = dir_summary(d);
    if (depth <= 0) {
        j["has_children"] = !d->child_dirs.empty() || !d->child_files.empty();
        return j;
    }

    // Merge dirs and files (each already sorted by size desc)
    json children = json::array();
    size_t di = 0, fi = 0;
    uint64_t other_size = 0;
    uint64_t other_count = 0;
    while (di < d->child_dirs.size() || fi < d->child_files.size()) {
        bool take_dir = fi >= d->child_files.size() ||
                        (di < d->child_dirs.size() &&
                         d->child_dirs[di]->size >= d->child_files[fi]->size);
        uint64_t size = take_dir ? d->child_dirs[di]->size : d->child_files[fi]->size;
        bool keep = children.size() < max_children && size >= min_size && size > 0;
        if (keep) {
            if (take_dir) children.push_back(dir_tree(d->child_dirs[di], depth - 1,
                                                      max_children, min_size));
            else          children.push_back(file_json(d->child_files[fi]));
        } else {
            other_size += size;
            ++other_count;
        }
        take_dir ? ++di : ++fi;
    }
    if (other_count) {
        children.push_back({{"type", "other"},
                            {"name", std::to_string(other_count) + " smaller items"},
                            {"path", d->path}, {"size", other_size},
                            {"count", other_count},
                            {"pct", d->size ? double(other_size) / double(d->size) : 0.0}});
    }
    j["children"] = std::move(children);
    return j;
}

json delta_json(const FileDelta& d) {
    return {{"path", d.path}, {"before", d.size_before}, {"after", d.size_after},
            {"delta", d.delta()}};
}

json info_json(const SnapshotInfo& i) {
    return {{"id", i.id}, {"scan_path", i.scan_path}, {"label", i.label},
            {"created_at", i.created_at}, {"total_size", i.total_size},
            {"total_files", i.total_files}, {"total_dirs", i.total_dirs}};
}

} // namespace

Engine::Engine(Options opts)
    : _opts(std::move(opts)),
      _store(std::make_unique<SnapshotStore>(_opts.db_path.empty()
                                                 ? SnapshotStore::default_path()
                                                 : _opts.db_path))
{
    auto access = platform::enable_full_read_access();
    _elevated  = access.elevated;
    _full_read = access.full_read;
}

Engine::~Engine() {
    _cancel = true;
    if (_worker.joinable()) _worker.join();
}

json Engine::health() const {
    return {{"name", "disktree"}, {"version", DISKTREE_VERSION},
            {"db", _store->path().string()},
            {"elevated", _elevated}, {"full_read", _full_read}};
}

json Engine::drives() const {
    json arr = json::array();
    for (const auto& d : platform::list_drives()) {
        arr.push_back({{"root", d.root}, {"label", d.label}, {"filesystem", d.filesystem},
                       {"type", d.type}, {"total", d.total_bytes}, {"free", d.free_bytes},
                       {"used", d.total_bytes >= d.free_bytes ? d.total_bytes - d.free_bytes : 0},
                       {"ready", d.ready}});
    }
    return arr;
}

json Engine::places() const {
    json arr = json::array();
    for (const auto& p : platform::list_places())
        arr.push_back({{"id", p.id}, {"name", p.name}, {"path", p.path}});
    return arr;
}

// Fixed drives that can be read right now (no USB sticks, DVDs or network shares)
// Used/free space of the drives a scan covers completely: the drive itself
// for a scan of a drive root, every fixed drive for the whole PC.
// null when the scan is of an ordinary folder.
static json volume_usage(const std::vector<std::string>& roots) {
    if (roots.empty()) return nullptr;
    uint64_t total = 0, free = 0, mft = 0;
    bool mft_known = true;
    json names = json::array();
    for (const auto& d : platform::list_drives()) {
        bool match = std::any_of(roots.begin(), roots.end(),
                                 [&](const std::string& r) { return lower(r) == lower(d.root); });
        if (!match || !d.ready) continue;
        total += d.total_bytes;
        free  += d.free_bytes;
        names.push_back(d.root);
        if (auto m = platform::file_table_bytes(d.root)) mft += *m;
        else mft_known = false;
    }
    if (names.empty()) return nullptr;
    json v = {{"total", total}, {"free", free}, {"used", total - std::min(total, free)},
              {"drives", names}};
    // Only with administrator rights; the UI then splits it out of "hidden"
    v["file_table"] = mft_known ? json(mft) : json(nullptr);
    return v;
}

static std::vector<std::string> whole_pc_roots() {
    std::vector<std::string> roots;
    for (const auto& d : platform::list_drives())
        if (d.ready && d.type == "fixed") roots.push_back(d.root);
    return roots;
}

void Engine::start_scan(const ScanRequest& req_in) {
    ScanRequest req = req_in;
    if (req.whole_pc) {
        if (whole_pc_roots().empty()) throw BadRequestError("No readable fixed drives found");
        req.path = kWholePc;
    } else {
        if (req.path.empty()) throw BadRequestError("No path given");
        std::error_code ec;
        fs::path p = u8path(req.path);
        if (!fs::exists(p, ec))       throw BadRequestError("Path does not exist: " + req.path);
        if (!fs::is_directory(p, ec)) throw BadRequestError("Not a folder: " + req.path);
    }

    std::lock_guard lock(_state_mtx);
    if (_state == "scanning" || _state == "building")
        throw BadRequestError("A scan is already running");
    if (_worker.joinable()) _worker.join();   // previous scan has finished

    _cancel    = false;
    _state     = "scanning";
    _error.clear();
    _scan_path = req.path;
    _started   = std::chrono::steady_clock::now();
    _progress  = std::make_shared<utils::ProgressTracker>();
    _worker    = std::thread([this, req] { run_scan(req); });
}

void Engine::cancel_scan() {
    _cancel = true;
}

void Engine::run_scan(ScanRequest req) {
    std::shared_ptr<utils::ProgressTracker> progress;
    {
        std::lock_guard lock(_state_mtx);
        progress = _progress;
    }
    auto finish = [this](const std::string& state, const std::string& error = "") {
        std::lock_guard lock(_state_mtx);
        _state        = state;
        _error        = error;
        _last_elapsed = std::chrono::duration<double>(
                            std::chrono::steady_clock::now() - _started).count();
        _state_cv.notify_all();
    };

    try {
        Scanner::Config cfg;
        if (req.default_excludes) cfg.exclude_patterns = Scanner::default_excludes();
        cfg.exclude_patterns.insert(cfg.exclude_patterns.end(),
                                    req.excludes.begin(), req.excludes.end());
        cfg.n_threads = req.threads;
        cfg.cancel    = &_cancel;

        Scanner scanner(*progress);
        auto cur = std::make_shared<Current>();
        if (req.whole_pc) {
            // Scan each fixed drive, then hang them all under one "This PC" node
            auto t0 = std::chrono::steady_clock::now();
            for (const auto& root : whole_pc_roots()) {
                cur->parts.push_back(scanner.scan(u8path(root), cfg));
                if (cur->parts.back().cancelled) { finish("cancelled"); return; }
            }
            ScanResult& r = cur->result;
            r.arena = std::make_unique<ScanArena>();
            DirNode* top = r.arena->alloc_dir();
            top->path   = kWholePc;
            top->name   = top->path;
            top->depth  = 0;
            top->parent = nullptr;
            for (auto& part : cur->parts) {
                part.root->parent = top;
                top->child_dirs.push_back(part.root);
                r.skipped.insert(r.skipped.end(), part.skipped.begin(), part.skipped.end());
                r.total_allocated += part.total_allocated;
            }
            r.root        = top;
            r.scan_path   = u8path(kWholePc);
            r.scanned_at  = std::chrono::system_clock::now();
            r.elapsed_sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            r.platform    = cur->parts.empty() ? "" : cur->parts.front().platform;
        } else {
            cur->result = scanner.scan(u8path(req.path), cfg);
            if (cur->result.cancelled) { finish("cancelled"); return; }
        }

        {
            // `complete`: the scan covered whole drive(s), so drive used minus
            // what it found is space no listing can show
            std::vector<std::string> roots;
            bool complete = true;
            if (req.whole_pc) {
                for (const auto& part : cur->parts) roots.push_back(part.root->path);
            } else {
                fs::path p = cur->result.scan_path;
                complete = p.has_root_path() && p.relative_path().empty();
                if (p.has_root_path()) {
                    auto u8 = p.root_path().u8string();
                    roots.emplace_back(reinterpret_cast<const char*>(u8.data()), u8.size());
                }
            }
            cur->volume = volume_usage(roots);
            if (!cur->volume.is_null()) cur->volume["complete"] = complete;
        }

        { std::lock_guard lock(_state_mtx); _state = "building"; }
        Builder{}.build(cur->result);
        cur->index.build(cur->result);

        cur->by_path.reserve(cur->index.total_dirs() + 1);
        cur->by_path.emplace(normalise_scan_path(cur->result.root->path), cur->result.root);
        for (const DirNode* d : cur->index.top_dirs(cur->index.total_dirs()))
            cur->by_path.emplace(normalise_scan_path(d->path), d);

        if (_cancel) { finish("cancelled"); return; }
        {
            std::unique_lock lock(_result_mtx);
            _current = std::move(cur);
        }
        finish("done");
    } catch (const std::exception& e) {
        finish("error", e.what());
    }
}

json Engine::status() const {
    json j;
    {
        std::lock_guard lock(_state_mtx);
        bool running = _state == "scanning" || _state == "building";
        auto p = _progress ? _progress->snapshot() : ScanProgress{};
        j["state"]        = _state;
        j["running"]      = running;
        j["scan_path"]    = _scan_path;
        j["error"]        = _error;
        j["files"]        = p.files_scanned;
        j["dirs"]         = p.dirs_scanned;
        j["bytes"]        = p.bytes_seen;
        j["current_path"] = (_progress && running) ? _progress->current_path() : "";
        j["elapsed"]      = running ? std::chrono::duration<double>(
                                          std::chrono::steady_clock::now() - _started).count()
                                    : _last_elapsed;
    }
    std::shared_lock lock(_result_mtx);
    if (_current) {
        const auto& r = _current->result;
        j["result"] = {{"scan_path", r.root->path}, {"total_size", r.total_size},
                       {"total_files", r.total_files}, {"total_dirs", r.total_dirs},
                       {"allocated", r.total_allocated}, {"volume", _current->volume},
                       {"elapsed", r.elapsed_sec}, {"skipped", r.skipped.size()},
                       {"finished_at", std::chrono::duration_cast<std::chrono::seconds>(
                                           r.scanned_at.time_since_epoch()).count()}};
    } else {
        j["result"] = nullptr;
    }
    return j;
}

bool Engine::wait_idle(std::chrono::milliseconds timeout) const {
    std::unique_lock lock(_state_mtx);
    return _state_cv.wait_for(lock, timeout, [this] {
        return _state != "scanning" && _state != "building";
    });
}

std::shared_ptr<const Engine::Current> Engine::current_or_throw() const {
    std::shared_lock lock(_result_mtx);
    if (!_current) throw NoScanError();
    return _current;
}

json Engine::node(const std::string& path, int depth, size_t max_children,
                  double min_fraction) const {
    auto cur = current_or_throw();
    const DirNode* d = cur->result.root;
    if (!path.empty()) {
        auto it = cur->by_path.find(normalise_scan_path(path));
        if (it == cur->by_path.end()) throw NotFoundError("Folder not in this scan: " + path);
        d = it->second;
    }
    depth        = std::clamp(depth, 0, 6);
    max_children = std::clamp<size_t>(max_children, 1, 1000);
    min_fraction = std::clamp(min_fraction, 0.0, 0.5);
    auto min_size = uint64_t(double(d->size) * min_fraction);

    json j = dir_tree(d, depth, max_children, min_size);
    // Breadcrumb from the scan root down to this folder
    json crumbs = json::array();
    for (const DirNode* p = d; p; p = p->parent)
        crumbs.push_back({{"name", std::string(p->name)}, {"path", p->path}});
    std::reverse(crumbs.begin(), crumbs.end());
    j["breadcrumb"] = std::move(crumbs);
    return j;
}

json Engine::top_files(size_t n) const {
    auto cur = current_or_throw();
    json arr = json::array();
    for (const FileNode* f : cur->index.top_files(std::clamp<size_t>(n, 1, 1000)))
        arr.push_back(file_json(f));
    return arr;
}

json Engine::top_dirs(size_t n) const {
    auto cur = current_or_throw();
    json arr = json::array();
    for (const DirNode* d : cur->index.top_dirs(std::clamp<size_t>(n, 1, 1000)))
        arr.push_back(dir_summary(d));
    return arr;
}

json Engine::extensions(size_t n) const {
    auto cur = current_or_throw();
    json arr = json::array();
    size_t i = 0;
    for (const auto& e : cur->index.extensions()) {
        if (i++ >= std::clamp<size_t>(n, 1, 1000)) break;
        arr.push_back({{"ext", std::string(e.ext)}, {"size", e.total_bytes},
                       {"count", e.file_count}});
    }
    return arr;
}

json Engine::search(const std::string& query, size_t limit) const {
    auto cur = current_or_throw();
    std::string q = lower(query);
    limit = std::clamp<size_t>(limit, 1, 500);
    json files = json::array(), dirs = json::array();
    if (!q.empty()) {
        // Both lists are sorted by size, so the biggest matches come first
        for (const DirNode* d : cur->index.top_dirs(cur->index.total_dirs())) {
            if (dirs.size() >= limit) break;
            if (lower(d->name).find(q) != std::string::npos) dirs.push_back(dir_summary(d));
        }
        for (const FileNode* f : cur->index.top_files(cur->index.total_files())) {
            if (files.size() >= limit) break;
            if (lower(f->name).find(q) != std::string::npos) files.push_back(file_json(f));
        }
    }
    return {{"query", query}, {"dirs", std::move(dirs)}, {"files", std::move(files)}};
}

json Engine::list_snapshots() const {
    std::string current_key;
    {
        std::shared_lock lock(_result_mtx);
        if (_current) current_key = normalise_scan_path(_current->result.root->path);
    }
    json arr = json::array();
    for (const auto& i : _store->list()) {
        json j = info_json(i);
        j["matches_current"] = !current_key.empty() &&
                               normalise_scan_path(i.scan_path) == current_key;
        arr.push_back(std::move(j));
    }
    return arr;
}

json Engine::save_snapshot(const std::string& label) {
    auto cur = current_or_throw();
    int64_t id = _store->save(snapshot_from_scan(cur->result), label);
    auto info = _store->info(id);
    return info ? info_json(*info) : json{{"id", id}};
}

void Engine::delete_snapshot(int64_t id) {
    if (!_store->remove(id)) throw NotFoundError("No snapshot with id " + std::to_string(id));
}

json Engine::diff(int64_t snapshot_id, size_t limit) const {
    auto cur = current_or_throw();
    auto info = _store->info(snapshot_id);
    if (!info) throw NotFoundError("No snapshot with id " + std::to_string(snapshot_id));

    Snapshot before = _store->load(snapshot_id);
    Snapshot after  = snapshot_from_scan(cur->result);
    DiffResult d    = diff_snapshots(before, after);

    limit = std::clamp<size_t>(limit, 1, 2000);
    auto list = [limit](const std::vector<FileDelta>& v) {
        json arr = json::array();
        for (size_t i = 0; i < v.size() && i < limit; ++i) arr.push_back(delta_json(v[i]));
        return arr;
    };
    auto sum = [](const std::vector<FileDelta>& v) {
        int64_t s = 0;
        for (const auto& x : v) s += x.delta();
        return s;
    };
    return {
        {"snapshot", info_json(*info)},
        {"scan_path", after.scan_path},
        {"same_path", normalise_scan_path(before.scan_path) ==
                      normalise_scan_path(after.scan_path)},
        {"net_delta", d.net_delta},
        {"unchanged", d.unchanged},
        {"counts", {{"added", d.added.size()}, {"removed", d.removed.size()},
                    {"grown", d.grown.size()}, {"shrunk", d.shrunk.size()}}},
        {"bytes", {{"added", sum(d.added)}, {"removed", sum(d.removed)},
                   {"grown", sum(d.grown)}, {"shrunk", sum(d.shrunk)}}},
        {"dirs", list(d.dirs)},
        {"added", list(d.added)},
        {"removed", list(d.removed)},
        {"grown", list(d.grown)},
        {"shrunk", list(d.shrunk)},
    };
}

} // namespace disktree::service
