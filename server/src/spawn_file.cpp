#include <wok/spawn_file.hpp>

#include <cstdio>
#include <string>

namespace wok {

std::vector<SpawnPoint> parseSpawnPoints(std::string_view text) {
    std::vector<SpawnPoint> out;
    while (!text.empty()) {
        size_t eol = text.find('\n');
        std::string line(text.substr(0, eol));
        text = eol == std::string_view::npos ? std::string_view{} : text.substr(eol + 1);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        size_t colon = line.rfind(':');
        if (colon == std::string::npos || colon == 0) continue;
        SpawnPoint sp;
        if (std::sscanf(line.c_str() + colon + 1, "%f,%f,%f", &sp.x, &sp.y, &sp.z) != 3) continue;
        sp.name = line.substr(0, colon);
        out.push_back(sp);
    }
    return out;
}

int findSpawn(const std::vector<SpawnPoint>& points, std::string_view name) {
    for (size_t i = 0; i < points.size(); ++i)
        if (points[i].name == name) return static_cast<int>(i);
    return -1;
}

}  // namespace wok
