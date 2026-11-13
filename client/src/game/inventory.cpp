#include "inventory.hpp"

#include "../log.hpp"
#include "game_memory.hpp"

#include <wok/protocol.hpp>

#include <windows.h>

#include <cstdlib>
#include <sstream>
#include <vector>

namespace wok {

namespace {

constexpr uintptr_t kCharInventory = 0x2e8;
constexpr uintptr_t kInvSectionCount = 0x70;   // u32
constexpr uintptr_t kInvSections = 0x78;       // InventorySection**
constexpr uintptr_t kSectionName = 0x08;       // std::string
constexpr uintptr_t kSectionItemsBegin = 0x40; // 16-byte entries, Item* first
constexpr uintptr_t kSectionItemsEnd = 0x48;
constexpr uintptr_t kItemTemplate = 0x40;      // GameData*
constexpr uintptr_t kItemAux1 = 0xc0;          // GameData* (weapon manufacturer)
constexpr uintptr_t kItemAux2 = 0xc8;          // GameData* (weapon model / armour grade)
constexpr uintptr_t kItemQuantity = 0x12c;     // int
constexpr uintptr_t kGameDataType = 0x50;      // int
constexpr uintptr_t kGameDataId = 0x58;        // std::string
constexpr uintptr_t kInvRemoveDestroyVt = 0x30;  // Inventory vtable: removeItemAutoDestroy(item, qty, bool)
constexpr int kWeaponType = 2;                   // GameData type of weapons

struct Ref {
    std::string id;
    int type = 0;
};

Ref refOf(uintptr_t gdField) {
    uintptr_t gd = 0;
    Ref r;
    if (!mem::readPtr(gdField, gd) || !gd || !mem::isA(gd, ".?AVGameData@@")) return r;
    r.id = mem::readString(gd + kGameDataId, kMaxStringIdBytes);
    mem::readBytes(gd + kGameDataType, &r.type, sizeof(r.type));
    return r;
}

// Every item of every section: (section name, Item*).
std::vector<std::pair<std::string, uintptr_t>> listItems(uintptr_t character, uintptr_t& inventory) {
    std::vector<std::pair<std::string, uintptr_t>> out;
    uint32_t count = 0;
    uintptr_t sections = 0;
    if (!mem::isA(character, ".?AVCharacterHuman@@") || !mem::readPtr(character + kCharInventory, inventory) ||
        !inventory || !mem::readBytes(inventory + kInvSectionCount, &count, 4) ||
        !mem::readPtr(inventory + kInvSections, sections) || count > 64)
        return out;
    for (uint32_t i = 0; i < count; ++i) {
        uintptr_t sec = 0, begin = 0, end = 0;
        if (!mem::readPtr(sections + i * 8, sec) || !sec || !mem::readPtr(sec + kSectionItemsBegin, begin) ||
            !mem::readPtr(sec + kSectionItemsEnd, end) || end < begin || end - begin > 16 * 512)
            continue;
        std::string name = mem::readString(sec + kSectionName, 32);
        for (uintptr_t e = begin; e < end; e += 16) {
            uintptr_t item = 0;
            if (mem::readPtr(e, item) && item) out.emplace_back(name, item);
        }
    }
    return out;
}

bool guardedRemove(uintptr_t fn, uintptr_t inventory, uintptr_t item) {
    __try {
        reinterpret_cast<void (*)(uintptr_t, uintptr_t, int, bool)>(fn)(inventory, item, -1, true);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool guardedWriteInt(uintptr_t addr, int v) {
    __try {
        *reinterpret_cast<int*>(addr) = v;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

std::string readInventory(uintptr_t character) {
    uintptr_t inventory = 0;
    std::string text;
    for (auto& [section, item] : listItems(character, inventory)) {
        Ref tpl = refOf(item + kItemTemplate);
        if (tpl.id.empty()) continue;
        Ref a1 = refOf(item + kItemAux1), a2 = refOf(item + kItemAux2);
        int qty = 1;
        mem::readBytes(item + kItemQuantity, &qty, sizeof(qty));
        if (qty < 1 || qty > 100000) qty = 1;
        text += section + "\t" + tpl.id + "\t" + std::to_string(tpl.type) + "\t" + a1.id + "\t" +
                std::to_string(a1.type) + "\t" + a2.id + "\t" + std::to_string(a2.type) + "\t" + std::to_string(qty) + "\n";
    }
    return text;
}

int restoreInventory(uintptr_t character, const std::string& saved, const Items& items) {
    // 1. Remove what Kenshi's start gave the character.
    uintptr_t inventory = 0, vt = 0, removeFn = 0;
    auto current = listItems(character, inventory);
    if (!inventory || !mem::readPtr(inventory, vt) || !mem::readPtr(vt + kInvRemoveDestroyVt, removeFn)) return 0;
    for (auto& entry : current) guardedRemove(removeFn, inventory, entry.second);

    // 2. Recreate the saved items.
    int restored = 0;
    std::istringstream in(saved);
    std::string line;
    while (std::getline(in, line)) {
        std::vector<std::string> f;
        std::istringstream ls(line);
        for (std::string part; std::getline(ls, part, '\t');) f.push_back(part);
        if (f.size() < 8) continue;
        uintptr_t tpl = items.find(f[1], std::atoi(f[2].c_str()));
        uintptr_t a1 = f[3].empty() ? 0 : items.find(f[3], std::atoi(f[4].c_str()));
        uintptr_t a2 = f[5].empty() ? 0 : items.find(f[5], std::atoi(f[6].c_str()));
        if (!tpl) {
            log("inventory: unknown item %s (type %s), skipped", f[1].c_str(), f[2].c_str());
            continue;
        }
        // Weapons are built from their manufacturer (+ the weapon and its model); the rest from
        // their own record (+ armour grade / material when there is one).
        uintptr_t item = std::atoi(f[2].c_str()) == kWeaponType && a1 ? items.create(a1, tpl, a2) : items.create(tpl, 0, a2);
        int qty = std::atoi(f[7].c_str());
        if (item && qty > 1) guardedWriteInt(item + kItemQuantity, qty);
        if (item && items.give(character, item)) ++restored;
    }
    log("inventory: removed %zu starting item(s), restored %d", current.size(), restored);
    return restored;
}

}  // namespace wok
