#include <core/interp.hpp>

#include <algorithm>
#include <cmath>

namespace wok {

namespace {

float lerp(float a, float b, float f) { return a + (b - a) * f; }

float normalizeDeg(float d) {
    d = std::fmod(d, 360.0f);
    if (d < 0.0f) d += 360.0f;
    if (d >= 360.0f) d -= 360.0f;
    return d;
}

// Interpolates along the shortest arc (350 -> 10 goes through 0, not 180).
float lerpAngle(float a, float b, float f) {
    float diff = std::fmod(b - a + 540.0f, 360.0f) - 180.0f;
    return normalizeDeg(a + diff * f);
}

RemotePose poseOf(const PlayerState& s) { return {s.x, s.y, s.z, s.yaw, s.anim}; }

}  // namespace

void Interpolator::push(double recvTimeSec, const PlayerState& s) {
    if (!samples_.empty() && s.tick <= samples_.back().s.tick) return;  // stale or duplicate
    samples_.push_back({recvTimeSec, s});
    while (samples_.size() > 2 && recvTimeSec - samples_.front().t > 2.0) samples_.pop_front();
}

std::optional<RemotePose> Interpolator::sample(double nowSec) const {
    if (samples_.empty()) return std::nullopt;
    const double renderT = nowSec - kInterpDelay;
    const Sample& last = samples_.back();

    if (samples_.size() == 1 || renderT <= samples_.front().t) return poseOf(samples_.front().s);

    if (renderT >= last.t) {
        const Sample& prev = samples_[samples_.size() - 2];
        double span = last.t - prev.t;
        float dt = static_cast<float>(std::min(renderT - last.t, static_cast<double>(kMaxExtrapolation)));
        RemotePose p = poseOf(last.s);
        if (span > 1e-6) {
            p.x += static_cast<float>((last.s.x - prev.s.x) / span) * dt;
            p.y += static_cast<float>((last.s.y - prev.s.y) / span) * dt;
            p.z += static_cast<float>((last.s.z - prev.s.z) / span) * dt;
        }
        return p;
    }

    for (size_t i = 0; i + 1 < samples_.size(); ++i) {
        const Sample& a = samples_[i];
        const Sample& b = samples_[i + 1];
        if (renderT >= a.t && renderT < b.t) {
            float f = static_cast<float>((renderT - a.t) / (b.t - a.t));
            return RemotePose{lerp(a.s.x, b.s.x, f), lerp(a.s.y, b.s.y, f), lerp(a.s.z, b.s.z, f),
                              lerpAngle(a.s.yaw, b.s.yaw, f), b.s.anim};
        }
    }
    return poseOf(last.s);
}

}  // namespace wok
