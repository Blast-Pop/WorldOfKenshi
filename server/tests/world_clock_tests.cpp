#include <doctest/doctest.h>

#include <wok/world_clock.hpp>

using namespace wok;

TEST_CASE("world_clock_runs_at_kenshi_rate_without_players") {
    WorldClock c(10.0, 0.0);              // 10 game hours since the world began
    CHECK(c.gameHours(0.0) == doctest::Approx(10.0));
    CHECK(c.gameHours(109.09) == doctest::Approx(11.0).epsilon(0.001));  // one game hour = ~109 s
    CHECK(c.gameHours(2618.2) == doctest::Approx(34.0).epsilon(0.001));  // a full day later
}

TEST_CASE("world_clock_message_splits_day_and_hour") {
    WorldClock c(24.0 * 3 + 6.5, 0.0);  // day 4 (Kenshi counts from 1), 06:30
    WorldTime t = c.message(0.0);
    CHECK(t.day == 4);
    CHECK(t.hour == doctest::Approx(6.5f));
}
