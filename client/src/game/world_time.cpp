#include "world_time.hpp"

#include "../log.hpp"
#include "game_memory.hpp"

#include <cmath>

namespace wok {

namespace {
constexpr uintptr_t kTimeSystem = 0x21303d0;   // RVA of the global pointer to the time-of-day object
constexpr uintptr_t kDay = 0x08;               // time object: int, the "Day N" shown in game
constexpr uintptr_t kSkyController = 0x20;     // time object: SkyX time controller
constexpr uintptr_t kHourOfDay = 0x1c;         // controller: float, 0-24
constexpr float kToleranceHours = 0.05f;       // 3 game minutes
}  // namespace

bool applyWorldTime(uintptr_t, const WorldTime& t) {
    uintptr_t timeSys = 0, controller = 0;
    int day = 0;
    float hour = 0;
    if (!mem::readPtr(mem::moduleBase() + kTimeSystem, timeSys) || !timeSys ||
        !mem::readPtr(timeSys + kSkyController, controller) || !controller ||
        !mem::readBytes(timeSys + kDay, &day, sizeof(day)) ||
        !mem::readBytes(controller + kHourOfDay, &hour, sizeof(hour)) || !(hour >= 0 && hour <= 24))
        return false;
    int wantDay = static_cast<int>(t.day);
    if (day == wantDay && std::fabs(hour - t.hour) < kToleranceHours) return false;
    mem::writeBytes(timeSys + kDay, &wantDay, sizeof(wantDay));
    mem::writeBytes(controller + kHourOfDay, &t.hour, sizeof(t.hour));
    log("time: game clock set to day %d %.2f h (was day %d %.2f h)", wantDay, t.hour, day, hour);
    return true;
}

}  // namespace wok
