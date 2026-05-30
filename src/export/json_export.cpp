#include "disktree/export/json_export.hpp"
#include "disktree/utils/humanize.hpp"

#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <chrono>
#include <iomanip>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace disktree::export_ {

static json node_to_json(const DirNode* node, int depth_limit, int cur = 0) {
    json j;
    j["name"]       = std::string(node->name);
    j["path"]       = node->path;
    j["size"]       = node->size;
    j["file_count"] = node->file_count;
    j["dir_count"]  = node->dir_count;
    j["pct"]        = node->pct_of_parent;
    j["type"]       = "dir";

    if (depth_limit == 0 || cur < depth_limit) {
        json children = json::array();
        for (const auto* d : node->child_dirs)
            children.push_back(node_to_json(d, depth_limit, cur + 1));
        for (const auto* f : node->child_files) {
            json fj;
            fj["name"]     = std::string(f->name);
            fj["path"]     = f->path;
            fj["size"]     = f->size;
            fj["ext"]      = std::string(f->extension);
            fj["pct"]      = f->pct_of_parent;
            fj["modified"] = f->modified;
            fj["type"]     = "file";
            children.push_back(fj);
        }
        j["children"] = children;
    }
    return j;
}

std::string to_json(const ScanResult& result, const Index& index) {
    json j;
    j["scan_path"]   = result.scan_path.string();
    j["total_size"]  = result.total_size;
    j["total_files"] = result.total_files;
    j["total_dirs"]  = result.total_dirs;
    j["elapsed_sec"] = result.elapsed_sec;
    j["platform"]    = result.platform;

    // Extension stats
    json exts = json::array();
    for (const auto& e : index.extensions()) {
        json ej;
        ej["ext"]   = std::string(e.ext.empty() ? "(none)" : e.ext);
        ej["bytes"] = e.total_bytes;
        ej["count"] = e.file_count;
        exts.push_back(ej);
    }
    j["extensions"] = exts;

    // Top files
    json top_files = json::array();
    for (const auto* f : index.top_files(100)) {
        json fj;
        fj["path"] = f->path;
        fj["size"] = f->size;
        fj["ext"]  = std::string(f->extension);
        top_files.push_back(fj);
    }
    j["top_files"] = top_files;

    // Tree (depth 4)
    j["tree"] = node_to_json(result.root, 4);

    return j.dump(2);
}

fs::path save_snapshot(const ScanResult& result, const Index& index) {
    // Create snapshot directory
    auto home = fs::path(std::getenv("USERPROFILE") ?
                         std::getenv("USERPROFILE") :
                         std::getenv("HOME"));
    auto snap_dir = home / ".disktree" / "snapshots";
    fs::create_directories(snap_dir);

    // Filename: timestamp
    auto now = std::chrono::system_clock::now();
    auto t   = std::chrono::system_clock::to_time_t(now);
    std::ostringstream ss;
    ss << std::put_time(std::localtime(&t), "%Y%m%d_%H%M%S");
    auto snap_path = snap_dir / (ss.str() + ".json");

    std::ofstream f(snap_path);
    f << to_json(result, index);
    return snap_path;
}

std::string load_snapshot(const fs::path& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("Cannot open snapshot: " + path.string());
    return std::string(std::istreambuf_iterator<char>(f),
                       std::istreambuf_iterator<char>());
}

} // namespace disktree::export_