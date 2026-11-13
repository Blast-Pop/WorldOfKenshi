#include "items.hpp"

#include "../log.hpp"
#include "game_memory.hpp"
#include "../ui/mygui_abi.hpp"

#include <windows.h>

#include <cstring>

namespace wok {

namespace {

constexpr uintptr_t kFactoryOff = 0x4a0;        // GameWorld -> item/character factory
constexpr uintptr_t kCreateItem = 0x580750;     // (factory, GameData* item, hand*, manufacturer, model, level, ptr)
constexpr uint8_t kCreateItemPrologue[] = {0x40, 0x55, 0x53, 0x56};  // push rbp; push rbx; push rsi
constexpr uintptr_t kNullHand = 0x1e3a5f8;      // a global empty "hand" (object handle); its vtable is reused
constexpr uintptr_t kCharacterGiveItemVt = 0x168;  // Character vtable slot: giveItem(Item*, bool)
constexpr uintptr_t kFindByType = 0x6bfe60;    // (GameData manager = GameWorld+0x20, const std::string*, int type)
constexpr uintptr_t kGameDataManagerOff = 0x20;

// Kenshi's object handle as passed by value-pointer to the factory (vtable + ids).
struct Hand {
    uintptr_t vtable;
    uint32_t type;
    uint32_t a;
    uint64_t b;
    uint32_t c;
    uint32_t pad;
};

using CreateItemFn = uintptr_t (*)(uintptr_t, uintptr_t, const Hand*, uintptr_t, uintptr_t, int, void*);
using GiveItemFn = void (*)(uintptr_t, uintptr_t, bool);

uintptr_t guardedCreate(CreateItemFn fn, uintptr_t factory, uintptr_t data, const Hand* h, uintptr_t manufacturer,
                        uintptr_t model) {
    __try {
        return fn(factory, data, h, manufacturer, model, 0, nullptr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

uintptr_t guardedFind(uintptr_t fn, uintptr_t manager, const mygui::Vc10String* id, int type) {
    __try {
        return reinterpret_cast<uintptr_t (*)(uintptr_t, const mygui::Vc10String*, int)>(fn)(manager, id, type);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

bool guardedGive(GiveItemFn fn, uintptr_t character, uintptr_t item) {
    __try {
        fn(character, item, true);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

bool Items::init(uintptr_t gameWorld) {
    gw_ = gameWorld;
    uint8_t p[sizeof(kCreateItemPrologue)];
    ok_ = gw_ && mem::readBytes(mem::moduleBase() + kCreateItem, p, sizeof(p)) &&
          std::memcmp(p, kCreateItemPrologue, sizeof(p)) == 0;
    log("items: %s", ok_ ? "ready" : "disabled (unexpected game version)");
    return ok_;
}

uintptr_t Items::create(uintptr_t itemData, uintptr_t manufacturer, uintptr_t model) const {
    uintptr_t factory = 0, handVt = 0;
    if (!ok_ || !itemData || !mem::isA(itemData, ".?AVGameData@@") || !mem::readPtr(gw_ + kFactoryOff, factory) ||
        !factory || !mem::readPtr(mem::moduleBase() + kNullHand, handVt))
        return 0;
    Hand h{handVt, 0xb, 0, 0, 0, 0};  // a fresh, unassigned handle (what the game passes for new items)
    uintptr_t item = guardedCreate(reinterpret_cast<CreateItemFn>(mem::moduleBase() + kCreateItem), factory, itemData,
                                   &h, manufacturer, model);
    return item && mem::isA(item, ".?AVGameData@@") ? 0 : item;
}

uintptr_t Items::find(const std::string& stringId, int type) const {
    if (!ok_ || stringId.empty()) return 0;
    mygui::Vc10String id(stringId);  // MSVC 2010 std::string layout, read-only for the game
    uintptr_t gd = guardedFind(mem::moduleBase() + kFindByType, gw_ + kGameDataManagerOff, &id, type);
    return gd && mem::isA(gd, ".?AVGameData@@") ? gd : 0;
}

bool Items::give(uintptr_t character, uintptr_t item) const {
    uintptr_t vt = 0, fn = 0;
    if (!ok_ || !item || !mem::isA(character, ".?AVCharacterHuman@@") || !mem::readPtr(character, vt) ||
        !mem::readPtr(vt + kCharacterGiveItemVt, fn))
        return false;
    return guardedGive(reinterpret_cast<GiveItemFn>(fn), character, item);
}

}  // namespace wok
