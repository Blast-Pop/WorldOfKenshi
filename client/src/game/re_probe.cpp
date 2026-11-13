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

// ---- F7: spawn experiment (Task 7 step 3). Kenshi 1.0.68 RVAs, see docs/re/spawning.md. ----
constexpr uintptr_t kFactoryOff = 0x4a0;           // GameWorld + 0x4a0 -> RootObjectFactory*
constexpr uintptr_t kCreateCharacter = 0x5836e0;   // factory helper: one character at a position
constexpr uintptr_t kPlatoonOfActive = 0x78;       // ActivePlatoon + 0x78 -> Platoon*
constexpr uintptr_t kPlatoonGetFactionVt = 0x58;   // Platoon vtable slot: Faction* getFaction()
constexpr uint8_t kCreateCharacterPrologue[] = {0x40, 0x55, 0x53};  // sanity check before calling

using CreateCharacterFn = uintptr_t (*)(uintptr_t factory, uintptr_t faction, const float* pos, uintptr_t activePlatoon,
                                        uintptr_t charTemplate, uintptr_t home, float scale);
using GetFactionFn = uintptr_t (*)(uintptr_t platoon);

bool isClass(uintptr_t obj, const char* rtti) {
    uintptr_t vt, col;
    if (!readQ(obj, vt) || !readQ(vt - 8, col)) return false;
    uint32_t td = 0;
    __try {
        td = *reinterpret_cast<const uint32_t*>(col + 12);
        return std::strcmp(reinterpret_cast<const char*>(g_base + td + 16), rtti) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

uintptr_t callCreate(CreateCharacterFn fn, uintptr_t factory, uintptr_t faction, const float* pos, uintptr_t ap) {
    __try {
        return fn(factory, faction, pos, ap, 0, 0, 1.0f);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 1;  // sentinel: crashed inside the game function
    }
}

void onF7() {
    uintptr_t gw = g_base + kGameWorld;
    uintptr_t factory = 0, pi = 0, sel = 0, ch = 0;
    readQ(gw + kFactoryOff, factory);
    readQ(gw + kPlayerInterfaceOff, pi);
    if (!factory || !pi || !readQ(pi + 0x2c0, sel) || !readQ(sel, ch) || !isClass(ch, ".?AVCharacterHuman@@")) {
        log("spawn: need a selected character in game (factory=%p)", reinterpret_cast<void*>(factory));
        return;
    }
    // Find the character's ActivePlatoon among its fields.
    uintptr_t ap = 0, apOff = 0;
    for (uintptr_t off = 0; off < 0x1000 && !ap; off += 8) {
        uintptr_t p;
        if (readQ(ch + off, p) && isClass(p, ".?AVActivePlatoon@@")) {
            ap = p;
            apOff = off;
        }
    }
    uintptr_t platoon = 0, vt = 0, getFaction = 0;
    if (!ap || !readQ(ap + kPlatoonOfActive, platoon) || !readQ(platoon, vt) ||
        !readQ(vt + kPlatoonGetFactionVt, getFaction)) {
        log("spawn: ActivePlatoon/Platoon not found (ap=%p)", reinterpret_cast<void*>(ap));
        return;
    }
    uintptr_t faction = reinterpret_cast<GetFactionFn>(getFaction)(platoon);
    log("spawn: char+0x%llx -> ActivePlatoon %p, Platoon %p (%s), faction %p", static_cast<unsigned long long>(apOff),
        reinterpret_cast<void*>(ap), reinterpret_cast<void*>(platoon),
        isClass(platoon, ".?AVPlatoon@@") ? "Platoon" : "?", reinterpret_cast<void*>(faction));

    auto fn = reinterpret_cast<const uint8_t*>(g_base + kCreateCharacter);
    if (std::memcmp(fn, kCreateCharacterPrologue, sizeof(kCreateCharacterPrologue)) != 0) {
        log("spawn: createCharacter prologue mismatch (different Kenshi version?)");
        return;
    }
    float pos[3];
    std::memcpy(pos, reinterpret_cast<const void*>(ch + 0x48), sizeof(pos));
    pos[0] += 40.0f;
    uintptr_t created = callCreate(reinterpret_cast<CreateCharacterFn>(g_base + kCreateCharacter), factory, faction,
                                   pos, ap);
    if (created == 1)
        log("spawn: game function raised an exception");
    else
        log("spawn: created %p (%s) at (%.1f, %.1f, %.1f)", reinterpret_cast<void*>(created),
            created && isClass(created, ".?AVCharacterHuman@@") ? "CharacterHuman" : "not a CharacterHuman", pos[0],
            pos[1], pos[2]);
}

bool g_f7WasDown = false;

}  // namespace

void probeTick() {
    if (!g_base) g_base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    // Dev keys use Ctrl+Shift so they never clash with Kenshi's own bindings (F7 hides its UI).
    bool focused = pid == GetCurrentProcessId() && (GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
                   (GetAsyncKeyState(VK_SHIFT) & 0x8000);
    bool down = (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
    if (down && !g_wasDown && focused) onF8();
    g_wasDown = down;
    bool f7 = (GetAsyncKeyState(VK_F7) & 0x8000) != 0;
    if (f7 && !g_f7WasDown && focused) onF7();
    g_f7WasDown = f7;
}

}  // namespace wok
