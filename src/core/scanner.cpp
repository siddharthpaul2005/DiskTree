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

static int64_t to_unix_time(fs::file_time_type t) {
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

    while (!stk.empty()) {
        auto [path, node, depth] = std::move(stk.top());
        stk.pop();

        ctx.total_dirs.fetch_add(1, std::memory_order_relaxed);
        ctx.progress.add_dir();
        ctx.progress.set_current_path(node->path);

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
            // Wrap each entry: some Windows filenames contain characters
            // that can't be converted and would otherwise throw.
            try {
                const fs::directory_entry& entry = *it;
                std::string full = path_to_str(entry.path());
                std::string_view name = name_of(full);

                if (!is_excluded(name, ctx.config)) {
                    std::error_code sec;
                    fs::file_status status = entry.symlink_status(sec);
                    bool skip = static_cast<bool>(sec);

                    if (!skip && fs::is_symlink(status)) {
                        if (!ctx.config.follow_symlinks) {
                            FileNode* f   = ctx.arena.alloc_file();
                            f->path       = std::move(full);
                            f->name       = name_of(f->path);
                            f->extension  = ctx.arena.intern("");
                            f->is_symlink = true;
                            node->child_files.push_back(f);
                            skip = true;
                        } else {
                            status = entry.status(sec);
                            skip = static_cast<bool>(sec);
                        }
                    }

                    if (skip) {
                        // nothing
                    } else if (fs::is_directory(status)) {
                        bool too_deep = ctx.config.max_depth > 0 &&
                                        depth >= ctx.config.max_depth;
                        if (!too_deep) {
                            DirNode* child = ctx.arena.alloc_dir();
                            child->path    = std::move(full);
                            child->name    = name_of(child->path);
                            child->depth   = depth + 1;
                            child->parent  = node;
                            node->child_dirs.push_back(child);

                            if (child->depth <= PARALLEL_DEPTH_LIMIT &&
                                ctx.pool.thread_count() > 1) {
                                // Hand the whole subtree to another worker
                                fs::path child_path = entry.path();
                                uint32_t child_depth = child->depth;
                                ctx.pool.submit([this, child_path, child, child_depth, &ctx] {
                                    scan_directory(child_path, child, child_depth, ctx);
                                });
                            } else {
                                stk.push({entry.path(), child, depth + 1});
                            }
                        }
                    } else if (fs::is_regular_file(status)) {
                        std::error_code fec;
                        uint64_t size = entry.file_size(fec);
                        if (fec) size = 0;

                        if (size >= ctx.config.min_size) {
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

                            std::error_code mec;
                            auto mtime = entry.last_write_time(mec);
                            if (!mec) f->modified = to_unix_time(mtime);

                            node->child_files.push_back(f);
                            ctx.total_files.fetch_add(1, std::memory_order_relaxed);
                            ctx.total_bytes.fetch_add(size, std::memory_order_relaxed);
                            ctx.progress.add_file(size);
                        }
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
    uint64_t total_bytes = 0, total_files = 0, total_dirs = 0;
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
    result.skipped     = std::move(errors);
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
