#pragma once

#include "disktree/core/models.hpp"
#include "disktree/core/index.hpp"

namespace disktree::tui {

// Entry point for TUI mode
// Called from main.cpp when --tui flag is passed
int run(const ScanResult& result, const Index& index);

} // namespace disktree::tui