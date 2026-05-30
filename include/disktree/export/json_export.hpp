#pragma once
#include "disktree/core/models.hpp"
#include "disktree/core/index.hpp"
#include <string>
#include <filesystem>

namespace disktree::export_ {

// Serialize full ScanResult to JSON string
std::string to_json(const ScanResult& result, const Index& index);

// Save snapshot to ~/.disktree/snapshots/
std::filesystem::path save_snapshot(const ScanResult& result,
                                    const Index& index);

// Load snapshot from file, returns JSON string
std::string load_snapshot(const std::filesystem::path& path);

} // namespace disktree::export_