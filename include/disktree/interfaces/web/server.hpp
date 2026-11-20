#pragma once

#include "disktree/core/models.hpp"
#include "disktree/core/index.hpp"
#include <string>

namespace disktree::web {

// Entry point for web mode.
// Serves the UI on http://localhost:<port> (127.0.0.1 only). If the port
// is taken, the next free one of the following 19 is used. Opens the
// default browser unless open_browser is false. Blocks until Ctrl+C.
// Returns non-zero if no port could be bound.
int run(const ScanResult& result, const Index& index,
        uint16_t port = 7821, bool open_browser = true);

} // namespace disktree::web