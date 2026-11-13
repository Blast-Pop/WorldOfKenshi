#pragma once
// Pure helpers turning raw game state into protocol values.

#include <wok/protocol.hpp>

namespace wok {

// Game units per second (measured in game; tune in docs/re/character.md).
inline constexpr float kIdleSpeed = 1.0f;
inline constexpr float kRunSpeed = 45.0f;

// Yaw in degrees, [0, 360), from an orientation quaternion rotating about the vertical axis.
float yawFromQuat(float w, float x, float y, float z);
AnimState animFromSpeed(float unitsPerSecond);

// Fires at most `hz` times per second without bursting after a long frame.
class SendThrottle {
public:
    explicit SendThrottle(double hz) : period_(1.0 / hz) {}
    bool due(double nowSec);

private:
    double period_;
    double next_ = -1.0;
};

}  // namespace wok
