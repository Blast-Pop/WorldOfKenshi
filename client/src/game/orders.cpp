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

bool orderMoveTo(uintptr_t character, const float pos[3]) {
    uintptr_t base = mem::moduleBase();
    uint8_t p[sizeof(kCreateTaskPrologue)];
    uintptr_t owner = 0, ctx = 0, queue = 0, vt = 0, addFn = 0, handVt = 0;
    if (!mem::isA(character, ".?AVCharacterHuman@@") || !mem::readBytes(base + kCreateTask, p, sizeof(p)) ||
        std::memcmp(p, kCreateTaskPrologue, sizeof(p)) != 0 || !mem::readPtr(character + kCharTaskOwner, owner) ||
        !owner || !mem::readPtr(owner + kTaskContextOff, ctx) || !ctx ||
        !mem::readPtr(character + kCharTaskQueue, queue) || !queue || !mem::readPtr(queue, vt) ||
        !mem::readPtr(vt + kAddTaskVt, addFn) || !mem::readPtr(base + kNullHand, handVt))
        return false;
    Hand target{handVt, 0xb, 0, 0, 0, 0};  // no target object: a plain move to a position
    uintptr_t task = guardedCreate(reinterpret_cast<CreateTaskFn>(base + kCreateTask), ctx, &target, pos);
    if (!task) return false;
    return guardedAdd(reinterpret_cast<AddTaskFn>(addFn), queue, task);
}

}  // namespace wok
