#include "re_probe.hpp"

#include "../log.hpp"
#include "../ui/mygui_abi.hpp"
#include "items.hpp"
#include "spawner.hpp"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <unordered_map>
#include <unordered_set>
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

// ---- F6: list factions, then spawn a default character in a NEW squad of a neutral faction. ----
constexpr uintptr_t kFactionMgrOff = 0x4a8;        // GameWorld + 0x4a8 -> faction manager
constexpr uintptr_t kFactionCount = 0x08;          // uint32
constexpr uintptr_t kFactionArray = 0x10;          // Faction**
constexpr uintptr_t kFactionGameData = 0x240;      // Faction -> GameData*
constexpr uintptr_t kGameDataName = 0x28;          // VC10 std::string
constexpr uintptr_t kGameDataStringId = 0x58;      // VC10 std::string
constexpr uintptr_t kCreatePlatoon = 0x7f4420;     // ActivePlatoon* (Faction*, GameData* squad|0, bool, const Vector3*)
constexpr uint8_t kCreatePlatoonPrologue[] = {0x40, 0x53, 0x55};

using CreatePlatoonFn = uintptr_t (*)(uintptr_t faction, uintptr_t squadTemplate, bool flag, const float* pos);

bool copyRaw(uintptr_t addr, void* out, size_t n) {
    __try {
        std::memcpy(out, reinterpret_cast<const void*>(addr), n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Reads an MSVC 2010 std::string (16-byte SSO buffer, size at +0x10, capacity at +0x18).
std::string readVc10String(uintptr_t addr) {
    uint8_t raw[32];
    if (!copyRaw(addr, raw, sizeof(raw))) return "?";
    uint64_t size, cap;
    uintptr_t src = 0;
    std::memcpy(&size, raw + 16, 8);
    std::memcpy(&cap, raw + 24, 8);
    std::memcpy(&src, raw, 8);
    if (size > 256) return "?";
    char buf[257] = {};
    if (cap >= 16) {
        if (!copyRaw(src, buf, size)) return "?";
    } else {
        std::memcpy(buf, raw, size);
    }
    return std::string(buf, size);
}

// Most-derived RTTI name of a polymorphic object (e.g. ".?AVPlatoon@@"), or "" if not readable.
std::string rttiName(uintptr_t obj) {
    uintptr_t vt = 0, col = 0;
    if (!readQ(obj, vt) || !readQ(vt - 8, col)) return {};
    uint32_t hdr[4];
    if (!copyRaw(col, hdr, sizeof(hdr)) || hdr[0] != 1) return {};
    char name[64] = {};
    if (!copyRaw(g_base + hdr[3] + 16, name, sizeof(name) - 1)) return {};
    if (std::strncmp(name, ".?A", 3) != 0) return {};
    return name;
}

uintptr_t callCreatePlatoon(CreatePlatoonFn fn, uintptr_t faction, const float* pos) {
    __try {
        return fn(faction, 0, true, pos);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 1;
    }
}

void onF6() {
    uintptr_t gw = g_base + kGameWorld;
    uintptr_t mgr = 0, arr = 0;
    uint32_t count = 0;
    if (!readQ(gw + kFactionMgrOff, mgr) || !mgr || !readQ(mgr + kFactionArray, arr)) {
        log("factions: manager not found");
        return;
    }
    if (!copyRaw(mgr + kFactionCount, &count, sizeof(count))) count = 0;
    log("factions: %u", count);
    uintptr_t neutral = 0;
    for (uint32_t i = 0; i < count && i < 512; ++i) {
        uintptr_t f = 0, gd = 0;
        if (!readQ(arr + 8ull * i, f) || !readQ(f + kFactionGameData, gd)) continue;
        std::string name = readVc10String(gd + kGameDataName);
        std::string id = readVc10String(gd + kGameDataStringId);
        log("faction %u: '%s' [%s] %s", i, name.c_str(), id.c_str(), isClass(f, ".?AVFaction@@") ? "" : "(not Faction?)");
        if (!neutral && name.find("Drifter") != std::string::npos) neutral = f;
    }
    if (!neutral) {
        log("spawn2: no 'Drifter' faction found, nothing spawned");
        return;
    }

    uintptr_t pi = 0, sel = 0, ch = 0, factory = 0;
    readQ(gw + kFactoryOff, factory);
    if (!readQ(gw + kPlayerInterfaceOff, pi) || !readQ(pi + 0x2c0, sel) || !readQ(sel, ch) ||
        !isClass(ch, ".?AVCharacterHuman@@") || !factory) {
        log("spawn2: need a selected character in game");
        return;
    }
    auto pfn = reinterpret_cast<const uint8_t*>(g_base + kCreatePlatoon);
    auto cfn = reinterpret_cast<const uint8_t*>(g_base + kCreateCharacter);
    if (std::memcmp(pfn, kCreatePlatoonPrologue, 3) != 0 || std::memcmp(cfn, kCreateCharacterPrologue, 3) != 0) {
        log("spawn2: prologue mismatch (different Kenshi version?)");
        return;
    }
    float pos[3];
    std::memcpy(pos, reinterpret_cast<const void*>(ch + 0x48), sizeof(pos));
    pos[0] -= 40.0f;
    uintptr_t ret = callCreatePlatoon(reinterpret_cast<CreatePlatoonFn>(g_base + kCreatePlatoon), neutral, pos);
    log("spawn2: createPlatoon -> %p class '%s'", reinterpret_cast<void*>(ret), rttiName(ret).c_str());
    if (ret <= 1) return;
    uintptr_t ap = 0;
    if (isClass(ret, ".?AVActivePlatoon@@")) {
        ap = ret;
    } else {
        // Probably a Platoon: look for the ActivePlatoon among its fields (and log what we see).
        for (uintptr_t off = 0; off < 0x400; off += 8) {
            uintptr_t p;
            if (!readQ(ret + off, p)) continue;
            std::string cls = rttiName(p);
            if (cls.empty()) continue;
            log("spawn2:   +0x%llx -> %s", static_cast<unsigned long long>(off), cls.c_str());
            if (!ap && cls == ".?AVActivePlatoon@@") ap = p;
        }
    }
    if (!ap) {
        log("spawn2: no ActivePlatoon found, nothing spawned");
        return;
    }
    uintptr_t created =
        callCreate(reinterpret_cast<CreateCharacterFn>(g_base + kCreateCharacter), factory, neutral, pos, ap);
    log("spawn2: created %p (%s) at (%.1f, %.1f, %.1f)", reinterpret_cast<void*>(created),
        created > 1 && isClass(created, ".?AVCharacterHuman@@") ? "CharacterHuman" : "failed", pos[0], pos[1], pos[2]);
}

bool g_f6WasDown = false;

// ---- F5: map every town (name + world position) into WorldOfKenshi\towns.txt ----
// Breadth-first walk over pointers reachable from GameWorld, keeping objects whose RTTI is Town.
// Each Town is a RootObject: position at +0x48 like characters; its name is the name of the
// GameData it points to (GameData +0x28).
void onF5() {
    auto townLike = [](const std::string& cls) {
        return cls == ".?AVTown@@" || cls == ".?AVTownBase@@" || cls == ".?AVTownNull@@";
    };
    std::vector<uintptr_t> frontier{g_base + kGameWorld};
    std::vector<uintptr_t> towns, townLists;
    std::unordered_set<uintptr_t> visited, townSet;
    std::map<std::string, int> counts;
    auto addTown = [&](uintptr_t p) {
        if (townSet.insert(p).second) towns.push_back(p);
    };
    const size_t kMaxNodes = 1500000;
    std::unordered_map<uintptr_t, std::pair<uintptr_t, uintptr_t>> parent;  // child -> (object, offset)
    for (int depth = 0; depth < 5 && !frontier.empty() && visited.size() < kMaxNodes; ++depth) {
        std::vector<uintptr_t> next;
        for (uintptr_t obj : frontier) {
            for (uintptr_t off = 0; off < 0x400 && visited.size() < kMaxNodes; off += 8) {
                uintptr_t p;
                if (!readQ(obj + off, p) || !visited.insert(p).second) continue;
                parent[p] = {obj, off};
                std::string cls = rttiName(p);
                if (cls.find("Town") != std::string::npos) ++counts[cls];
                if (townLike(cls)) addTown(p);
                if (cls == ".?AVTownList@@") townLists.push_back(p);
                if (cls == ".?AV?$lektor@PEAVTownBase@@@@") {
                    // Kenshi's lektor<T*>: element count (uint32) at +0x08, array pointer at +0x10.
                    uint32_t count = 0;
                    uintptr_t arr = 0;
                    if (copyRaw(p + 0x08, &count, sizeof(count)) && readQ(p + 0x10, arr)) {
                        log("towns: lektor<TownBase*> with %u entries", count);
                        // Print how to reach it from GameWorld, so the client can go there directly.
                        std::string chain;
                        for (uintptr_t cur = p; cur != g_base + kGameWorld && parent.count(cur);) {
                            char buf[48];
                            std::snprintf(buf, sizeof(buf), "+0x%llx", static_cast<unsigned long long>(parent[cur].second));
                            chain = std::string(buf) + (chain.empty() ? "" : " -> ") + chain;
                            cur = parent[cur].first;
                        }
                        log("towns: path from GameWorld: %s", chain.c_str());
                        for (uint32_t i = 0; i < count && i < 4096; ++i) {
                            uintptr_t t;
                            if (readQ(arr + 8ull * i, t) && townLike(rttiName(t))) addTown(t);
                        }
                    }
                }
                next.push_back(p);
            }
        }
        frontier.swap(next);
    }
    // The town list itself: look through its fields for arrays of town pointers.
    for (uintptr_t list : townLists) {
        for (uintptr_t off = 0; off < 0x200; off += 8) {
            uintptr_t arr;
            if (!readQ(list + off, arr)) continue;
            for (size_t i = 0; i < 2048; ++i) {
                uintptr_t p;
                if (!readQ(arr + 8 * i, p)) break;
                if (townLike(rttiName(p))) addTown(p);
            }
        }
    }
    for (auto& [cls, n] : counts) log("towns: class %s x%d", cls.c_str(), n);
    log("towns: %zu TownList object(s)", townLists.size());
    std::string path = modDir() + "\\towns.txt";
    FILE* f = std::fopen(path.c_str(), "w");
    int written = 0;
    for (uintptr_t t : towns) {
        float pos[3];
        if (!copyRaw(t + 0x48, pos, sizeof(pos))) continue;
        std::string name;
        for (uintptr_t off = 0; off < 0x200 && name.empty(); off += 8) {
            uintptr_t gd;
            if (readQ(t + off, gd) && rttiName(gd) == ".?AVGameData@@") name = readVc10String(gd + kGameDataName);
        }
        if (name.empty()) name = "?";
        if (f) std::fprintf(f, "%s:%.1f,%.1f,%.1f\n", name.c_str(), pos[0], pos[1], pos[2]);
        ++written;
    }
    if (f) std::fclose(f);
    log("towns: %d written (visited %zu objects) -> %s", written, visited.size(), path.c_str());
}

bool g_f5WasDown = false;

// ---- F4: what identifies the selected character (name, race...) for CreateCharacter ----
// Logs every plausible VC10 std::string stored inline in the character, and every GameData it
// points to (with that data's name and FCS string ID), with their offsets.
bool printable(const std::string& s) {
    if (s.empty() || s.size() > 64) return false;
    for (unsigned char c : s)
        if (c < 0x20 || c > 0x7e) return false;
    return true;
}

void onF4() {
    uintptr_t pi = 0, sel = 0, ch = 0;
    if (!readQ(g_base + kGameWorld + kPlayerInterfaceOff, pi) || !readQ(pi + 0x2c0, sel) || !readQ(sel, ch) ||
        rttiName(ch) != ".?AVCharacterHuman@@") {
        log("ident: select a character in game first");
        return;
    }
    log("ident: character %p", reinterpret_cast<void*>(ch));
    for (uintptr_t off = 0; off < 0x1000; off += 8) {
        std::string inl = readVc10String(ch + off);
        if (printable(inl)) log("ident: +0x%llx string '%s'", static_cast<unsigned long long>(off), inl.c_str());
        uintptr_t p;
        if (!readQ(ch + off, p)) continue;
        std::string cls = rttiName(p);
        if (cls == ".?AVGameData@@")
            log("ident: +0x%llx GameData '%s' [%s]", static_cast<unsigned long long>(off),
                readVc10String(p + kGameDataName).c_str(), readVc10String(p + kGameDataStringId).c_str());
    }
    log("ident: done");
}

bool g_f4WasDown = false;

// ---- F3: who writes a character's position? Hardware write-watch on character+0x48 ----
// Debug registers are per thread: this watches the game (frame) thread only. A vectored exception
// handler arms/disarms them on a deliberate breakpoint and records the code addresses that write.
volatile LONG g_watchMode = 0;       // 1 = arm requested, 2 = disarm requested
uintptr_t g_watchAddr = 0;
uintptr_t g_writers[64];
uintptr_t g_writerCallers[64];  // return address on the stack at the write (caller of memcpy-like helpers)
volatile LONG g_writerCount = 0;
volatile LONG g_writeHits = 0;
double g_watchUntil = 0;
bool g_watching = false;
PVOID g_veh = nullptr;

LONG CALLBACK watchHandler(EXCEPTION_POINTERS* e) {
    auto* ctx = e->ContextRecord;
    if (e->ExceptionRecord->ExceptionCode == EXCEPTION_BREAKPOINT && g_watchMode != 0) {
        if (g_watchMode == 1) {
            ctx->Dr0 = g_watchAddr;
            ctx->Dr7 = 1ull | (1ull << 16) | (3ull << 18);  // L0, break on write, 4 bytes
        } else {
            ctx->Dr0 = 0;
            ctx->Dr7 = 0;
        }
        g_watchMode = 0;
        ctx->Rip += 1;  // skip our int3
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (e->ExceptionRecord->ExceptionCode == EXCEPTION_SINGLE_STEP && (ctx->Dr6 & 1)) {
        InterlockedIncrement(&g_writeHits);
        uintptr_t rip = reinterpret_cast<uintptr_t>(e->ExceptionRecord->ExceptionAddress);
        LONG n = g_writerCount;
        bool known = false;
        for (LONG i = 0; i < n; ++i) known |= g_writers[i] == rip;
        if (!known && n < 64) {
            LONG slot = InterlockedIncrement(&g_writerCount) - 1;
            g_writers[slot] = rip;
            uintptr_t ret = 0;
            readQ(ctx->Rsp, ret);  // for a leaf helper (memcpy), [rsp] is the return address into the caller
            g_writerCallers[slot] = ret;
        }
        ctx->Dr6 = 0;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

__declspec(noinline) void setWatch(LONG mode) {
    g_watchMode = mode;
    __debugbreak();  // handled by watchHandler, which updates this thread's debug registers
}

void onF3() {
    if (g_watching) return;
    uintptr_t pi = 0, sel = 0, ch = 0;
    if (!readQ(g_base + kGameWorld + kPlayerInterfaceOff, pi) || !readQ(pi + 0x2c0, sel) || !readQ(sel, ch) ||
        rttiName(ch) != ".?AVCharacterHuman@@") {
        log("watch: select a character in game first");
        return;
    }
    if (!g_veh) g_veh = AddVectoredExceptionHandler(1, watchHandler);
    g_watchAddr = ch + 0x48;
    g_writerCount = 0;
    g_writeHits = 0;
    setWatch(1);
    g_watching = true;
    g_watchUntil = GetTickCount64() / 1000.0 + 3.0;
    log("watch: watching writes to %p (character %p + 0x48) for 3 s - move the character",
        reinterpret_cast<void*>(g_watchAddr), reinterpret_cast<void*>(ch));
}

void watchTick() {
    if (!g_watching || GetTickCount64() / 1000.0 < g_watchUntil) return;
    setWatch(2);
    g_watching = false;
    log("watch: %ld writes from %ld code location(s) on the game thread", g_writeHits, g_writerCount);
    for (LONG i = 0; i < g_writerCount; ++i) {
        HMODULE mod = nullptr;
        char name[MAX_PATH] = "?";
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(g_writers[i]), &mod);
        if (mod) GetModuleFileNameA(mod, name, MAX_PATH);
        uintptr_t caller = g_writerCallers[i];
        log("watch:   writer %p in %s (+0x%llx); [rsp] = %p (exe+0x%llx)", reinterpret_cast<void*>(g_writers[i]), name,
            static_cast<unsigned long long>(g_writers[i] - reinterpret_cast<uintptr_t>(mod)),
            reinterpret_cast<void*>(caller), static_cast<unsigned long long>(caller - g_base));
    }
}

bool g_f3WasDown = false;

// ---- F2: teleport experiment. The real position lives in the movement object (character +0x640,
// position at +0xc4; Kenshi copies it into character +0x48 every frame). Write there, then watch. ----
bool writeRaw(uintptr_t addr, const void* src, size_t n) {
    __try {
        std::memcpy(reinterpret_cast<void*>(addr), src, n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool callTeleport(uintptr_t fn, uintptr_t movement, const float* pos) {
    __try {
        reinterpret_cast<void (*)(uintptr_t, const float*)>(fn)(movement, pos);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

uintptr_t g_tpChar = 0;
double g_tpUntil = 0;
double g_tpNextLog = 0;

uintptr_t guardedFind2(uintptr_t fn, uintptr_t manager, const mygui::Vc10String* id, int type) {
    __try {
        return reinterpret_cast<uintptr_t (*)(uintptr_t, const mygui::Vc10String*, int)>(fn)(manager, id, type);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 1;
    }
}

void onF2() {
    // Item test: create an Iron Club (rebirth mod template) with Kenshi's factory and give it to
    // the selected character.
    uintptr_t pi = 0, sel = 0, ch = 0;
    if (!readQ(g_base + kGameWorld + kPlayerInterfaceOff, pi) || !readQ(pi + 0x2c0, sel) || !readQ(sel, ch) ||
        rttiName(ch) != ".?AVCharacterHuman@@") {
        log("give: select a character in game first");
        return;
    }
    Spawner spawner;
    Items items;
    if (!spawner.init(g_base + kGameWorld) || !items.init(g_base + kGameWorld)) return;
    // Template and type of the weapon the character holds (hip section = Weapon I).
    uintptr_t inv = 0, secs = 0, hip = 0, vb = 0, held = 0, heldData = 0;
    int type = 0;
    std::string id;
    if (readQ(ch + 0x2e8, inv) && readQ(inv + 0x78, secs) && readQ(secs, hip) && readQ(hip + 0x40, vb) &&
        readQ(vb, held) && held && readQ(held + 0x40, heldData) && heldData) {
        copyRaw(heldData + 0x50, &type, 4);
        id = readVc10String(heldData + 0x58);
    }
    uintptr_t manu = 0, model = 0;
    readQ(held + 0xc0, manu);
    readQ(held + 0xc8, model);
    auto typeOf = [](uintptr_t gd) {
        int t = -1;
        if (gd) copyRaw(gd + 0x50, &t, 4);
        return t;
    };
    log("give: weapon '%s' type %d, +0xc0 '%s' type %d, +0xc8 '%s' type %d", id.c_str(), type,
        manu ? readVc10String(manu + 0x58).c_str() : "-", typeOf(manu), model ? readVc10String(model + 0x58).c_str() : "-",
        typeOf(model));
    // Lookups: both candidate functions, by the weapon's own id and type.
    log("give: lookup 0x6bfda0 -> %p", reinterpret_cast<void*>(spawner.findGameData(id, type)));
    {
        using Find2 = uintptr_t (*)(uintptr_t, const mygui::Vc10String*, int);
        mygui::Vc10String sid(id);
        uintptr_t r = guardedFind2(reinterpret_cast<uintptr_t>(reinterpret_cast<Find2>(g_base + 0x6bfe60)),
                                   g_base + kGameWorld + 0x20, &sid, type);
        log("give: lookup 0x6bfe60 -> %p (wanted %p)", reinterpret_cast<void*>(r), reinterpret_cast<void*>(heldData));
    }
    // Creation: (manufacturer, weapon, model) as the factory's weapon branch reads them.
    uintptr_t item = items.create(manu, heldData, model);
    bool given = items.give(ch, item);
    log("give: created %p (%s) given: %s", reinterpret_cast<void*>(item), item ? rttiName(item).c_str() : "-",
        given ? "yes" : "no");
}

void tpTick() {
    double now = GetTickCount64() / 1000.0;
    if (!g_tpChar || now > g_tpUntil || now < g_tpNextLog) return;
    g_tpNextLog = now + 0.4;
    float p[3];
    if (copyRaw(g_tpChar + 0x48, p, sizeof(p))) log("tp:   character at (%.1f, %.1f, %.1f)", p[0], p[1], p[2]);
}

bool g_f2WasDown = false;

// ---- F10: character components (persistence RE). Lists the character's pointers to stats,
// inventory, medical and body objects, and dumps the CharStats floats. ----
void timeSearch() {
    // Displayed clock, e.g. "Day: 1" + "10:06": find GameWorld values matching it.
    std::string shown = mygui::labelText(mygui::findBySuffix("_TimeText", 12));
    int h = -1, m = -1;
    size_t colon = shown.rfind(':');
    if (colon != std::string::npos && colon >= 2) std::sscanf(shown.c_str() + colon - 2, "%d:%d", &h, &m);
    log("time: shown '%s' -> %d:%02d", shown.c_str(), h, m);
    if (h < 0) return;
    double hours = h + m / 60.0;
    std::vector<uint8_t> gw(0x4000);
    copyRaw(g_base + kGameWorld, gw.data(), gw.size());
    std::string hits;
    for (size_t o = 0; o + 8 <= gw.size(); o += 4) {
        float f;
        double d;
        std::memcpy(&f, &gw[o], 4);
        std::memcpy(&d, &gw[o], 8);
        char buf[64];
        for (double target : {hours, hours / 24.0, hours * 60.0, 24.0 + hours, 48.0 + hours})
            if (std::fabs(f - target) < 0.05 * (target > 1 ? target : 1) / 10) {
                std::snprintf(buf, sizeof(buf), " f+%zx=%g", o, f);
                hits += buf;
            }
        if (o % 8 == 0)
            for (double target : {hours, hours / 24.0, hours * 60.0, hours * 3600.0, 24.0 + hours})
                if (std::fabs(d - target) < 0.02 * (target > 1 ? target : 1)) {
                    std::snprintf(buf, sizeof(buf), " d+%zx=%g", o, d);
                    hits += buf;
                }
    }
    log("time: candidates%s", hits.c_str());
}

void onF10() {
    timeSearch();
    uintptr_t pi = 0, sel = 0, ch = 0;
    if (!readQ(g_base + kGameWorld + kPlayerInterfaceOff, pi) || !readQ(pi + 0x2c0, sel) || !readQ(sel, ch) ||
        rttiName(ch) != ".?AVCharacterHuman@@") {
        log("comp: select a character in game first");
        return;
    }
    log("comp: character %p", reinterpret_cast<void*>(ch));
    uintptr_t stats = 0;
    for (uintptr_t off = 0; off < 0x1400; off += 8) {
        uintptr_t p = 0;
        if (!readQ(ch + off, p) || p < 0x10000) continue;
        std::string n = rttiName(p);
        if (n.find("Stat") == std::string::npos && n.find("Inventory") == std::string::npos &&
            n.find("Medical") == std::string::npos && n.find("Body") == std::string::npos &&
            n.find("Anatomy") == std::string::npos)
            continue;
        log("comp: +0x%llx -> %s %p", static_cast<unsigned long long>(off), n.c_str(), reinterpret_cast<void*>(p));
        if (n == ".?AVCharStats@@" && !stats) stats = p;
    }
    // Inventory layout: pointer fields (with RTTI) and pointer vectors (begin/end/capacity).
    uintptr_t inv = 0;
    if (readQ(ch + 0x2e8, inv) && inv) {
        std::string dump;
        for (uintptr_t off = 0; off < 0x200; off += 8) {
            uintptr_t v = 0, e = 0;
            if (!readQ(inv + off, v)) continue;
            std::string n1 = v > 0x10000 ? rttiName(v) : "";
            std::string n2 = v > 0x10000 && readQ(v, e) && e > 0x10000 ? rttiName(e) : "";
            char buf[160];
            std::snprintf(buf, sizeof(buf), " +%llx=%llx%s%s%s%s", static_cast<unsigned long long>(off),
                          static_cast<unsigned long long>(v), n1.empty() ? "" : "[", n1.c_str(),
                          n2.empty() ? "" : "]{*", n2.c_str());
            dump += buf;
            if (dump.size() > 200) {
                log("invdump:%s", dump.c_str());
                dump.clear();
            }
        }
        if (!dump.empty()) log("invdump:%s", dump.c_str());
        // Sections: Kenshi array at Inventory+0x68 (vtable, u32 count, u32 capacity, T* data).
        uint32_t count = 0;
        uintptr_t data = 0;
        if (copyRaw(inv + 0x70, &count, 4) && readQ(inv + 0x78, data) && count < 64) {
            for (uint32_t i = 0; i < count; ++i) {
                uintptr_t sec = 0;
                if (!readQ(data + i * 8, sec) || !sec) continue;
                std::string line;
                for (uintptr_t off = 8; off < 0x100; off += 8) {
                    std::string str = readVc10String(sec + off);
                    if (str != "?" && str.size() >= 2 && str.size() < 40) line += " str+" + std::to_string(off) + "='" + str + "'";
                    uintptr_t vp = 0, arr = 0;
                    uint32_t sz[2] = {};
                    // a Kenshi array: vtable in the image, then count <= capacity, then data
                    if (readQ(sec + off, vp) && vp > g_base && vp < g_base + 0x4000000 && copyRaw(sec + off + 8, sz, 8) &&
                        sz[0] <= sz[1] && sz[1] < 4096 && sz[0] > 0 && readQ(sec + off + 16, arr) && arr) {
                        uintptr_t first = 0;
                        std::string fn = readQ(arr, first) && first ? rttiName(first) : "";
                        line += " arr+" + std::to_string(off) + "(" + std::to_string(sz[0]) + "x" + fn + ")";
                    }
                }
                log("invsec %u %p:%s", i, reinterpret_cast<void*>(sec), line.c_str());
                // Raw layout of the section: value, its RTTI, and the RTTI of what it points to.
                std::string raw;
                for (uintptr_t off = 0x28; off < 0xc0; off += 8) {
                    uintptr_t v = 0, e = 0;
                    if (!readQ(sec + off, v)) continue;
                    std::string n1 = v > 0x10000 ? rttiName(v) : "";
                    std::string n2 = v > 0x10000 && readQ(v, e) && e > 0x10000 ? rttiName(e) : "";
                    char buf[200];
                    std::snprintf(buf, sizeof(buf), " +%llx=%llx%s%s%s%s", static_cast<unsigned long long>(off),
                                  static_cast<unsigned long long>(v), n1.empty() ? "" : "[", n1.c_str(),
                                  n2.empty() ? "" : "]{*", n2.c_str());
                    raw += buf;
                }
                log("invraw %u:%s", i, raw.c_str());
                // First item of the section: entries are 16 bytes (Item*, then 8 bytes).
                uintptr_t vb = 0, ve = 0, item = 0;
                if (readQ(sec + 0x40, vb) && readQ(sec + 0x48, ve) && ve > vb && readQ(vb, item) && item) {
                    uint32_t extra[2] = {};
                    copyRaw(vb + 8, extra, 8);
                    std::string it = " entry-extra=" + std::to_string(extra[0]) + "," + std::to_string(extra[1]);
                    for (uintptr_t off = 8; off < 0x200; off += 8) {
                        uintptr_t v = 0;
                        if (!readQ(item + off, v)) continue;
                        std::string n = v > 0x10000 ? rttiName(v) : "";
                        if (n == ".?AVGameData@@")
                            it += " gd+" + std::to_string(off) + "='" + readVc10String(v + 0x28) + "'/" + readVc10String(v + 0x58);
                        else if (!n.empty())
                            it += " p+" + std::to_string(off) + "=" + n;
                        std::string str = readVc10String(item + off);
                        if (str != "?" && str.size() >= 3 && str.size() < 48) it += " s+" + std::to_string(off) + "='" + str + "'";
                        int32_t iv[2];
                        float fv[2];
                        std::memcpy(iv, &v, 8);
                        std::memcpy(fv, &v, 8);
                        for (int k = 0; k < 2; ++k) {
                            if (iv[k] > 0 && iv[k] < 100000 && n.empty())
                                it += " i+" + std::to_string(off + k * 4) + "=" + std::to_string(iv[k]);
                            else if (fv[k] > 0.001f && fv[k] < 100000.0f && n.empty() && iv[k] >= 100000)
                                it += " f+" + std::to_string(off + k * 4) + "=" + std::to_string(fv[k]);
                        }
                    }
                    for (char& ch : it)
                        if (static_cast<unsigned char>(ch) < 0x20 || static_cast<unsigned char>(ch) > 0x7e) ch = '.';
                    log("invitem %u %s%s", i, rttiName(item).c_str(), it.c_str());
                }
            }
        }
        for (uintptr_t off = 8; off < 0x200; off += 8) {
            uintptr_t a = 0, b = 0, c = 0;
            if (!readQ(inv + off, a)) continue;
            std::string n = a > 0x10000 ? rttiName(a) : "";
            if (!n.empty()) log("inv: +0x%llx -> %s", static_cast<unsigned long long>(off), n.c_str());
            if (readQ(inv + off + 8, b) && readQ(inv + off + 16, c) && a > 0x10000 && b >= a && c >= b &&
                b - a < 0x4000 && (b - a) % 8 == 0 && b > a) {
                std::string first;
                uintptr_t e0 = 0;
                if (readQ(a, e0) && e0 > 0x10000) first = rttiName(e0);
                if (!first.empty())
                    log("inv: +0x%llx vector of %llu x %s", static_cast<unsigned long long>(off),
                        static_cast<unsigned long long>((b - a) / 8), first.c_str());
            }
        }
    }
    if (!stats) return;
    float f[256];
    if (!copyRaw(stats, f, sizeof(f))) return;
    std::string line;
    for (int i = 2; i < 256; ++i) {  // skip the vtable
        if (f[i] == 0.0f || !(f[i] > -100000.0f && f[i] < 100000.0f)) continue;
        char buf[32];
        std::snprintf(buf, sizeof(buf), " +0x%x=%g", i * 4, f[i]);
        line += buf;
        if (line.size() > 180) {
            log("comp: stats%s", line.c_str());
            line.clear();
        }
    }
    if (!line.empty()) log("comp: stats%s", line.c_str());
}

bool g_f10WasDown = false;

// ---- F11: game speed RE. Clicks Kenshi's speed buttons one after the other and logs which
// GameWorld fields changed (float view), to find the speed variable. ----
int g_speedStep = -1;
int g_speedWait = 0;
std::vector<uint8_t> g_gwSnap;

void snapGameWorld(std::vector<uint8_t>& out) {
    out.assign(0x3000, 0);
    copyRaw(g_base + kGameWorld, out.data(), out.size());
}

void speedTick() {
    if (g_speedStep < 0 || --g_speedWait > 0) return;
    static const char* kButtons[] = {"_TimeSpeedButton3", "_TimeSpeedButton1", "_TimeSpeedButton4", "_TimeSpeedButton2"};
    if (g_speedStep > 0) {  // diff against the snapshot taken before the previous click
        std::vector<uint8_t> now;
        snapGameWorld(now);
        std::string diff;
        for (size_t o = 0; o + 4 <= now.size(); o += 4) {
            if (std::memcmp(&now[o], &g_gwSnap[o], 4) == 0) continue;
            float a, b;
            std::memcpy(&a, &g_gwSnap[o], 4);
            std::memcpy(&b, &now[o], 4);
            if (std::fabs(a) > 1e6f || std::fabs(b) > 1e6f || (std::fabs(a) < 1e-6f && std::fabs(b) < 1e-6f)) continue;
            char buf[64];
            std::snprintf(buf, sizeof(buf), " +%zx:%g->%g", o, a, b);
            diff += buf;
            if (diff.size() > 900) break;
        }
        log("speed: after %s:%s", kButtons[g_speedStep - 1], diff.c_str());
    }
    if (g_speedStep >= 4) {
        g_speedStep = -1;
        return;
    }
    snapGameWorld(g_gwSnap);
    mygui::Widget* b = mygui::findBySuffix(kButtons[g_speedStep], 12);
    log("speed: click %s (%s)", kButtons[g_speedStep], b ? "found" : "missing");
    if (b) mygui::click(b);
    ++g_speedStep;
    g_speedWait = 30;
}

bool g_f11WasDown = false;

// ---- F1: two-press GameWorld diff (any byte), e.g. to find the pause-menu freeze flag. ----
std::vector<uint8_t> g_diffSnap;
bool g_f1WasDown = false;
void onF1() {
    std::vector<uint8_t> now(0x20000);
    copyRaw(g_base + kGameWorld, now.data(), now.size());
    if (g_diffSnap.empty()) {
        g_diffSnap = now;
        log("diff: snapshot taken");
        return;
    }
    std::string out;
    for (size_t o = 0; o < now.size(); ++o) {
        if (now[o] == g_diffSnap[o]) continue;
        size_t a = o & ~size_t{3};
        uint32_t x, y;
        std::memcpy(&x, &g_diffSnap[a], 4);
        std::memcpy(&y, &now[a], 4);
        // keep small integer/bool changes only (flags), skip timers and positions
        if (x <= 1 && y <= 1) {  // booleans only
            char buf[48];
            std::snprintf(buf, sizeof(buf), " +%zx:%u->%u", a, x, y);
            out += buf;
        }
        o = a + 3;
    }
    log("diff:%s", out.c_str());
    g_diffSnap.clear();
}

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
    bool f6 = (GetAsyncKeyState(VK_F6) & 0x8000) != 0;
    if (f6 && !g_f6WasDown && focused) onF6();
    g_f6WasDown = f6;
    bool f5 = (GetAsyncKeyState(VK_F5) & 0x8000) != 0;
    if (f5 && !g_f5WasDown && focused) onF5();
    g_f5WasDown = f5;
    bool f4 = (GetAsyncKeyState(VK_F4) & 0x8000) != 0;
    if (f4 && !g_f4WasDown && focused) onF4();
    g_f4WasDown = f4;
    bool f3 = (GetAsyncKeyState(VK_F3) & 0x8000) != 0;
    if (f3 && !g_f3WasDown && focused) onF3();
    g_f3WasDown = f3;
    watchTick();
    bool f2 = (GetAsyncKeyState(VK_F2) & 0x8000) != 0;
    if (f2 && !g_f2WasDown && focused) onF2();
    g_f2WasDown = f2;
    tpTick();
    bool f10 = (GetAsyncKeyState(VK_F10) & 0x8000) != 0;
    if (f10 && !g_f10WasDown && focused) onF10();
    g_f10WasDown = f10;
    bool f11 = (GetAsyncKeyState(VK_F11) & 0x8000) != 0;
    if (f11 && !g_f11WasDown && focused && g_speedStep < 0) {
        g_speedStep = 0;
        g_speedWait = 0;
    }
    g_f11WasDown = f11;
    speedTick();
    bool f1 = (GetAsyncKeyState(VK_F1) & 0x8000) != 0;
    if (f1 && !g_f1WasDown && focused) onF1();
    g_f1WasDown = f1;
}

}  // namespace wok
