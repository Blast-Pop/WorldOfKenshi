#pragma once
// Automatic updates (pure part): which release on GitHub is newer than this build, and where its
// client DLL is. The download and the file swap are in ../updater.cpp.

#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace wok {

inline constexpr const char* kClientVersion = "0.3.1";
// The repository's releases, newest first (pre-releases included), as GitHub's API returns them.
inline constexpr const char* kReleasesUrl = "https://api.github.com/repos/Blast-Pop/WorldOfKenshi/releases?per_page=5";
// Only files published on that repository's releases are ever downloaded.
inline constexpr const char* kDownloadPrefix = "https://github.com/Blast-Pop/WorldOfKenshi/releases/download/";
inline constexpr const char* kUpdateAsset = "wok_client.dll";

struct UpdateInfo {
    std::string version;  // e.g. "0.3.2"
    std::string url;      // the release's wok_client.dll
    std::string sha256;   // lower-case hex, empty if GitHub did not give one
    bool operator==(const UpdateInfo&) const = default;
};

// "v0.3.1-alpha" or "0.3.1" -> {0, 3, 1}; nothing if it is not three numbers.
std::optional<std::array<int, 3>> parseVersion(std::string_view tag);
bool isNewer(std::string_view candidate, std::string_view current);
// The newest release in GitHub's "list releases" JSON that is newer than `current` and carries the
// client DLL; nothing if this build is up to date.
std::optional<UpdateInfo> findUpdate(std::string_view releasesJson, std::string_view current);

}  // namespace wok
