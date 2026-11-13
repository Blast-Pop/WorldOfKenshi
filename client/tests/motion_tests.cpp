#include <doctest/doctest.h>

#include <core/motion.hpp>

#include <cmath>

using namespace wok;

TEST_CASE("yaw_identity_is_zero") { CHECK(yawFromQuat(1, 0, 0, 0) == doctest::Approx(0.0f)); }

TEST_CASE("yaw_from_vertical_rotation") {
    // 90 deg about Y: w = cos(45), y = sin(45)
    float h = std::sqrt(0.5f);
    CHECK(yawFromQuat(h, 0, h, 0) == doctest::Approx(90.0f).epsilon(0.001));
}

TEST_CASE("yaw_is_normalised_0_360") {
    // -90 deg about Y
    float h = std::sqrt(0.5f);
    CHECK(yawFromQuat(h, 0, -h, 0) == doctest::Approx(270.0f).epsilon(0.001));
    // value seen in game: w=-0.26, y=0.966
    float y = yawFromQuat(-0.26f, 0, 0.966f, 0);
    CHECK(y >= 0.0f);
    CHECK(y < 360.0f);
}

TEST_CASE("anim_from_speed") {
    CHECK(animFromSpeed(0.0f) == AnimState::Idle);
    CHECK(animFromSpeed(kIdleSpeed - 0.01f) == AnimState::Idle);
    CHECK(animFromSpeed(kIdleSpeed + 0.01f) == AnimState::Walk);
    CHECK(animFromSpeed(kRunSpeed + 0.01f) == AnimState::Run);
}

TEST_CASE("send_throttle_10hz") {
    SendThrottle t(10.0);
    CHECK(t.due(0.0));
    CHECK_FALSE(t.due(0.05));
    CHECK(t.due(0.1));
    CHECK_FALSE(t.due(0.15));
    CHECK(t.due(0.35));  // late frame: sends once, no burst
    CHECK_FALSE(t.due(0.36));
}
