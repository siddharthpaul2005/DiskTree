#pragma once

#include <cstdint>
#include <string>
#include <array>
#include <optional>
#include <string_view>

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

// ── format_signed_bytes ─────────────────────────────────────────
// Like format_bytes but with an explicit sign, for diffs.
//   format_signed_bytes(1536)  → "+1.50 KB"
//   format_signed_bytes(-1024) → "-1.00 KB"
//   format_signed_bytes(0)     → "0 B"
std::string format_signed_bytes(int64_t bytes);

// ── parse_size ──────────────────────────────────────────────────
// Parses a human size into bytes (binary units, case-insensitive,
// optional space and optional trailing "B" / "iB").
//   "500"    → 500        "1.5KB" → 1536      "2 mb" → 2097152
//   "1G"     → 1073741824 "3TiB"  → 3 * 2^40
// Returns std::nullopt for anything it can't parse.
std::optional<uint64_t> parse_size(std::string_view text);

} // namespace disktree::utils