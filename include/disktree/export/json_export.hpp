#pragma once
#include "disktree/core/models.hpp"
#include "disktree/core/index.hpp"
#include <string>
#include <filesystem>

namespace disktree::export_ {

// Serialize full ScanResult to JSON string
std::string to_json(const ScanResult& result, const Index& index);

// Snapshots (for --snapshot / --diff) live in core/snapshot.hpp

} // namespace disktree::export_