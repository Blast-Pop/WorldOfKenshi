#include <doctest/doctest.h>

#include <core/sigscan.hpp>

#include <string>

using namespace wok;

namespace {
const uint8_t kBuf[] = {0x90, 0x90, 0x48, 0x8B, 0x05, 0x10, 0x00, 0x00, 0x00, 0x48, 0x85, 0xC0, 0xC3};
}

TEST_CASE("scan_finds_exact_pattern") {
    auto m = scan(kBuf, "48 85 C0");
    REQUIRE(m != nullptr);
    CHECK(m - kBuf == 9);
}

TEST_CASE("scan_handles_wildcards") {
    auto m = scan(kBuf, "48 8B 05 ?? ?? ? ?? 48");
    REQUIRE(m != nullptr);
    CHECK(m - kBuf == 2);
}

TEST_CASE("scan_returns_null_when_absent") {
    CHECK(scan(kBuf, "DE AD BE EF") == nullptr);
    CHECK(scan(kBuf, "C3 90") == nullptr);  // would run past the end
    CHECK(scan(std::span<const uint8_t>{}, "90") == nullptr);
}

TEST_CASE("scan_rejects_malformed_pattern") {
    CHECK(scan(kBuf, "4G ??") == nullptr);
    CHECK(scan(kBuf, "") == nullptr);
    CHECK(scan(kBuf, "   ") == nullptr);
    CHECK(scan(kBuf, "488") == nullptr);
    CHECK(scan(kBuf, nullptr) == nullptr);
}

TEST_CASE("resolve_direct") {
    Signature s{"direct", "48 85 C0", 1};
    CHECK(resolve(kBuf, 0x1000, s) == 0x1000 + 9 + 1);
}

TEST_CASE("resolve_riprel32") {
    const uint8_t buf[] = {0x48, 0x8B, 0x05, 0x10, 0x00, 0x00, 0x00};
    Signature s{"rip", "48 8B 05 ?? ?? ?? ??", 3, Signature::RipRel32};
    CHECK(resolve(buf, 0x1000, s) == 0x1000 + 3 + 4 + 0x10);
}

TEST_CASE("resolve_riprel32_negative_displacement") {
    const uint8_t buf[] = {0x48, 0x8B, 0x05, 0xF0, 0xFF, 0xFF, 0xFF};
    Signature s{"rip", "48 8B 05", 3, Signature::RipRel32};
    CHECK(resolve(buf, 0x1000, s) == 0x1000 + 3 + 4 - 0x10);
}

TEST_CASE("resolve_riprel32_out_of_bounds_returns_zero") {
    const uint8_t buf[] = {0x48, 0x8B, 0x05, 0x10};
    Signature s{"rip", "48 8B 05", 3, Signature::RipRel32};
    CHECK(resolve(buf, 0x1000, s) == 0);
}

static std::string g_lastLog;
TEST_CASE("resolve_logs_missing_signature") {
    setSigLogger([](const char* m) { g_lastLog = m; });
    Signature s{"GameWorld", "DE AD BE EF"};
    CHECK(resolve(kBuf, 0x1000, s) == 0);
    CHECK(g_lastLog == "signature not found: GameWorld");
    setSigLogger(nullptr);
}
