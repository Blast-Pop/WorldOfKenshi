#include "orders.hpp"

#include "../log.hpp"
#include "game_memory.hpp"

#include <windows.h>

#include <cstring>

namespace wok {

namespace {

constexpr uintptr_t kCreateTask = 0x32ebd0;   // (ctx, type, hand* target, int, float, const Vector3*, int, int) -> Task*
constexpr uint8_t kCreateTaskPrologue[] = {0x40, 0x55, 0x56, 0x57};  // push rbp; push rsi; push rdi
constexpr int kTaskMove = 1;                   // builds a Task_Move
constexpr uintptr_t kCharTaskOwner = 0x650;    // Character -> object whose +0x2a8 is the task context
constexpr uintptr_t kTaskContextOff = 0x2a8;
constexpr uintptr_t kCharTaskQueue = 0x648;    // Character -> object taking tasks (vtable +0x18)
constexpr uintptr_t kAddTaskVt = 0x18;
constexpr uintptr_t kNullHand = 0x1e3a5f8;     // a global empty object handle

struct Hand {
    uintptr_t vtable;
    uint32_t type, a;
    uint64_t b;
    uint32_t c, pad;
};

using CreateTaskFn = uintptr_t (*)(uintptr_t, int, const Hand*, int, float, const float*, int, int);
using AddTaskFn = void (*)(uintptr_t, uintptr_t);

uintptr_t guardedCreate(CreateTaskFn fn, uintptr_t ctx, const Hand* target, const float* pos) {
    __try {
        return fn(ctx, kTaskMove, target, 3, 1.0f, pos, 0, 100);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

bool guardedAdd(AddTaskFn fn, uintptr_t queue, uintptr_t task) {
    __try {
        fn(queue, task);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

// Kenshi's "add an order" on a character's AI (RVA 0x5086d0):
// (ai, int taskType, hand* target, const Vector3* pos, bool playerOrder). It builds the task with
// the task factory, stores it in the AI's order list and makes the AI re-plan; a task that is only
// created (as tried before) is dropped at once, the character moving a few centimetres.
constexpr uintptr_t kAddOrder = 0x5086d0;
constexpr uint8_t kAddOrderPrologue[] = {0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x20, 0x55, 0x41, 0x54};
using AddOrderFn = void (*)(uintptr_t, int, Hand*, const float*, bool);

bool guardedAddOrder(AddOrderFn fn, uintptr_t ai, int type, Hand* target, const float* pos, bool player) {
    __try {
        fn(ai, type, target, pos, player);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool orderMoveTo(uintptr_t character, const float pos[3]) {
    uintptr_t base = mem::moduleBase(), owner = 0, ai = 0, handVt = 0;  // AI: *(*(character + 0x650) + 0x20)
    uint8_t p[sizeof(kAddOrderPrologue)];
    if (!mem::isA(character, ".?AVCharacterHuman@@") || !mem::readBytes(base + kAddOrder, p, sizeof(p)) ||
        std::memcmp(p, kAddOrderPrologue, sizeof(p)) != 0 || !mem::readPtr(character + kCharTaskOwner, owner) || !owner || !mem::readPtr(owner + 0x20, ai) ||
        !ai || !mem::readPtr(base + kNullHand, handVt))
        return false;
    Hand target{handVt, 0xb, 0, 0, 0, 0};  // no target object: a plain move to a position
    return guardedAddOrder(reinterpret_cast<AddOrderFn>(base + kAddOrder), ai, kTaskMove, &target, pos, true);
}

namespace {
constexpr uintptr_t kCharMovementVtable = 0x16fcc88;  // RVA of CharMovement's vtable (1.0.68)
constexpr uintptr_t kSetDestinationSlot = 0x90;

bool callSetDestination(uintptr_t fn, uintptr_t mv, const float* pos, int mode) {
    __try {
        reinterpret_cast<void (*)(uintptr_t, const float*, int, int)>(fn)(mv, pos, mode, 0);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
}  // namespace

uintptr_t charMovement(uintptr_t character) {
    static size_t offset = 0;  // found once: the same for every character
    const uintptr_t vt = mem::moduleBase() + kCharMovementVtable;
    auto isMovement = [&](uintptr_t p) {
        uintptr_t v = 0;
        return p && mem::readPtr(p, v) && v == vt;
    };
    uintptr_t p = 0;
    if (offset && mem::readPtr(character + offset, p) && isMovement(p)) return p;
    for (size_t off = 8; off < 0x1000; off += 8)
        if (mem::readPtr(character + off, p) && isMovement(p)) {
            if (offset != off) log("orders: CharMovement at character + 0x%zx", off);
            offset = off;
            return p;
        }
    return 0;
}

bool setDestination(uintptr_t character, const float pos[3], int mode) {
    uintptr_t mv = charMovement(character), vt = 0, fn = 0;
    if (!mv || !mem::readPtr(mv, vt) || !mem::readPtr(vt + kSetDestinationSlot, fn) || !fn) return false;
    return callSetDestination(fn, mv, pos, mode);
}

}  // namespace wok
