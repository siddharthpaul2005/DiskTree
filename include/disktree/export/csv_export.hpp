#pragma once
#include "disktree/core/models.hpp"
#include "disktree/core/index.hpp"
#include <string>

namespace disktree::export_ {

// Returns CSV string: path,size,type,extension,modified
std::string to_csv(const ScanResult& result, const Index& index);

} // namespace disktree::export_