#include "disktree/core/snapshot_store.hpp"

#include <sqlite3.h>

#include <cstdlib>
#include <stdexcept>

namespace fs = std::filesystem;

namespace disktree {

namespace {

// RAII wrapper for a prepared statement
class Stmt {
public:
    Stmt(sqlite3* db, const char* sql) : _db(db) {
        if (sqlite3_prepare_v2(db, sql, -1, &_s, nullptr) != SQLITE_OK)
            throw std::runtime_error(std::string("SQLite prepare failed: ") + sqlite3_errmsg(db));
    }
    ~Stmt() { sqlite3_finalize(_s); }
    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;

    Stmt& bind(int i, int64_t v)          { check(sqlite3_bind_int64(_s, i, v)); return *this; }
    Stmt& bind(int i, std::string_view v) {
        check(sqlite3_bind_text(_s, i, v.data(), int(v.size()), SQLITE_TRANSIENT));
        return *this;
    }
    Stmt& bind_blob(int i, std::string_view v) {
        check(sqlite3_bind_blob(_s, i, v.data(), int(v.size()), SQLITE_TRANSIENT));
        return *this;
    }

    // true = a row is available
    bool step() {
        int rc = sqlite3_step(_s);
        if (rc == SQLITE_ROW)  return true;
        if (rc == SQLITE_DONE) return false;
        throw std::runtime_error(std::string("SQLite step failed: ") + sqlite3_errmsg(_db));
    }

    int64_t     i64(int c) const { return sqlite3_column_int64(_s, c); }
    std::string text(int c) const {
        auto p = reinterpret_cast<const char*>(sqlite3_column_text(_s, c));
        return p ? std::string(p, size_t(sqlite3_column_bytes(_s, c))) : std::string();
    }
    std::string blob(int c) const {
        auto p = static_cast<const char*>(sqlite3_column_blob(_s, c));
        return p ? std::string(p, size_t(sqlite3_column_bytes(_s, c))) : std::string();
    }

private:
    void check(int rc) {
        if (rc != SQLITE_OK)
            throw std::runtime_error(std::string("SQLite bind failed: ") + sqlite3_errmsg(_db));
    }
    sqlite3*      _db;
    sqlite3_stmt* _s = nullptr;
};

std::string path_to_str(const fs::path& p) {
    auto u8 = p.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

fs::path str_to_path(const char* s) {
    std::string str(s);
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(str.data()), str.size()));
}

constexpr const char* INFO_COLUMNS =
    "id, scan_path, label, created_at, total_size, total_files, total_dirs";

SnapshotInfo read_info(const Stmt& st) {
    SnapshotInfo i;
    i.id          = st.i64(0);
    i.scan_path   = st.text(1);
    i.label       = st.text(2);
    i.created_at  = st.i64(3);
    i.total_size  = uint64_t(st.i64(4));
    i.total_files = uint64_t(st.i64(5));
    i.total_dirs  = uint64_t(st.i64(6));
    return i;
}

} // namespace

fs::path SnapshotStore::default_path() {
    const char* base = std::getenv("LOCALAPPDATA");
    if (base && *base) return str_to_path(base) / "DiskTree" / "disktree.db";
    const char* home = std::getenv("HOME");
    if (home && *home) return str_to_path(home) / ".local" / "share" / "disktree" / "disktree.db";
    return fs::current_path() / "disktree.db";
}

SnapshotStore::SnapshotStore(const fs::path& db_path) : _path(db_path) {
    std::error_code ec;
    if (db_path.has_parent_path()) fs::create_directories(db_path.parent_path(), ec);

    if (sqlite3_open_v2(path_to_str(db_path).c_str(), &_db,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                        nullptr) != SQLITE_OK) {
        std::string msg = _db ? sqlite3_errmsg(_db) : "out of memory";
        sqlite3_close(_db);
        _db = nullptr;
        throw std::runtime_error("Cannot open snapshot database " + path_to_str(db_path) +
                                 ": " + msg);
    }
    sqlite3_busy_timeout(_db, 5000);   // CLI and app may share the file
    exec("PRAGMA journal_mode=WAL");
    exec("PRAGMA foreign_keys=ON");
    exec(R"SQL(
        CREATE TABLE IF NOT EXISTS snapshots (
            id            INTEGER PRIMARY KEY AUTOINCREMENT,
            scan_path     TEXT    NOT NULL,
            scan_path_key TEXT    NOT NULL,
            label         TEXT    NOT NULL DEFAULT '',
            created_at    INTEGER NOT NULL,
            total_size    INTEGER NOT NULL,
            total_files   INTEGER NOT NULL,
            total_dirs    INTEGER NOT NULL,
            data          BLOB    NOT NULL
        );
        CREATE INDEX IF NOT EXISTS snapshots_by_path
            ON snapshots(scan_path_key, created_at DESC);
        PRAGMA user_version = 1;
    )SQL");
}

SnapshotStore::~SnapshotStore() {
    if (_db) sqlite3_close(_db);
}

void SnapshotStore::exec(const char* sql) {
    char* err = nullptr;
    if (sqlite3_exec(_db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = err ? err : "unknown error";
        sqlite3_free(err);
        throw std::runtime_error("SQLite error: " + msg);
    }
}

int64_t SnapshotStore::save(const Snapshot& snap, std::string_view label) {
    std::string blob = zstd_compress(snapshot_to_json(snap));   // outside the lock
    std::lock_guard lock(_mtx);
    Stmt st(_db, "INSERT INTO snapshots (scan_path, scan_path_key, label, created_at, "
                 "total_size, total_files, total_dirs, data) VALUES (?,?,?,?,?,?,?,?)");
    st.bind(1, snap.scan_path)
      .bind(2, normalise_scan_path(snap.scan_path))
      .bind(3, label)
      .bind(4, snap.created_at)
      .bind(5, int64_t(snap.total_size))
      .bind(6, int64_t(snap.total_files))
      .bind(7, int64_t(snap.total_dirs))
      .bind_blob(8, blob);
    st.step();
    return sqlite3_last_insert_rowid(_db);
}

std::vector<SnapshotInfo> SnapshotStore::list(std::string_view scan_path) const {
    std::lock_guard lock(_mtx);
    std::vector<SnapshotInfo> out;
    if (scan_path.empty()) {
        Stmt st(_db, (std::string("SELECT ") + INFO_COLUMNS +
                      " FROM snapshots ORDER BY created_at DESC, id DESC").c_str());
        while (st.step()) out.push_back(read_info(st));
    } else {
        Stmt st(_db, (std::string("SELECT ") + INFO_COLUMNS +
                      " FROM snapshots WHERE scan_path_key = ? "
                      "ORDER BY created_at DESC, id DESC").c_str());
        st.bind(1, normalise_scan_path(scan_path));
        while (st.step()) out.push_back(read_info(st));
    }
    return out;
}

std::optional<SnapshotInfo> SnapshotStore::info(int64_t id) const {
    std::lock_guard lock(_mtx);
    Stmt st(_db, (std::string("SELECT ") + INFO_COLUMNS +
                  " FROM snapshots WHERE id = ?").c_str());
    st.bind(1, id);
    if (!st.step()) return std::nullopt;
    return read_info(st);
}

Snapshot SnapshotStore::load(int64_t id) const {
    std::string blob;
    {
        std::lock_guard lock(_mtx);
        Stmt st(_db, "SELECT data FROM snapshots WHERE id = ?");
        st.bind(1, id);
        if (!st.step()) throw std::runtime_error("No snapshot with id " + std::to_string(id));
        blob = st.blob(0);
    }
    std::string json = is_zstd(blob) ? zstd_decompress(blob) : blob;
    return snapshot_from_json(json, "snapshot #" + std::to_string(id));
}

std::optional<int64_t> SnapshotStore::latest_for(std::string_view scan_path) const {
    std::lock_guard lock(_mtx);
    Stmt st(_db, "SELECT id FROM snapshots WHERE scan_path_key = ? "
                 "ORDER BY created_at DESC, id DESC LIMIT 1");
    st.bind(1, normalise_scan_path(scan_path));
    if (!st.step()) return std::nullopt;
    return st.i64(0);
}

bool SnapshotStore::remove(int64_t id) {
    std::lock_guard lock(_mtx);
    Stmt st(_db, "DELETE FROM snapshots WHERE id = ?");
    st.bind(1, id);
    st.step();
    return sqlite3_changes(_db) > 0;
}

} // namespace disktree
