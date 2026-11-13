#include "re_probe.hpp"

#include "../log.hpp"

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace wok {

namespace {

// RVAs in kenshi_x64.exe 1.0.68 (docs/re/game-world.md).
constexpr uintptr_t kGameWorld = 0x2134110;
constexpr uintptr_t kGameWorldVt = 0x1722608;
constexpr uintptr_t kPlayerInterfaceOff = 0x580;
constexpr uintptr_t kPlayerInterfaceSize = 0x2f8;

struct KnownVt {
    uintptr_t rva;
    const char* name;
};
constexpr KnownVt kVtables[] = {
    {0x16f2848, "CharacterHuman"}, {0x16f9eb8, "Character"},    {0x16f2208, "CharacterAnimal"},
    {0x1718808, "Platoon"},        {0x16e2e08, "ActivePlatoon"},
};

uintptr_t g_base = 0;
bool g_wasDown = false;
uintptr_t g_char = 0;
std::vector<float> g_snapshot;
int g_lines = 0;

bool readQ(uintptr_t addr, uintptr_t& out) {
    if (addr < 0x10000 || addr > 0x7FFFFFFFFFF8ull || (addr & 7)) return false;
    __try {
        out = *reinterpret_cast<const uintptr_t*>(addr);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool readF(uintptr_t addr, float& out) {
    __try {
        out = *reinterpret_cast<const float*>(addr);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

const char* classOf(uintptr_t obj) {
    uintptr_t vt;
    if (!readQ(obj, vt)) return nullptr;
    for (auto& k : kVtables)
        if (vt == g_base + k.rva) return k.name;
    return nullptr;
}

void scan(uintptr_t obj, size_t bytes, int depth, const std::string& path) {
    for (size_t off = 0; off < bytes && g_lines < 300; off += 8) {
        uintptr_t p;
        if (!readQ(obj + off, p)) continue;
        char buf[32];
        std::snprintf(buf, sizeof(buf), "+0x%zx", off);
        std::string here = path + buf;
        if (const char* cls = classOf(p)) {
            log("probe: %s -> %s @ %p", here.c_str(), cls, reinterpret_cast<void*>(p));
            ++g_lines;
            if (!g_char && std::strncmp(cls, "CharacterHuman", 14) == 0) g_char = p;
            continue;
        }
        if (depth > 0) scan(p, 64 * 8, depth - 1, here + "->");
    }
}

void snapshot(std::vector<float>& out) {
    out.assign(0x1000 / 4, NAN);
    for (size_t i = 0; i < out.size(); ++i) readF(g_char + i * 4, out[i]);
}

void onF8() {
    uintptr_t gwVt = 0;
    readQ(g_base + kGameWorld, gwVt);
    log("probe: base=%p GameWorld vtable %s", reinterpret_cast<void*>(g_base),
        gwVt == g_base + kGameWorldVt ? "OK" : "MISMATCH");
    uintptr_t pi = 0;
    if (!readQ(g_base + kGameWorld + kPlayerInterfaceOff, pi) || !pi) {
        log("probe: PlayerInterface is null (not in game?)");
        return;
    }
    if (!g_char) {
        g_lines = 0;
        scan(pi, kPlayerInterfaceSize, 2, "PI");
        log("probe: scan done, %d hits, tracking character %p", g_lines, reinterpret_cast<void*>(g_char));
        if (g_char) snapshot(g_snapshot);
        return;
    }
    std::vector<float> now;
    snapshot(now);
    int shown = 0;
    for (size_t i = 0; i < now.size() && shown < 80; ++i) {
        float a = g_snapshot[i], b = now[i];
        if (!std::isfinite(a) || !std::isfinite(b) || std::fabs(b) > 1e7f) continue;
        float delta = std::fabs(b - a);
        if (delta >= 0.05f && delta <= 5000.f) {
            log("probe: char+0x%zx  %.3f -> %.3f", i * 4, a, b);
            ++shown;
        }
    }
    log("probe: %d changed floats", shown);
    g_snapshot = std::move(now);
}

}  // namespace

void probeTick() {
    if (!g_base) g_base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    bool down = (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
    if (down && !g_wasDown) {
        DWORD pid = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &pid);
        if (pid == GetCurrentProcessId()) onF8();
    }
    g_wasDown = down;
}

}  // namespace wok
