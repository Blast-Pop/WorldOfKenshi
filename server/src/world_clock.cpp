#include <wok/world_clock.hpp>

#include <cmath>

namespace wok {

WorldTime WorldClock::message(double nowSec) const {
    double h = gameHours(nowSec);
    double days = std::floor(h / 24.0);
    WorldTime t;
    t.day = static_cast<uint32_t>(days) + 1;  // Kenshi's first day is "Day 1"
    t.hour = static_cast<float>(h - days * 24.0);
    return t;
}

}  // namespace wok
