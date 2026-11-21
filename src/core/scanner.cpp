#include "disktree/core/scanner.hpp"

#include <filesystem>
#include <system_error>
#include <chrono>
#include <algorithm>
#include <stack>
#include <stdexcept>
#include <thread>
#include <clocale>
#include <cctype>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace disktree {

static std::string path_to_str(const fs::path& path) {
    auto u8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

static std::string_view name_of(const std::string& full_path) {
    size_t sl = full_path.find_last_of("/\\");
    return std::string_view(full_path).substr(sl == std::string::npos ? 0 : sl + 1);
}

[[maybe_unused]] static int64_t to_unix_time(fs::file_time_type t) {
    auto sys = std::chrono::clock_cast<std::chrono::system_clock>(t);
    return std::chrono::duration_cast<std::chrono::seconds>(
               sys.time_since_epoch()).count();
}

Scanner::Scanner(utils::ProgressTracker& progress)
    : _progress(progress)
{}

std::vector<std::string> Scanner::default_excludes() {
    return {"$RECYCLE.BIN", "System Volume Information", ".git", "node_modules"};
}

bool Scanner::matches_pattern(std::string_view pattern, std::string_view name) {
    // Iterative glob match with single-star backtracking: O(n*m) worst case.
    auto lower = [](char c) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    };
    size_t p = 0, n = 0;
    size_t star = std::string_view::npos, mark = 0;
    while (n < name.size()) {
        if (p < pattern.size() &&
            (pattern[p] == '?' || lower(pattern[p]) == lower(name[n]))) {
            ++p; ++n;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            mark = n;
        } else if (star != std::string_view::npos) {
            p = star + 1;
            n = ++mark;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

bool Scanner::is_excluded(std::string_view name, const Config& config) {
    for (const auto& pattern : config.exclude_patterns)
        if (matches_pattern(pattern, name)) return true;
    return false;
}

#ifdef _WIN32
namespace {

// One directory entry as Windows reports it, before any filtering
struct RawEntry {
    std::wstring name;
    DWORD        attrs       = 0;
    ULONG        reparse_tag = 0;
    uint64_t     size        = 0;   // logical length
    uint64_t     allocated   = 0;   // bytes taken on disk
    int64_t      modified    = 0;   // unix seconds
    uint64_t     file_id     = 0;   // 0 = unknown
};

// Paths near MAX_PATH need the \\?\ prefix to open
std::wstring long_path(const std::wstring& p) {
    if (p.size() < 240 || p.rfind(L"\\\\?\\", 0) == 0) return p;
    if (p.rfind(L"\\\\", 0) == 0) return L"\\\\?\\UNC\\" + p.substr(2);
    return L"\\\\?\\" + p;
}

int64_t filetime_to_unix(const LARGE_INTEGER& t) {
    if (t.QuadPart <= 0) return 0;
    return t.QuadPart / 10'000'000 - 11'644'473'600LL;
}

uint64_t fold_id(const unsigned char (&id)[16]) {
    uint64_t lo = 0, hi = 0;
    std::memcpy(&lo, id, 8);
    std::memcpy(&hi, id + 8, 8);
    return lo ^ (hi * 0x9E3779B97F4A7C15ull);   // NTFS ids fit in `lo`
}

// Lists a directory through one handle. Unlike FindFirstFile this also
// returns each entry's allocated size and file id, without opening the
// files. Returns 0 or a Win32 error code.
DWORD list_directory(const fs::path& dir, std::vector<RawEntry>& out) {
    out.clear();
    HANDLE h = CreateFileW(long_path(dir.native()).c_str(), FILE_LIST_DIRECTORY,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) return GetLastError();

    thread_local std::vector<uint64_t> buf(64 * 1024 / sizeof(uint64_t));
    const DWORD buf_bytes = DWORD(buf.size() * sizeof(uint64_t));

    // FileIdExtdDirectoryInfo carries the reparse tag and a 128-bit id;
    // file systems without it (FAT, some network shares) get the older class.
    bool extd  = true;
    bool first = true;
    DWORD err  = 0;
    for (;;) {
        BOOL ok = GetFileInformationByHandleEx(
            h, extd ? FileIdExtdDirectoryInfo : FileFullDirectoryInfo, buf.data(), buf_bytes);
        if (!ok) {
            err = GetLastError();
            if (err == ERROR_NO_MORE_FILES) { err = 0; break; }
            if (first && extd && (err == ERROR_INVALID_PARAMETER ||
                                  err == ERROR_NOT_SUPPORTED ||
                                  err == ERROR_INVALID_FUNCTION)) {
                extd = false;
                continue;
            }
            break;
        }
        first = false;

        auto* p = reinterpret_cast<const unsigned char*>(buf.data());
        for (;;) {
            RawEntry e;
            ULONG next = 0;
            if (extd) {
                auto* d = reinterpret_cast<const FILE_ID_EXTD_DIR_INFO*>(p);
                e.name.assign(d->FileName, d->FileNameLength / sizeof(WCHAR));
                e.attrs       = d->FileAttributes;
                e.reparse_tag = d->ReparsePointTag;
                e.size        = uint64_t(d->EndOfFile.QuadPart);
                e.allocated   = uint64_t(d->AllocationSize.QuadPart);
                e.modified    = filetime_to_unix(d->LastWriteTime);
                e.file_id     = fold_id(d->FileId.Identifier);
                next          = d->NextEntryOffset;
            } else {
                auto* d = reinterpret_cast<const FILE_FULL_DIR_INFO*>(p);
                e.name.assign(d->FileName, d->FileNameLength / sizeof(WCHAR));
                e.attrs = d->FileAttributes;
                // For reparse points this field holds the tag (as FindFirstFile's dwReserved0)
                e.reparse_tag = (e.attrs & FILE_ATTRIBUTE_REPARSE_POINT) ? d->EaSize : 0;
                e.size        = uint64_t(d->EndOfFile.QuadPart);
                e.allocated   = uint64_t(d->AllocationSize.QuadPart);
                e.modified    = filetime_to_unix(d->LastWriteTime);
                next          = d->NextEntryOffset;
            }
            if (e.name != L"." && e.name != L"..") out.push_back(std::move(e));
            if (next == 0) break;
            p += next;
        }
    }
    CloseHandle(h);
    return err;
}

} // namespace
#endif

void Scanner::scan_directory(const fs::path& root_path,
                             DirNode*        root_node,
                             uint32_t        root_depth,
                             ScanContext&    ctx)
{
    struct Frame {
        fs::path  path;
        DirNode*  node;
        uint32_t  depth;
    };

    std::stack<Frame> stk;
    stk.push({root_path, root_node, root_depth});

    auto cancelled = [&ctx] {
        return ctx.config.cancel && ctx.config.cancel->load(std::memory_order_relaxed);
    };

    // A symlink or junction that isn't followed: listed, never sized
    auto add_link = [&ctx](DirNode* node, std::string full) {
        FileNode* f   = ctx.arena.alloc_file();
        f->path       = std::move(full);
        f->name       = name_of(f->path);
        f->extension  = ctx.arena.intern("");
        f->is_symlink = true;
        node->child_files.push_back(f);
    };

    auto add_dir = [this, &ctx, &stk](DirNode* node, std::string full,
                                      const fs::path& child_path, uint32_t depth) {
        if (ctx.config.max_depth > 0 && depth >= ctx.config.max_depth) return;
        DirNode* child = ctx.arena.alloc_dir();
        child->path    = std::move(full);
        child->name    = name_of(child->path);
        child->depth   = depth + 1;
        child->parent  = node;
        node->child_dirs.push_back(child);

        if (child->depth <= PARALLEL_DEPTH_LIMIT && ctx.pool.thread_count() > 1) {
            // Hand the whole subtree to another worker
            uint32_t child_depth = child->depth;
            ctx.pool.submit([this, child_path, child, child_depth, &ctx] {
                scan_directory(child_path, child, child_depth, ctx);
            });
        } else {
            stk.push({child_path, child, depth + 1});
        }
    };

    // `file_id` 0 = unknown: then the file is never treated as a hard link
    auto add_file = [&ctx](DirNode* node, std::string full, uint64_t size,
                           uint64_t allocated, int64_t modified, uint64_t file_id) {
        if (size < ctx.config.min_size) return;
        FileNode* f = ctx.arena.alloc_file();
        f->path     = std::move(full);
        f->name     = name_of(f->path);

        size_t dot = f->name.rfind('.');
        std::string ext = (dot == std::string_view::npos || dot == 0)
            ? "" : std::string(f->name.substr(dot));
        std::transform(ext.begin(), ext.end(), ext.begin(),
            [](unsigned char c) { return char(std::tolower(c)); });
        f->extension = ctx.arena.intern(ext);
        f->size      = size;
        f->modified  = modified;
        f->inode     = file_id;

        // A second name for a file already counted takes no extra space
        if (file_id != 0 && !ctx.first_sighting(file_id)) f->is_hardlink = true;
        else ctx.total_allocated.fetch_add(allocated, std::memory_order_relaxed);

        node->child_files.push_back(f);
        ctx.total_files.fetch_add(1, std::memory_order_relaxed);
        ctx.total_bytes.fetch_add(size, std::memory_order_relaxed);
        ctx.progress.add_file(size);
    };

#ifdef _WIN32
    std::vector<RawEntry> entries;
#endif

    while (!stk.empty()) {
        if (cancelled()) return;
        auto [path, node, depth] = std::move(stk.top());
        stk.pop();

        ctx.total_dirs.fetch_add(1, std::memory_order_relaxed);
        ctx.progress.add_dir();
        ctx.progress.set_current_path(node->path);

#ifdef _WIN32
        if (DWORD err = list_directory(path, entries)) {
            std::lock_guard<std::mutex> lock(ctx.errors_mtx);
            bool denied = err == ERROR_ACCESS_DENIED || err == ERROR_PRIVILEGE_NOT_HELD;
            ctx.errors.push_back({path, denied ? SkipReason::Permission : SkipReason::IOError,
                                  std::system_category().message(int(err))});
            node->scan_failed = true;
            continue;
        }
        for (const RawEntry& e : entries) {
            if (cancelled()) return;
            // Wrap each entry: some Windows filenames contain characters
            // that can't be converted and would otherwise throw.
            try {
                fs::path child_path = path / e.name;
                std::string full = path_to_str(child_path);
                if (is_excluded(name_of(full), ctx.config)) continue;

                bool is_dir  = (e.attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
                bool is_link = (e.attrs & FILE_ATTRIBUTE_REPARSE_POINT) &&
                               (e.reparse_tag == IO_REPARSE_TAG_SYMLINK ||
                                e.reparse_tag == IO_REPARSE_TAG_MOUNT_POINT);
                if (is_link && !ctx.config.follow_symlinks) add_link(node, std::move(full));
                else if (is_dir) add_dir(node, std::move(full), child_path, depth);
                else add_file(node, std::move(full), e.size, e.allocated, e.modified, e.file_id);
            } catch (...) {
                // Skip unencodable or otherwise problematic entries
            }
        }
#else
        std::error_code ec;
        fs::directory_iterator iter(
            path, fs::directory_options::skip_permission_denied, ec);

        if (ec) {
            std::lock_guard<std::mutex> lock(ctx.errors_mtx);
            ctx.errors.push_back({path, SkipReason::Permission, ec.message()});
            node->scan_failed = true;
            continue;
        }

        for (auto it = fs::begin(iter); it != fs::end(iter); ) {
            if (cancelled()) return;
            try {
                const fs::directory_entry& entry = *it;
                std::string full = path_to_str(entry.path());

                if (!is_excluded(name_of(full), ctx.config)) {
                    std::error_code sec;
                    fs::file_status status = entry.symlink_status(sec);
                    bool skip = static_cast<bool>(sec);

                    if (!skip && fs::is_symlink(status)) {
                        if (!ctx.config.follow_symlinks) {
                            add_link(node, std::move(full));
                            skip = true;
                        } else {
                            status = entry.status(sec);
                            skip = static_cast<bool>(sec);
                        }
                    }

                    if (skip) {
                        // nothing
                    } else if (fs::is_directory(status)) {
                        add_dir(node, std::move(full), entry.path(), depth);
                    } else if (fs::is_regular_file(status)) {
                        std::error_code fec;
                        uint64_t size = entry.file_size(fec);
                        if (fec) size = 0;
                        int64_t modified = 0;
                        std::error_code mec;
                        auto mtime = entry.last_write_time(mec);
                        if (!mec) modified = to_unix_time(mtime);
                        add_file(node, std::move(full), size, size, modified, 0);
                    }
                }
            } catch (...) {
                // Skip unencodable or otherwise problematic entries
            }

            std::error_code inc_ec;
            it.increment(inc_ec);
            if (inc_ec) {
                std::lock_guard<std::mutex> lock(ctx.errors_mtx);
                ctx.errors.push_back({path, SkipReason::IOError, inc_ec.message()});
                break;
            }
        }
#endif
    }
}

ScanResult Scanner::scan(const fs::path& root, Config config) {
#ifdef _WIN32
    std::setlocale(LC_ALL, ".UTF-8");
#endif
    std::error_code ec;
    fs::path abs_root = fs::absolute(root, ec);
    if (ec) abs_root = root;
    if (!fs::exists(abs_root, ec))
        throw std::runtime_error("Path does not exist: " + path_to_str(abs_root));
    if (!fs::is_directory(abs_root, ec))
        throw std::runtime_error("Not a directory: " + path_to_str(abs_root));

    auto start = std::chrono::steady_clock::now();
    _progress.set_phase(ScanPhase::Scanning);

    auto arena         = std::make_unique<ScanArena>();
    DirNode* root_node = arena->alloc_dir();

    root_node->path = path_to_str(abs_root);
    // "D:\" has an empty filename; show the drive instead
    root_node->name = name_of(root_node->path);
    if (root_node->name.empty()) root_node->name = root_node->path;
    root_node->depth  = 0;
    root_node->parent = nullptr;

    size_t n_threads = config.n_threads;
    if (n_threads == 0) n_threads = std::max(1u, std::thread::hardware_concurrency());

    ScanResult result;
    std::vector<ScanError> errors;
    uint64_t total_bytes = 0, total_files = 0, total_dirs = 0, total_allocated = 0;
    {
        utils::ThreadPool pool(n_threads);
        ScanContext ctx{
            .arena    = *arena,
            .config   = config,
            .progress = _progress,
            .pool     = pool,
        };

        pool.submit([this, abs_root, root_node, &ctx] {
            scan_directory(abs_root, root_node, 0, ctx);
        });
        pool.wait_all();   // tasks spawn tasks; returns when all are done

        errors      = std::move(ctx.errors);
        total_bytes = ctx.total_bytes.load();
        total_files = ctx.total_files.load();
        total_dirs  = ctx.total_dirs.load();
        total_allocated = ctx.total_allocated.load();
    }

    auto end       = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(end - start).count();

    _progress.set_phase(ScanPhase::Building);

    result.root        = root_node;
    result.scan_path   = abs_root;
    result.scanned_at  = std::chrono::system_clock::now();
    result.elapsed_sec = elapsed;
    result.total_size  = total_bytes;
    result.total_files = total_files;
    result.total_dirs  = total_dirs;
    result.total_allocated = total_allocated;
    result.skipped     = std::move(errors);
    result.cancelled   = config.cancel && config.cancel->load();
#ifdef _WIN32
    result.platform    = "windows";
#elif __APPLE__
    result.platform    = "macos";
#else
    result.platform    = "linux";
#endif
    result.arena       = std::move(arena);

    return result;
}

} // namespace disktree
