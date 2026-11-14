#include "spawner.hpp"

#include "../log.hpp"
#include "game_memory.hpp"
#include "../ui/mygui_abi.hpp"

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
constexpr uintptr_t kFindGameData = 0x6bfda0;      // (GameData manager, const std::string& id, int type) -> GameData*
constexpr uint8_t kFindGameDataPrologue[] = {0x48, 0x89, 0x5c, 0x24, 0x10};
constexpr uintptr_t kGameDataManagerOff = 0x20;    // GameWorld + 0x20
constexpr uintptr_t kCharActivePlatoon = 0x658;    // Character -> its ActivePlatoon
constexpr uintptr_t kPlatoonOfActive = 0x78;       // ActivePlatoon -> Platoon
constexpr uintptr_t kPlatoonGetFactionVt = 0x58;   // Platoon vtable slot: Faction* getFaction()
constexpr uintptr_t kCharName = 0x18;              // std::string
constexpr uintptr_t kCharMovement = 0x640;         // Character -> CharMovement
constexpr uintptr_t kMovementTeleportVt = 0xc8;    // CharMovement vtable slot: teleport(const Vector3*)
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

using FindGameDataFn = uintptr_t (*)(uintptr_t manager, const mygui::Vc10String* id, int flag);
using GetFactionFn = uintptr_t (*)(uintptr_t platoon);

uintptr_t guardedFind(uintptr_t fn, uintptr_t manager, const mygui::Vc10String* id, int type) {
    __try {
        return reinterpret_cast<FindGameDataFn>(fn)(manager, id, type);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

uintptr_t guardedFaction(uintptr_t fn, uintptr_t platoon) {
    __try {
        return reinterpret_cast<GetFactionFn>(fn)(platoon);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

bool guardedDestroy(uintptr_t fn, uintptr_t gw, uintptr_t obj) {
    __try {
        reinterpret_cast<void (*)(uintptr_t, uintptr_t, int, const char*)>(fn)(gw, obj, 0, "World of Kenshi: player left");
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool guardedTeleport(uintptr_t fn, uintptr_t movement, const float* pos) {
    __try {
        reinterpret_cast<void (*)(uintptr_t, const float*)>(fn)(movement, pos);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
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

bool Spawner::destroy(uintptr_t character) const {
    // GameWorld::destroy (RVA 0x799af0): (GameWorld*, RootObject*, int, const char* reason).
    constexpr uintptr_t kDestroy = 0x799af0;
    constexpr uint8_t kPrologue[] = {0x40, 0x55, 0x56, 0x57, 0x41, 0x54};
    uint8_t p[sizeof(kPrologue)];
    if (!ok_ || !isAlive(character) || !mem::readBytes(mem::moduleBase() + kDestroy, p, sizeof(p)) ||
        std::memcmp(p, kPrologue, sizeof(p)) != 0)
        return false;
    return guardedDestroy(mem::moduleBase() + kDestroy, gw_, character);
}

bool Spawner::isAlive(uintptr_t character) const { return character && mem::isA(character, ".?AVCharacterHuman@@"); }

void Spawner::setPosition(uintptr_t character, const float pos[3]) const {
    // Teleport through the character's own movement object (docs/re/character.md): writing the
    // position fields directly is undone by the engine every frame.
    uintptr_t movement = 0, vt = 0, fn = 0;
    if (!mem::readPtr(character + kCharMovement, movement) || !movement || !mem::readPtr(movement, vt) ||
        !mem::readPtr(vt + kMovementTeleportVt, fn))
        return;
    guardedTeleport(fn, movement, pos);
}

uintptr_t Spawner::findGameData(std::string_view stringId, int type) const {
    if (!ok_ || stringId.empty()) return 0;
    uint8_t prologue[5];
    if (!mem::readBytes(mem::moduleBase() + kFindGameData, prologue, 5) ||
        std::memcmp(prologue, kFindGameDataPrologue, 5) != 0)
        return 0;
    mygui::Vc10String id(stringId);  // MSVC 2010 std::string layout, read-only for the game
    uintptr_t gd = guardedFind(mem::moduleBase() + kFindGameData, gw_ + kGameDataManagerOff, &id, type);
    return gd && mem::isA(gd, ".?AVGameData@@") ? gd : 0;
}

namespace {
// Rewrite an MSVC std::string in place, never growing it past its capacity (no allocation).
void writeStringInPlace(uintptr_t str, std::string_view text) {
    uint8_t raw[32];
    if (!mem::readBytes(str, raw, sizeof(raw))) return;
    uint64_t cap;
    uintptr_t ptr;
    std::memcpy(&cap, raw + 24, 8);
    std::memcpy(&ptr, raw, 8);
    uint64_t size = std::min<uint64_t>(text.size(), cap);
    uintptr_t buffer = cap >= 16 ? ptr : str;
    char terminator = 0;
    if (!guardedWrite(buffer, text.data(), size) || !guardedWrite(buffer + size, &terminator, 1)) return;
    guardedWrite(str + 16, &size, sizeof(size));
}
}  // namespace

void Spawner::setName(uintptr_t character, std::string_view name) {
    // Only the character's own string: Kenshi caches the name in its name tag and panels, so a
    // visible rename goes through the character editor instead (see MultiplayerMenu).
    writeStringInPlace(character + kCharName, name);
}

bool Spawner::restorePlayerCharacter(uintptr_t playerCharacter, const CharacterData& data) const {
    if (!ok_ || !mem::isA(playerCharacter, ".?AVCharacterHuman@@")) {
        log("restore: no player character to restore into");
        return false;
    }
    // The New Game character becomes the server character, back where it was left. Its name is
    // applied afterwards through Kenshi's own rename (portrait name field), which refreshes the
    // name tag; writing the string here first would make that rename a no-op.
    const float pos[3] = {data.x, data.y, data.z};
    setPosition(playerCharacter, pos);
    log("restore: '%s' back at (%.1f, %.1f, %.1f)", data.name.c_str(), pos[0], pos[1], pos[2]);
    return true;
}

}  // namespace wok
