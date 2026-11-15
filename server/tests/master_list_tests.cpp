#include <doctest/doctest.h>

#include <wok/master_list.hpp>

using namespace wok;

TEST_CASE("master_lists_announced_servers") {
    MasterList m;
    m.announce("95.179.253.252", 27800, 0.0);
    m.announce("1.2.3.4", 27900, 1.0);
    CHECK(m.listText(2.0) == "1.2.3.4:27900\n95.179.253.252:27800\n");
}

TEST_CASE("master_lists_servers_as_json_with_their_details") {
    MasterList m;
    m.announce("1.2.3.4", 27800, 0.0, {"Kenshi \"NA\"", 3, 250, true});
    m.announce("5.6.7.8", 27900, 0.0);  // an older server: address only
    CHECK(m.listJson(1.0) ==
          "[{\"address\":\"1.2.3.4:27800\",\"name\":\"Kenshi \\\"NA\\\"\",\"players\":3,\"maxPlayers\":250},"
          "{\"address\":\"5.6.7.8:27900\"}]\n");
}

TEST_CASE("master_forgets_silent_servers") {
    MasterList m;
    m.announce("1.2.3.4", 27800, 0.0);
    CHECK(m.listText(kMasterEntryTtlSec - 1.0) == "1.2.3.4:27800\n");
    CHECK(m.listText(kMasterEntryTtlSec + 1.0).empty());
}

TEST_CASE("master_reannounce_keeps_one_entry_and_refreshes_it") {
    MasterList m;
    m.announce("1.2.3.4", 27800, 0.0);
    m.announce("1.2.3.4", 27800, kMasterEntryTtlSec - 10.0);
    CHECK(m.listText(kMasterEntryTtlSec + 5.0) == "1.2.3.4:27800\n");
}

TEST_CASE("master_rejects_bad_announces") {
    MasterList m;
    CHECK_FALSE(m.announce("", 27800, 0.0));
    CHECK_FALSE(m.announce("1.2.3.4", 0, 0.0));
    CHECK_FALSE(m.announce("bad host\n", 27800, 0.0));
    CHECK(m.listText(1.0).empty());
}

TEST_CASE("master_has_a_size_limit") {
    MasterList m;
    for (int i = 0; i < static_cast<int>(kMasterMaxEntries) + 50; ++i)
        m.announce("10.0." + std::to_string(i / 250) + "." + std::to_string(i % 250), 27800, 0.0);
    size_t lines = 0;
    for (char c : m.listText(1.0)) lines += c == '\n';
    CHECK(lines == kMasterMaxEntries);
}
