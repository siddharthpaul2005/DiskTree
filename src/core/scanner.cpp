#include "disktree/core/scanner.hpp"

#include <filesystem>
#include <system_error>
#include <chrono>
#include <algorithm>

namespace fs = std::filesystem;

namespace disktree {

Scanner::Scanner(utils::ProgressTracker& progress)
    : _progress(progress)
{}

uint64_t Scanner::inode_key(uint64_t inode, uint32_t device) {
    return (uint64_t(device) << 32) | (inode & 0xFFFFFFFF);
}

bool Scanner::is_excluded(const fs::path& path, const Config& config) {
    std::string name = path.filename().string();
    for (const auto& pattern : config.exclude_patterns) {
        if (name == pattern) return true;
    }
    return false;
}

void Scanner::scan_directory(const fs::path& path,
                              DirNode*        node,
                              uint32_t        depth,
                              ScanContext&    ctx)
{
    ctx.total_dirs.fetch_add(1, std::memory_order_relaxed);
    ctx.progress.add_dir();
    ctx.progress.set_current_path(path.string());
    ctx.progress.maybe_emit(5000);

    std::error_code ec;
    fs::directory_iterator iter(
        path, fs::directory_options::skip_permission_denied, ec);

    if (ec) {
        std::lock_guard<std::mutex> lock(ctx.errors_mtx);
        ctx.errors.push_back({path, SkipReason::Permission, ec.message()});
        node->scan_failed = true;
        return;
    }

    std::vector<fs::path>  subdirs;
    std::vector<DirNode*>  subdir_nodes;

    for (const auto& entry : iter) {
        if (is_excluded(entry.path(), ctx.config)) continue;

        std::error_code sec;
        fs::file_status status = entry.symlink_status(sec);
        if (sec) continue;

        if (fs::is_symlink(status)) {
            if (!ctx.config.follow_symlinks) {
                FileNode* f  = ctx.arena.alloc_file();
                f->path      = entry.path().string();
                size_t sl    = f->path.find_last_of("/\\");
                f->name      = std::string_view(f->path).substr(
                                   sl == std::string::npos ? 0 : sl + 1);
                f->extension = ctx.arena.intern("");
                f->size      = 0;
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
            child->path    = entry.path().string();
            size_t sl      = child->path.find_last_of("/\\");
            child->name    = std::string_view(child->path).substr(
                                 sl == std::string::npos ? 0 : sl + 1);
            child->depth   = depth + 1;
            child->parent  = node;
            node->child_dirs.push_back(child);

            subdirs.push_back(entry.path());
            subdir_nodes.push_back(child);

        } else if (fs::is_regular_file(status)) {
            std::error_code fec;
            uint64_t size = entry.file_size(fec);
            if (fec) size = 0;

            if (size < ctx.config.min_size) continue;

            FileNode* f  = ctx.arena.alloc_file();
            f->path      = entry.path().string();
            size_t sl    = f->path.find_last_of("/\\");
            f->name      = std::string_view(f->path).substr(
                               sl == std::string::npos ? 0 : sl + 1);

            size_t dot   = f->name.rfind('.');
            std::string ext = (dot == std::string_view::npos)
                ? "" : std::string(f->name.substr(dot));
            std::transform(ext.begin(), ext.end(),
                           ext.begin(), ::tolower);
            f->extension = ctx.arena.intern(ext);
            f->size      = size;
            f->is_symlink = false;

            // Modification time
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
    }

    // Dispatch subdirs to thread pool at shallow depths only
    if (depth < PARALLEL_DEPTH_LIMIT && subdirs.size() > 1) {
        std::vector<std::future<void>> futures;
        futures.reserve(subdirs.size());

        for (size_t i = 0; i < subdirs.size(); ++i) {
            futures.push_back(ctx.pool.submit(
                [this, path_i = subdirs[i],
                       node_i = subdir_nodes[i],
                       depth, &ctx]() mutable {
                    scan_directory(path_i, node_i, depth + 1, ctx);
                }
            ));
        }

        for (auto& f : futures) {
            try { f.get(); }
            catch (...) {}   // errors already recorded in ctx.errors
        }

    } else {
        for (size_t i = 0; i < subdirs.size(); ++i)
            scan_directory(subdirs[i], subdir_nodes[i], depth + 1, ctx);
    }
}

ScanResult Scanner::scan(const fs::path& root,
                         Scanner::Config config) {
    auto start = std::chrono::steady_clock::now();
    _progress.set_phase(ScanPhase::Scanning);

    auto arena      = std::make_unique<ScanArena>();
    DirNode* root_node = arena->alloc_dir();
    root_node->path    = fs::absolute(root).string();
    size_t sl          = root_node->path.find_last_of("/\\");
    root_node->name    = std::string_view(root_node->path).substr(
                             sl == std::string::npos ? 0 : sl + 1);
    root_node->depth   = 0;
    root_node->parent  = nullptr;

    // Single thread pool for the entire scan
    size_t n_threads = config.n_threads > 0 ? config.n_threads : 8;
    utils::ThreadPool pool(n_threads);

    ScanContext ctx{
        *arena,
        config,
        _progress,
        pool,
    };

    scan_directory(fs::absolute(root), root_node, 0, ctx);

    // Wait for all dispatched tasks to finish before reading results
    pool.wait_all();

    auto end = std::chrono::steady_clock::now();
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