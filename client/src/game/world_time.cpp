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

// Kenshi's game time is day * 24 + hour (time object + 0xa0, RVA 0x66cf30), and everything that
// ages (hunger, healing, timers) uses "now - last update". Moving the clock is therefore time that
// passes for every character. So only the hour of day follows the server (day/night match), always
// forward and by less than a day; the local day counter is left alone.
bool applyWorldTime(uintptr_t, const WorldTime& t) {
    uintptr_t timeSys = 0, controller = 0;
    int day = 0;
    float hour = 0;
    if (!mem::readPtr(mem::moduleBase() + kTimeSystem, timeSys) || !timeSys ||
        !mem::readPtr(timeSys + kSkyController, controller) || !controller ||
        !mem::readBytes(timeSys + kDay, &day, sizeof(day)) ||
        !mem::readBytes(controller + kHourOfDay, &hour, sizeof(hour)) || !(hour >= 0 && hour <= 24))
        return false;
    float ahead = t.hour - hour;  // forward distance to the server's hour, in [0, 24)
    if (ahead < 0) ahead += 24.0f;
    if (ahead < kToleranceHours || ahead > 24.0f - kToleranceHours) return false;
    int newDay = day;
    if (t.hour < hour) ++newDay;  // past midnight: the next day
    mem::writeBytes(timeSys + kDay, &newDay, sizeof(newDay));
    mem::writeBytes(controller + kHourOfDay, &t.hour, sizeof(t.hour));
    log("time: game clock moved %.2f h forward to %.2f h (server day %u, local day %d)", ahead, t.hour, t.day,
        newDay);
    return true;
}

}  // namespace wok
