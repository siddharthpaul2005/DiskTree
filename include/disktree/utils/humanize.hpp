#pragma once

#include <cstdint>
#include <string>
#include <array>

namespace disktree::utils {

// ── format_bytes ────────────────────────────────────────────────
// Converts a raw byte count into a human-readable string.
// Examples:
//   format_bytes(0)              → "0 B"
//   format_bytes(1023)           → "1023 B"
//   format_bytes(1024)           → "1.00 KB"
//   format_bytes(1536)           → "1.50 KB"
//   format_bytes(1073741824)     → "1.00 GB"
//   format_bytes(90455211008)    → "84.24 GB"
//
// Uses binary units (1 KB = 1024 B), same as WizTree and Windows Explorer.
// Returns a std::string — this is not on the hot path, only called for display.
std::string format_bytes(uint64_t bytes);

// ── format_count ────────────────────────────────────────────────
// Formats a file/dir count with thousand separators.
// Examples:
//   format_count(142847) → "142,847"
//   format_count(1000)   → "1,000"
//   format_count(42)     → "42"
std::string format_count(uint64_t count);

// ── format_duration ─────────────────────────────────────────────
// Formats elapsed seconds into a readable duration.
// Examples:
//   format_duration(0.31)  → "0.31s"
//   format_duration(2.31)  → "2.31s"
//   format_duration(61.0)  → "1m 1s"
std::string format_duration(double seconds);

// ── format_pct ──────────────────────────────────────────────────
// Formats a 0.0–1.0 fraction as a percentage string.
// Examples:
//   format_pct(0.489) → "48.9%"
//   format_pct(1.0)   → "100.0%"
//   format_pct(0.001) → "0.1%"
std::string format_pct(float fraction);

} // namespace disktree::utils