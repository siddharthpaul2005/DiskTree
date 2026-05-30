#include "disktree/core/scanner.hpp"

#include <filesystem>
#include <system_error>
#include <chrono>
#include <algorithm>
#include <stack>
#include <clocale>

namespace fs = std::filesystem;

namespace disktree {

static std::string path_to_str(const fs::path& path) {
    auto u8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

Scanner::Scanner(utils::ProgressTracker& progress)
    : _progress(progress)
{}

uint64_t Scanner::inode_key(uint64_t inode, uint32_t device) {
    return (uint64_t(device) << 32) | (inode & 0xFFFFFFFF);
}

bool Scanner::is_excluded(const fs::path& path, const Config& config) {
    try {
        std::string name = path_to_str(path.filename());
        for (const auto& pattern : config.exclude_patterns) {
            if (name == pattern) return true;
        }
    } catch (...) {}
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
        auto [path, node, depth] = stk.top();
        stk.pop();

        ctx.total_dirs.fetch_add(1, std::memory_order_relaxed);
        ctx.progress.add_dir();

        try {
            ctx.progress.set_current_path(path_to_str(path));
        } catch (...) {}

        ctx.progress.maybe_emit(5000);

        std::error_code ec;
        fs::directory_iterator iter(
            path, fs::directory_options::skip_permission_denied, ec);

        if (ec) {
            std::lock_guard<std::mutex> lock(ctx.errors_mtx);
            ctx.errors.push_back({path, SkipReason::Permission, ec.message()});
            node->scan_failed = true;
            continue;
        }

        for (const auto& entry : iter) {
            // Wrap each entry in try-catch — some filenames on Windows
            // have unencodable characters that would otherwise crash us
            try {
                if (is_excluded(entry.path(), ctx.config)) continue;

                std::error_code sec;
                fs::file_status status = entry.symlink_status(sec);
                if (sec) continue;

                if (fs::is_symlink(status)) {
                    if (!ctx.config.follow_symlinks) {
                        FileNode* f   = ctx.arena.alloc_file();
                        f->path       = path_to_str(entry.path());
                        size_t sl     = f->path.find_last_of("/\\");
                        f->name       = std::string_view(f->path).substr(
                                            sl == std::string::npos ? 0 : sl + 1);
                        f->extension  = ctx.arena.intern("");
                        f->size       = 0;
                        f->is_symlink = true;
                        node->child_files.push_back(f);
                        continue;
                    }
                    status = entry.status(sec);
                    if (sec) continue;
                }

                if (fs::is_directory(status)) {
                    if (ctx.config.max_depth > 0 && depth >= ctx.config.max_depth)
                        continue;

                    DirNode* child = ctx.arena.alloc_dir();
                    child->path    = path_to_str(entry.path());
                    size_t sl      = child->path.find_last_of("/\\");
                    child->name    = std::string_view(child->path).substr(
                                         sl == std::string::npos ? 0 : sl + 1);
                    child->depth   = depth + 1;
                    child->parent  = node;
                    node->child_dirs.push_back(child);

                    stk.push({entry.path(), child, depth + 1});

                } else if (fs::is_regular_file(status)) {
                    std::error_code fec;
                    uint64_t size = entry.file_size(fec);
                    if (fec) size = 0;
                    if (size < ctx.config.min_size) continue;

                    FileNode* f  = ctx.arena.alloc_file();
                    f->path      = path_to_str(entry.path());
                    size_t sl    = f->path.find_last_of("/\\");
                    f->name      = std::string_view(f->path).substr(
                                       sl == std::string::npos ? 0 : sl + 1);

                    size_t dot   = f->name.rfind('.');
                    std::string ext = (dot == std::string_view::npos)
                        ? "" : std::string(f->name.substr(dot));
                    std::transform(ext.begin(), ext.end(),
                                   ext.begin(), ::tolower);
                    f->extension  = ctx.arena.intern(ext);
                    f->size       = size;
                    f->is_symlink = false;

                    std::error_code mec;
                    auto mtime = entry.last_write_time(mec);
                    if (!mec) {
                        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                                mtime - fs::file_time_type::clock::now()
                                + std::chrono::system_clock::now());
                        f->modified = std::chrono::system_clock::to_time_t(sctp);
                    }

                    node->child_files.push_back(f);
                    ctx.total_files.fetch_add(1, std::memory_order_relaxed);
                    ctx.total_bytes.fetch_add(size, std::memory_order_relaxed);
                    ctx.progress.add_file(size);
                    ctx.progress.maybe_emit(5000);
                }

            } catch (...) {
                // Skip unencodable or otherwise problematic entries
                continue;
            }
        }
    }
}

ScanResult Scanner::scan(const fs::path& root, Config config) {
#ifdef _WIN32
    std::setlocale(LC_ALL, ".UTF-8");
#endif
    auto start = std::chrono::steady_clock::now();
    _progress.set_phase(ScanPhase::Scanning);

    auto arena         = std::make_unique<ScanArena>();
    DirNode* root_node = arena->alloc_dir();

    try {
        root_node->path = path_to_str(fs::absolute(root));
    } catch (...) {
        root_node->path = root.string();
    }

    size_t sl       = root_node->path.find_last_of("/\\");
    root_node->name = std::string_view(root_node->path).substr(
                          sl == std::string::npos ? 0 : sl + 1);
    root_node->depth  = 0;
    root_node->parent = nullptr;

    utils::ThreadPool pool(1);

    ScanContext ctx{
        .arena    = *arena,
        .config   = config,
        .progress = _progress,
        .pool     = pool,
    };

    scan_directory(fs::absolute(root), root_node, 0, ctx);

    auto end       = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(end - start).count();

    _progress.set_phase(ScanPhase::Building);

    ScanResult result;
    result.root        = root_node;
    result.scan_path   = fs::absolute(root);
    result.scanned_at  = std::chrono::system_clock::now();
    result.elapsed_sec = elapsed;
    result.total_size  = ctx.total_bytes.load();
    result.total_files = ctx.total_files.load();
    result.total_dirs  = ctx.total_dirs.load();
    result.skipped     = std::move(ctx.errors);
    result.platform    = "windows";
    result.arena       = std::move(arena);

    return result;
}

} // namespace disktree