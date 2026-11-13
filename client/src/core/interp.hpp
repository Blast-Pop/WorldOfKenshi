#pragma once
// Smooths a remote player's movement: positions are rendered kInterpDelay in the past and
// interpolated between the two surrounding network samples; past the newest sample they are
// extrapolated for at most kMaxExtrapolation seconds, then frozen.

#include <wok/protocol.hpp>

#include <deque>
#include <optional>

namespace wok {

inline constexpr double kInterpDelay = 0.100;
inline constexpr float kMaxExtrapolation = 0.25f;

struct RemotePose {
    float x, y, z, yaw;
    AnimState anim;
};

class Interpolator {
public:
    void push(double recvTimeSec, const PlayerState& s);
    std::optional<RemotePose> sample(double nowSec) const;

private:
    struct Sample {
        double t;
        PlayerState s;
    };
    std::deque<Sample> samples_;  // ordered by tick, at most a few seconds kept
};

}  // namespace wok
