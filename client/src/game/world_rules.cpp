#include "world_rules.hpp"

#include "../log.hpp"
#include "game_memory.hpp"

#include <cstdint>
#include <cstring>

namespace wok {

namespace {

// Kenshi 1.0.68: the function that copies the advanced options into their globals (RVA 0x912f80)
// stores each one with "mov [rip + disp], eax". Checking those stores proves the addresses.
struct Rate {
    const char* name;
    uintptr_t store;   // RVA of the 6-byte store instruction
    uintptr_t global;  // RVA of the float
};
constexpr Rate kBuilding{"building", 0x912faf, 0x2133584};
constexpr Rate kResearch{"research", 0x912fc1, 0x213358c};
constexpr Rate kProduction{"production", 0x912fca, 0x2133590};

bool storesTo(const Rate& r) {
    uint8_t code[6] = {};
    if (!mem::readBytes(mem::moduleBase() + r.store, code, sizeof(code)) || code[0] != 0x89 || code[1] != 0x05)
        return false;
    int32_t disp = 0;
    std::memcpy(&disp, code + 2, 4);
    return r.store + 6 + static_cast<intptr_t>(disp) == r.global;
}

}  // namespace

bool applyWorldRules(const WorldRules& rules) {
    static const bool known = storesTo(kBuilding) && storesTo(kResearch) && storesTo(kProduction);
    static bool warned = false;
    if (!known) {
        if (!warned) log("rules: unexpected Kenshi code, the server's rates are not applied");
        warned = true;
        return false;
    }
    const uintptr_t base = mem::moduleBase();
    bool changed = false;
    for (auto [rate, value] : {std::pair{kProduction, rules.productionSpeed}, std::pair{kBuilding, rules.buildingSpeed},
                               std::pair{kResearch, rules.researchSpeed}}) {
        float now = 0;
        if (!mem::readBytes(base + rate.global, &now, sizeof(now))) return false;
        if (now == value) continue;
        if (!mem::writeBytes(base + rate.global, &value, sizeof(value))) return false;
        changed = true;
    }
    if (changed)
        log("rules: production x%.2f, building x%.2f, research x%.2f", rules.productionSpeed, rules.buildingSpeed,
            rules.researchSpeed);
    return true;
}

}  // namespace wok
