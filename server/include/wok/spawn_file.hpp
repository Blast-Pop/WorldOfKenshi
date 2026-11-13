#pragma once
// Town spawn points file: one "Name:X,Y,Z" per line ('#' comments), as written by the client's
// town mapper (docs/re/towns-1.0.68.txt).

#include <wok/session.hpp>

#include <string_view>
#include <vector>

namespace wok {

std::vector<SpawnPoint> parseSpawnPoints(std::string_view text);
int findSpawn(const std::vector<SpawnPoint>& points, std::string_view name);  // index or -1

}  // namespace wok
