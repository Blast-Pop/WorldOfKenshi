#include <doctest/doctest.h>

#include <wok/spawn_file.hpp>

using namespace wok;

TEST_CASE("spawn_file_parses_points_comments_and_bad_lines") {
    auto pts = parseSpawnPoints("# towns\nThe Hub:-50978.8,1533.1,2932.5\r\n\nTrader's Edge:49563.0,1366.2,-43045.0\nbroken\nNoCoords:\n");
    REQUIRE(pts.size() == 2);
    CHECK(pts[0].name == "The Hub");
    CHECK(pts[0].x == doctest::Approx(-50978.8f));
    CHECK(pts[0].z == doctest::Approx(2932.5f));
    CHECK(pts[1].name == "Trader's Edge");
}

TEST_CASE("spawn_file_find_by_name") {
    auto pts = parseSpawnPoints("Squin:1,2,3\nThe Hub:4,5,6\n");
    CHECK(findSpawn(pts, "The Hub") == 1);
    CHECK(findSpawn(pts, "Stoat") == -1);
}
