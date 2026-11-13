#pragma once
// The world's clock, owned by the server (spec 7.7): it keeps running at Kenshi's normal rate even
// with nobody online, and clients align their game clock to it.

#include <wok/protocol.hpp>

namespace wok {

// Kenshi at normal speed advances 0.0091666 game hours per real second (one game day ~ 43.6 min).
inline constexpr double kGameHoursPerSecond = 0.009166666;

class WorldClock {
public:
    WorldClock(double gameHoursAtStart, double nowSec) : startHours_(gameHoursAtStart), startSec_(nowSec) {}
    double gameHours(double nowSec) const { return startHours_ + (nowSec - startSec_) * kGameHoursPerSecond; }
    WorldTime message(double nowSec) const;

private:
    double startHours_;
    double startSec_;
};

}  // namespace wok
