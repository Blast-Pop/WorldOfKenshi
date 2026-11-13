#include "spawner.hpp"

#include "../log.hpp"
#include "game_memory.hpp"

#include <windows.h>

#include <cstring>
#include <string>

namespace wok {

namespace {

// RVAs and offsets for Kenshi 1.0.68 - see docs/re/spawning.md.
constexpr uintptr_t kFactoryOff = 0x4a0;
constexpr uintptr_t kFactionMgrOff = 0x4a8;
constexpr uintptr_t kPlayerInterfaceOff = 0x580;
constexpr uintptr_t kPlayerSquadOff = 0x2a8;  // PlayerInterface -> current Platoon (docs/re/game-world.md)
constexpr uintptr_t kFactionCount = 0x08;
constexpr uintptr_t kFactionArray = 0x10;
constexpr uintptr_t kFactionGameData = 0x240;
constexpr uintptr_t kGameDataName = 0x28;
constexpr uintptr_t kPlatoonActive = 0x1d8;
constexpr uintptr_t kCharPosition = 0x48;
constexpr uintptr_t kCreatePlatoon = 0x7f4420;
constexpr uintptr_t kCreateCharacter = 0x5836e0;
constexpr uint8_t kCreatePlatoonPrologue[] = {0x40, 0x53, 0x55};
constexpr uint8_t kCreateCharacterPrologue[] = {0x40, 0x55, 0x53};

using CreatePlatoonFn = uintptr_t (*)(uintptr_t faction, uintptr_t squadTemplate, bool flag, const float* pos);
using CreateCharacterFn = uintptr_t (*)(uintptr_t factory, uintptr_t faction, const float* pos, uintptr_t activePlatoon,
                                        uintptr_t charTemplate, uintptr_t home, float scale);

uintptr_t guardedCreatePlatoon(uintptr_t fn, uintptr_t faction, const float* pos) {
    __try {
        return reinterpret_cast<CreatePlatoonFn>(fn)(faction, 0, true, pos);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

uintptr_t guardedCreateCharacter(uintptr_t fn, uintptr_t factory, uintptr_t faction, const float* pos, uintptr_t ap) {
    __try {
        return reinterpret_cast<CreateCharacterFn>(fn)(factory, faction, pos, ap, 0, 0, 1.0f);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

bool guardedWrite(uintptr_t addr, const void* src, size_t n) {
    __try {
        std::memcpy(reinterpret_cast<void*>(addr), src, n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// MSVC 2010 std::string: 16-byte SSO buffer, size at +0x10, capacity at +0x18.
std::string readVc10String(uintptr_t addr) {
    uint8_t raw[32];
    if (!mem::readBytes(addr, raw, sizeof(raw))) return {};
    uint64_t size, cap;
    uintptr_t ptr;
    std::memcpy(&size, raw + 16, 8);
    std::memcpy(&cap, raw + 24, 8);
    std::memcpy(&ptr, raw, 8);
    if (size > 256) return {};
    std::string s(size, '\0');
    if (cap >= 16) {
        if (!mem::readBytes(ptr, s.data(), size)) return {};
    } else {
        std::memcpy(s.data(), raw, size);
    }
    return s;
}

}  // namespace

bool Spawner::init(uintptr_t gameWorld) {
    gw_ = gameWorld;
    uintptr_t base = mem::moduleBase();
    uint8_t a[3], b[3];
    ok_ = gw_ && mem::readBytes(base + kCreatePlatoon, a, 3) && mem::readBytes(base + kCreateCharacter, b, 3) &&
          std::memcmp(a, kCreatePlatoonPrologue, 3) == 0 && std::memcmp(b, kCreateCharacterPrologue, 3) == 0;
    log("spawner: %s", ok_ ? "ready" : "disabled (unexpected game version)");
    return ok_;
}

bool Spawner::inGame() const {
    // The PlayerInterface already exists on the title screen (its 3D backdrop is a world), so being
    // "in game" also requires the player to have an active squad (PlayerInterface + 0x2a8).
    uintptr_t pi = 0, platoon = 0;
    return ok_ && mem::readPtr(gw_ + kPlayerInterfaceOff, pi) && pi != 0 &&
           mem::readPtr(pi + kPlayerSquadOff, platoon) && mem::isA(platoon, ".?AVPlatoon@@");
}

uintptr_t Spawner::findFaction(std::string_view name) const {
    uintptr_t mgr = 0, arr = 0;
    uint32_t count = 0;
    if (!ok_ || !mem::readPtr(gw_ + kFactionMgrOff, mgr) || !mgr || !mem::readPtr(mgr + kFactionArray, arr) ||
        !mem::readBytes(mgr + kFactionCount, &count, sizeof(count)))
        return 0;
    for (uint32_t i = 0; i < count && i < 1024; ++i) {
        uintptr_t f = 0, gd = 0;
        if (mem::readPtr(arr + 8ull * i, f) && mem::readPtr(f + kFactionGameData, gd) &&
            readVc10String(gd + kGameDataName) == name)
            return f;
    }
    return 0;
}

uintptr_t Spawner::spawnCharacter(uintptr_t faction, const float pos[3]) const {
    uintptr_t base = mem::moduleBase(), factory = 0, ap = 0;
    if (!ok_ || !faction || !mem::readPtr(gw_ + kFactoryOff, factory) || !factory) return 0;
    uintptr_t platoon = guardedCreatePlatoon(base + kCreatePlatoon, faction, pos);
    if (!platoon || !mem::isA(platoon, ".?AVPlatoon@@") || !mem::readPtr(platoon + kPlatoonActive, ap) ||
        !mem::isA(ap, ".?AVActivePlatoon@@")) {
        log("spawner: squad creation failed (%p)", reinterpret_cast<void*>(platoon));
        return 0;
    }
    uintptr_t ch = guardedCreateCharacter(base + kCreateCharacter, factory, faction, pos, ap);
    if (!ch || !mem::isA(ch, ".?AVCharacterHuman@@")) {
        log("spawner: character creation failed (%p)", reinterpret_cast<void*>(ch));
        return 0;
    }
    return ch;
}

bool Spawner::isAlive(uintptr_t character) const { return character && mem::isA(character, ".?AVCharacterHuman@@"); }

void Spawner::setPosition(uintptr_t character, const float pos[3]) const {
    // First version: write the world position directly. To be replaced by move orders (Task 9).
    guardedWrite(character + kCharPosition, pos, sizeof(float) * 3);
}

}  // namespace wok
