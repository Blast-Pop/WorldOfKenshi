#include <core/sigscan.hpp>

#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace wok {

namespace {

SigLogFn g_sigLog = nullptr;

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Each element: 0..255 = byte to match, -1 = wildcard. nullopt if malformed or empty.
std::optional<std::vector<int>> parsePattern(const char* pattern) {
    if (pattern == nullptr) return std::nullopt;
    std::vector<int> out;
    const char* p = pattern;
    while (*p) {
        while (*p == ' ' || *p == '\t') ++p;
        if (!*p) break;
        const char* start = p;
        while (*p && *p != ' ' && *p != '\t') ++p;
        size_t len = static_cast<size_t>(p - start);
        if ((len == 1 && start[0] == '?') || (len == 2 && start[0] == '?' && start[1] == '?')) {
            out.push_back(-1);
        } else if (len == 2 && hexValue(start[0]) >= 0 && hexValue(start[1]) >= 0) {
            out.push_back(hexValue(start[0]) * 16 + hexValue(start[1]));
        } else {
            return std::nullopt;
        }
    }
    if (out.empty()) return std::nullopt;
    return out;
}

}  // namespace

void setSigLogger(SigLogFn fn) { g_sigLog = fn; }

const uint8_t* scan(std::span<const uint8_t> region, const char* pattern) {
    auto pat = parsePattern(pattern);
    if (!pat || pat->size() > region.size()) return nullptr;
    const size_t n = pat->size();
    for (size_t i = 0; i + n <= region.size(); ++i) {
        size_t j = 0;
        while (j < n && ((*pat)[j] < 0 || region[i + j] == (*pat)[j])) ++j;
        if (j == n) return region.data() + i;
    }
    return nullptr;
}

uintptr_t resolve(std::span<const uint8_t> region, uintptr_t regionBase, const Signature& sig) {
    const uint8_t* m = scan(region, sig.pattern);
    uintptr_t result = 0;
    if (m) {
        size_t at = static_cast<size_t>(m - region.data()) + static_cast<size_t>(sig.offset);
        if (sig.kind == Signature::Direct) {
            result = regionBase + at;
        } else if (at + 4 <= region.size()) {
            int32_t disp;
            std::memcpy(&disp, region.data() + at, 4);
            result = regionBase + at + 4 + static_cast<intptr_t>(disp);
        }
    }
    if (result == 0 && g_sigLog) {
        std::string msg = std::string("signature not found: ") + sig.name;
        g_sigLog(msg.c_str());
    }
    return result;
}

}  // namespace wok
