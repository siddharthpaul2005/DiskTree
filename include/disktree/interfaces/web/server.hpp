#pragma once

#include "disktree/core/models.hpp"
#include "disktree/core/index.hpp"
#include <string>

namespace disktree::web {

// Entry point for web mode
// Starts HTTP server on localhost:7821
// Opens browser automatically
// Blocks until user closes browser or presses Ctrl+C
int run(const ScanResult& result, const Index& index, uint16_t port = 7821);

} // namespace disktree::web