#include <doctest/doctest.h>

#include <core/paths.hpp>

using namespace wok;

TEST_CASE("mod_dir_is_next_to_exe") {
    CHECK(modDirFromExe("D:\\SteamLibrary\\steamapps\\common\\Kenshi\\kenshi_x64.exe") ==
          "D:\\SteamLibrary\\steamapps\\common\\Kenshi\\WorldOfKenshi");
}

TEST_CASE("mod_dir_accepts_forward_slashes") {
    CHECK(modDirFromExe("C:/Games/Kenshi/kenshi_x64.exe") == "C:/Games/Kenshi\\WorldOfKenshi");
}

TEST_CASE("mod_file_joins_with_backslash") {
    CHECK(modFile("D:\\K\\WorldOfKenshi", "wok_client.log") == "D:\\K\\WorldOfKenshi\\wok_client.log");
}
