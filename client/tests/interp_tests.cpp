#include <doctest/doctest.h>

#include <core/interp.hpp>

using namespace wok;

namespace {
PlayerState st(uint32_t tick, float x, float yaw = 0.0f) {
    PlayerState s{};
    s.playerId = 1;
    s.tick = tick;
    s.x = x;
    s.yaw = yaw;
    s.anim = AnimState::Walk;
    return s;
}
}  // namespace

TEST_CASE("interp_empty_returns_nothing") {
    Interpolator in;
    CHECK_FALSE(in.sample(1.0).has_value());
}

TEST_CASE("interp_single_sample_returns_it") {
    Interpolator in;
    in.push(0.0, st(1, 7.0f));
    auto s = in.sample(0.5);
    REQUIRE(s.has_value());
    CHECK(s->x == doctest::Approx(7.0f));
}

TEST_CASE("interp_midpoint") {
    Interpolator in;
    in.push(0.0, st(1, 0.0f));
    in.push(0.1, st(2, 10.0f));
    auto s = in.sample(0.15);  // rendered at 0.05
    REQUIRE(s.has_value());
    CHECK(s->x == doctest::Approx(5.0f));
}

TEST_CASE("interp_extrapolation_capped") {
    Interpolator in;
    in.push(0.0, st(1, 0.0f));
    in.push(0.1, st(2, 10.0f));  // 100 units/s
    auto s = in.sample(1.1);     // rendered at 1.0, far past the last sample
    REQUIRE(s.has_value());
    CHECK(s->x == doctest::Approx(10.0f + 100.0f * kMaxExtrapolation));
}

TEST_CASE("interp_out_of_order_ignored") {
    Interpolator in;
    in.push(0.0, st(5, 0.0f));
    in.push(0.1, st(6, 10.0f));
    in.push(0.2, st(4, 999.0f));  // older tick arriving late
    auto s = in.sample(0.2);      // rendered at 0.1
    REQUIRE(s.has_value());
    CHECK(s->x == doctest::Approx(10.0f));
}

TEST_CASE("interp_yaw_wraps") {
    Interpolator in;
    in.push(0.0, st(1, 0.0f, 350.0f));
    in.push(0.1, st(2, 0.0f, 10.0f));
    auto s = in.sample(0.15);
    REQUIRE(s.has_value());
    CHECK(s->yaw == doctest::Approx(0.0f).epsilon(0.01));
}

TEST_CASE("interp_keeps_latest_anim") {
    Interpolator in;
    in.push(0.0, st(1, 0.0f));
    auto idle = st(2, 0.0f);
    idle.anim = AnimState::Idle;
    in.push(0.1, idle);
    auto s = in.sample(1.0);
    REQUIRE(s.has_value());
    CHECK(s->anim == AnimState::Idle);
}
