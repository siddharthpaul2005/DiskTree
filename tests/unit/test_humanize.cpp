#include <catch2/catch_test_macros.hpp>
#include "disktree/utils/humanize.hpp"

using namespace disktree::utils;

// ── format_bytes ────────────────────────────────────────────────

TEST_CASE("format_bytes: zero", "[humanize]") {
    REQUIRE(format_bytes(0) == "0 B");
}

TEST_CASE("format_bytes: under 1KB stays in bytes", "[humanize]") {
    REQUIRE(format_bytes(1)    == "1 B");
    REQUIRE(format_bytes(1023) == "1023 B");
}

TEST_CASE("format_bytes: exact KB boundary", "[humanize]") {
    REQUIRE(format_bytes(1024) == "1.00 KB");
}

TEST_CASE("format_bytes: fractional KB", "[humanize]") {
    REQUIRE(format_bytes(1536) == "1.50 KB");
}

TEST_CASE("format_bytes: megabytes", "[humanize]") {
    REQUIRE(format_bytes(1024 * 1024)       == "1.00 MB");
    REQUIRE(format_bytes(1024 * 1024 * 2)   == "2.00 MB");
}

TEST_CASE("format_bytes: gigabytes", "[humanize]") {
    REQUIRE(format_bytes(1073741824ULL)      == "1.00 GB");
    // 90455211008 bytes = 84.24 GB (this is a real scan size)
    REQUIRE(format_bytes(90455211008ULL)     == "84.24 GB");
}

TEST_CASE("format_bytes: terabytes", "[humanize]") {
    REQUIRE(format_bytes(1099511627776ULL)   == "1.00 TB");
}

// ── format_count ────────────────────────────────────────────────

TEST_CASE("format_count: small numbers no commas", "[humanize]") {
    REQUIRE(format_count(0)   == "0");
    REQUIRE(format_count(42)  == "42");
    REQUIRE(format_count(999) == "999");
}

TEST_CASE("format_count: thousands", "[humanize]") {
    REQUIRE(format_count(1000)    == "1,000");
    REQUIRE(format_count(142847)  == "142,847");
    REQUIRE(format_count(1000000) == "1,000,000");
}

// ── format_duration ─────────────────────────────────────────────

TEST_CASE("format_duration: sub-minute", "[humanize]") {
    REQUIRE(format_duration(0.31) == "0.31s");
    REQUIRE(format_duration(2.31) == "2.31s");
    REQUIRE(format_duration(59.9) == "59.90s");
}

TEST_CASE("format_duration: minutes", "[humanize]") {
    REQUIRE(format_duration(60.0)  == "1m 0s");
    REQUIRE(format_duration(61.0)  == "1m 1s");
    REQUIRE(format_duration(125.0) == "2m 5s");
}

// ── format_pct ──────────────────────────────────────────────────

TEST_CASE("format_pct: normal values", "[humanize]") {
    REQUIRE(format_pct(1.0f)   == "100.0%");
    REQUIRE(format_pct(0.0f)   == "0.0%");
    REQUIRE(format_pct(0.489f) == "48.9%");
}

TEST_CASE("format_pct: clamps out-of-range", "[humanize]") {
    REQUIRE(format_pct(-0.1f) == "0.0%");
    REQUIRE(format_pct(1.01f) == "100.0%");
}