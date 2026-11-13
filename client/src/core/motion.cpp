#include <core/motion.hpp>

#include <cmath>

namespace wok {

float yawFromQuat(float w, float /*x*/, float y, float /*z*/) {
    float deg = 2.0f * std::atan2(y, w) * 57.2957795f;
    deg = std::fmod(deg, 360.0f);
    if (deg < 0.0f) deg += 360.0f;
    if (deg >= 360.0f) deg -= 360.0f;
    return deg;
}

AnimState animFromSpeed(float unitsPerSecond) {
    if (unitsPerSecond < kIdleSpeed) return AnimState::Idle;
    if (unitsPerSecond < kRunSpeed) return AnimState::Walk;
    return AnimState::Run;
}

bool SendThrottle::due(double nowSec) {
    if (nowSec < next_) return false;
    // Schedule from now when we are late, so a long frame never causes a burst of sends.
    next_ = (next_ < 0 || nowSec - next_ >= period_) ? nowSec + period_ : next_ + period_;
    return true;
}

}  // namespace wok
