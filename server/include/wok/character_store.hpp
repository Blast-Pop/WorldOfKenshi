#pragma once
// Server-side persistence of players: per-server auth token and the player's character, keyed by
// SteamID (SQLite file, or ":memory:" for tests).

#include <wok/protocol.hpp>

#include <cstdint>
#include <optional>
#include <string>

struct sqlite3;

namespace wok {

class CharacterStore {
public:
    explicit CharacterStore(const std::string& path);  // throws std::runtime_error if it cannot open
    ~CharacterStore();
    CharacterStore(const CharacterStore&) = delete;
    CharacterStore& operator=(const CharacterStore&) = delete;

    std::optional<AuthToken> tokenFor(uint64_t steamId);
    void setToken(uint64_t steamId, const AuthToken& token);

    std::optional<CharacterData> load(uint64_t steamId);
    bool create(uint64_t steamId, const CharacterData& c);  // false if the player already has one
    void savePosition(uint64_t steamId, float x, float y, float z, float yaw);

private:
    sqlite3* db_ = nullptr;
};

}  // namespace wok
